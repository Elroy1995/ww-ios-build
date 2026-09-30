// BlueWake for Windows: the player's disc, the first time and whenever it goes
// missing. See win_disc.h.
//
// A downloaded BlueWake has no game in it. The first launch asks for the
// player's own disc image (GameCube USA, GZLE01 revision 0): an .iso or .gcm
// is used where it is; a Dolphin image (.rvz, .wia, .gcz, .ciso, .nfs) is
// unpacked once to %APPDATA%\BlueWake\GZLE01.iso with the bundled nodtool.
// The disc is checked (its id, and main.dol's SHA-1 for revision 0) and
// prepared once, by the iOS app's own importer (apple/ios/src/disc_import.c):
// main.dol and the 415 RELs, into %APPDATA%\BlueWake\game. disc.txt remembers
// the disc; later launches start straight away.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <process.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disc_import.h"
#include "win_disc.h"

// Every GameCube disc image, unpacked, is this long.
#define BW_GC_DISC_SIZE 1459978240ull

static const char* g_exe;
static const char* g_data;

static int is_file(const char* path) {
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_dir(const char* path) {
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void wide(const char* utf8, wchar_t* out, int count) {
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, count) <= 0)
        out[0] = L'\0';
}

static void box(const char* text, UINT flags) {
    if (getenv("BLUEWAKE_NO_DIALOG") != NULL)  // scripted runs: the log has it
        return;
    wchar_t message[4096];
    wide(text, message, 4096);
    MessageBoxW(NULL, message, L"BlueWake", flags);
}

// A line of text from a file (the remembered disc), without its line end.
static int read_line(const char* path, char* out, size_t size) {
    out[0] = '\0';
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    if (fgets(out, (int)size, f) == NULL)
        out[0] = '\0';
    fclose(f);
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = '\0';
    return n > 0;
}

static void write_line(const char* path, const char* line) {
    FILE* f = fopen(path, "wb");
    if (f == NULL)
        return;
    fprintf(f, "%s\n", line);
    fclose(f);
}

// What the prepared files were made from: the disc's path, size and time, so
// a different or changed disc is prepared again.
static void disc_stamp(const char* disc, char* out, size_t size) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(disc, GetFileExInfoStandard, &info)) {
        snprintf(out, size, "%s", disc);
        return;
    }
    snprintf(out, size, "%s|%llu|%llu", disc,
             ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow,
             ((unsigned long long)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime);
}

static const char* extension(const char* path) {
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, '\\');
    return dot != NULL && (slash == NULL || dot > slash) ? dot : "";
}

// Dolphin's compressed and other formats, which nodtool unpacks to an ISO.
static int needs_unpacking(const char* path) {
    static const char* const kinds[] = {".rvz", ".wia", ".gcz", ".ciso", ".nfs", ".wbfs"};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (_stricmp(extension(path), kinds[i]) == 0)
            return 1;
    return 0;
}

// --- a progress window while the work runs on another thread ----------------

typedef struct Work {
    int (*run)(struct Work*);
    const char* disc;
    char out[MAX_PATH * 4];
    char error[1024];
    volatile LONG fraction;  // 0-1000
    char stage[128];
    HWND window;
    int result;
} Work;

#define WM_BW_PROGRESS (WM_APP + 1)

static LRESULT CALLBACK progress_proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE)
        return 0;  // the work finishes first
    return DefWindowProcW(w, message, wp, lp);
}

static unsigned __stdcall work_main(void* arg) {
    Work* work = (Work*)arg;
    work->result = work->run(work);
    PostMessageW(work->window, WM_BW_PROGRESS, 1, 0);
    return 0;
}

static void note_progress(void* context, double fraction, const char* stage) {
    Work* work = (Work*)context;
    InterlockedExchange(&work->fraction, (LONG)(fraction * 1000.0));
    if (stage != NULL)
        snprintf(work->stage, sizeof work->stage, "%s", stage);
    PostMessageW(work->window, WM_BW_PROGRESS, 0, 0);
}

// Runs work->run on a thread with a small window showing `title` and its
// progress; returns its result.
static int with_progress(Work* work, const wchar_t* title) {
    INITCOMMONCONTROLSEX controls = {sizeof controls, ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = progress_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.hIcon = LoadIconW(cls.hInstance, MAKEINTRESOURCEW(1));
    cls.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_WAIT);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = L"BlueWakeDiscProgress";
    RegisterClassW(&cls);
    const UINT dpi = GetDpiForSystem();
    const int width = MulDiv(440, dpi, 96), height = MulDiv(130, dpi, 96);
    HWND w = CreateWindowExW(0, cls.lpszClassName, L"BlueWake", WS_POPUP | WS_CAPTION | WS_VISIBLE,
                             (GetSystemMetrics(SM_CXSCREEN) - width) / 2, (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                             width, height, NULL, NULL, cls.hInstance, NULL);
    NONCLIENTMETRICSW metrics = {sizeof metrics};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0, dpi);
    HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
    const int pad = MulDiv(16, dpi, 96);
    HWND text = CreateWindowExW(0, L"STATIC", title, WS_CHILD | WS_VISIBLE, pad, pad, width - 2 * pad,
                                MulDiv(22, dpi, 96), w, NULL, cls.hInstance, NULL);
    HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, pad, pad + MulDiv(30, dpi, 96),
                               width - 2 * pad - GetSystemMetrics(SM_CXFIXEDFRAME) * 2, MulDiv(20, dpi, 96), w, NULL,
                               cls.hInstance, NULL);
    SendMessageW(text, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
    work->window = w;
    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, work_main, work, 0, NULL);
    MSG message;
    int done = thread == NULL;
    if (thread == NULL)
        work->result = -1;
    while (!done && GetMessageW(&message, NULL, 0, 0) > 0) {
        if (message.hwnd == w && message.message == WM_BW_PROGRESS) {
            SendMessageW(bar, PBM_SETPOS, (WPARAM)InterlockedCompareExchange(&work->fraction, 0, 0), 0);
            if (message.wParam == 1)
                done = 1;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (thread != NULL) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    DestroyWindow(w);
    DeleteObject(font);
    return work->result;
}

// --- the steps ---------------------------------------------------------------

// nodtool (beside BlueWake.exe) unpacks the image to work->out; the window
// follows the ISO as it grows.
static int unpack(Work* work) {
    char tool[MAX_PATH * 4], pending[MAX_PATH * 4];
    snprintf(tool, sizeof tool, "%snodtool.exe", g_exe);
    // nodtool chooses the output format by its extension.
    snprintf(pending, sizeof pending, "%sGZLE01.unpacking.iso", g_data);
    if (!is_file(tool)) {
        snprintf(work->error, sizeof work->error,
                 "nodtool.exe, which unpacks %s images, is missing from BlueWake's folder. Unpack the whole "
                 "download again, or convert the image to ISO in Dolphin (right-click the game, Convert File).",
                 extension(work->disc));
        return -1;
    }
    DeleteFileA(pending);
    wchar_t command[MAX_PATH * 8 + 64], wtool[MAX_PATH * 2], wdisc[MAX_PATH * 2], wpending[MAX_PATH * 2];
    wide(tool, wtool, MAX_PATH * 2);
    wide(work->disc, wdisc, MAX_PATH * 2);
    wide(pending, wpending, MAX_PATH * 2);
    _snwprintf(command, sizeof command / sizeof command[0], L"\"%ls\" --no-color convert \"%ls\" \"%ls\"", wtool, wdisc,
               wpending);
    STARTUPINFOW startup = {sizeof startup};
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessW(wtool, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        snprintf(work->error, sizeof work->error, "nodtool could not start (error %lu).", GetLastError());
        return -1;
    }
    while (WaitForSingleObject(process.hProcess, 200) == WAIT_TIMEOUT) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExA(pending, GetFileExInfoStandard, &info)) {
            const unsigned long long size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
            note_progress(work, size >= BW_GC_DISC_SIZE ? 1.0 : (double)size / BW_GC_DISC_SIZE, NULL);
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0 || !is_file(pending)) {
        DeleteFileA(pending);
        snprintf(work->error, sizeof work->error,
                 "nodtool could not unpack this image (exit code %lu). Is it a GameCube disc image?", code);
        return -1;
    }
    snprintf(work->out, sizeof work->out, "%sGZLE01.iso", g_data);
    if (!MoveFileExA(pending, work->out, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(pending);
        snprintf(work->error, sizeof work->error, "The unpacked image could not be kept in %s (error %lu).", g_data,
                 GetLastError());
        return -1;
    }
    return 0;
}

static int prepare(Work* work) {
    return bluewake_disc_prepare(work->disc, work->out, note_progress, work, work->error, sizeof work->error);
}

// The explanation, then Windows' file picker. 1 and the chosen path, or 0
// when the player cancels.
static int choose(const char* why, char* out, size_t size) {
    const char* testing = getenv("BLUEWAKE_DISC_CHOICE");  // scripted tests: the picker's answer
    if (testing != NULL) {
        snprintf(out, size, "%s", testing);
        return testing[0] != '\0';
    }
    char text[2048];
    snprintf(text, sizeof text,
             "%s"
             "BlueWake plays The Legend of Zelda: The Wind Waker from your own copy of the game: the "
             "GameCube disc for the USA (GZLE01).\n\n"
             "Choose your disc image next: an .iso or .gcm file, or a Dolphin .rvz. BlueWake checks it and "
             "prepares it once (an .rvz is unpacked to an ISO in %s), then remembers it.",
             why, g_data);
    wchar_t message[4096];
    wide(text, message, 4096);
    if (MessageBoxW(NULL, message, L"BlueWake: choose your disc", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return 0;
    wchar_t file[MAX_PATH * 2] = L"";
    OPENFILENAMEW dialog = {sizeof dialog};
    dialog.lpstrFilter =
        L"GameCube disc images (*.iso, *.gcm, *.rvz, *.wia, *.gcz, *.ciso, *.nfs)\0"
        L"*.iso;*.gcm;*.rvz;*.wia;*.gcz;*.ciso;*.nfs\0All files\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH * 2;
    dialog.lpstrTitle = L"Your The Wind Waker disc image (GameCube, USA)";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog))
        return 0;
    return WideCharToMultiByte(CP_UTF8, 0, file, -1, out, (int)size, NULL, NULL) > 0;
}

int bw_disc_setup(const char* exe_dir, const char* data_dir) {
    g_exe = exe_dir;
    g_data = data_dir;
    // Everything given already (the builder's training, scripted runs).
    const char* given_dol = getenv("BLUEWAKE_DOL");
    if (given_dol != NULL && given_dol[0] != '\0')
        return 0;
    char path[MAX_PATH * 4];
    const char* given = getenv("BLUEWAKE_DISC");
    const int explicit_disc = given != NULL && given[0] != '\0';
    // A folder the builder made from the player's disc has it all beside the app.
    char app_disc[MAX_PATH * 4], app_dol[MAX_PATH * 4], app_rels[MAX_PATH * 4];
    snprintf(app_disc, sizeof app_disc, "%sgame\\GZLE01.iso", exe_dir);
    snprintf(app_dol, sizeof app_dol, "%sgame\\main.dol", exe_dir);
    snprintf(app_rels, sizeof app_rels, "%sgame\\rels", exe_dir);
    if (!explicit_disc && is_file(app_disc) && is_file(app_dol) && is_dir(app_rels))
        return 0;
    if (explicit_disc && is_file(app_dol) && is_dir(app_rels))
        return 0;

    char remembered[MAX_PATH * 4], game[MAX_PATH * 4], dol[MAX_PATH * 4], rels[MAX_PATH * 4], stamp_path[MAX_PATH * 4];
    snprintf(remembered, sizeof remembered, "%sdisc.txt", data_dir);
    snprintf(game, sizeof game, "%sgame", data_dir);
    snprintf(dol, sizeof dol, "%s\\main.dol", game);
    snprintf(rels, sizeof rels, "%s\\rels", game);
    snprintf(stamp_path, sizeof stamp_path, "%s\\prepared.txt", game);
    char disc[MAX_PATH * 4] = "";
    if (explicit_disc)
        snprintf(disc, sizeof disc, "%s", given);
    else
        read_line(remembered, disc, sizeof disc);
    if (explicit_disc && !is_file(disc)) {
        char text[MAX_PATH * 4 + 128];
        snprintf(text, sizeof text, "BlueWake cannot find the disc image it was given:\n\n%s", disc);
        fprintf(stderr, "[disc] %s\n", text);
        box(text, MB_OK | MB_ICONERROR);
        return -1;
    }
    const char* why = disc[0] != '\0' && !is_file(disc) ? "The disc image BlueWake used before is gone.\n\n" : "";
    // A scripted choice is not asked again once it was refused.
    const int scripted = getenv("BLUEWAKE_DISC_CHOICE") != NULL;
    int unpacked = 0;  // disc is the ISO nodtool made from the chosen image
    for (;;) {
        if (disc[0] == '\0' || !is_file(disc)) {
            if (!choose(why, path, sizeof path)) {
                fprintf(stderr, "[disc] no disc image chosen\n");
                return 1;
            }
            snprintf(disc, sizeof disc, "%s", path);
            unpacked = 0;
            if (needs_unpacking(disc)) {
                Work work = {unpack, disc};
                fprintf(stderr, "[disc] unpacking %s\n", disc);
                if (with_progress(&work, L"Unpacking your disc image to an ISO (once)...") != 0) {
                    fprintf(stderr, "[disc] %s\n", work.error);
                    box(work.error, MB_OK | MB_ICONWARNING);
                    if (scripted)
                        return -1;
                    disc[0] = '\0';
                    why = "";
                    continue;
                }
                snprintf(disc, sizeof disc, "%s", work.out);
                unpacked = 1;
            }
        }
        char error[1024] = "";
        if (bluewake_disc_check(disc, error, sizeof error) != 0) {
            char text[2048];
            snprintf(text, sizeof text,
                     "%s\n\n%s\n\nChoose an image of the GameCube USA disc (GZLE01) instead: an .iso, .gcm or "
                     "Dolphin .rvz file.",
                     disc, error);
            fprintf(stderr, "[disc] %s: %s\n", disc, error);
            box(text, MB_OK | MB_ICONWARNING);
            if (unpacked)
                DeleteFileA(disc);  // another game's image: no use keeping it
            if (explicit_disc || scripted)
                return -1;
            disc[0] = '\0';
            why = "";
            continue;
        }
        char stamp[MAX_PATH * 4 + 64], prepared[MAX_PATH * 4 + 64];
        disc_stamp(disc, stamp, sizeof stamp);
        read_line(stamp_path, prepared, sizeof prepared);
        if (strcmp(stamp, prepared) != 0 || !is_file(dol) || !is_dir(rels)) {
            CreateDirectoryA(game, NULL);
            Work work = {prepare, disc};
            snprintf(work.out, sizeof work.out, "%s", game);
            fprintf(stderr, "[disc] preparing %s\n", disc);
            if (with_progress(&work, L"Checking and preparing your disc (once)...") != 0) {
                char text[2048];
                snprintf(text, sizeof text, "%s\n\n%s\n\nBlueWake needs the USA disc, revision 0 (GZLE01).", disc,
                         work.error);
                fprintf(stderr, "[disc] %s: %s\n", disc, work.error);
                box(text, MB_OK | MB_ICONWARNING);
                if (unpacked)
                    DeleteFileA(disc);
                if (explicit_disc || scripted)
                    return -1;
                disc[0] = '\0';
                why = "";
                continue;
            }
            write_line(stamp_path, stamp);
        }
        break;
    }
    if (!explicit_disc)
        write_line(remembered, disc);
    _putenv_s("BLUEWAKE_DISC", disc);
    _putenv_s("BLUEWAKE_DOL", dol);
    _putenv_s("BLUEWAKE_RELS_DIR", rels);
    fprintf(stderr, "[disc] %s (prepared in %s)\n", disc, game);
    return 0;
}
