// The executable of the Mac app scripts/mac/package_app.sh assembles. It points
// the BlueWake host at the game module and game data inside the app, keeps the
// memory card, SRAM, settings and logs in ~/Library/Application Support/Wind
// Waker Recomp, and runs the host. A double-clicked app gets no environment,
// so everything the host needs is set here; a variable already set wins.
//
// settings.txt in that folder holds KEY=VALUE lines read at every launch (the
// first launch writes one with the defaults and the choices commented).
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char kDefaultSettings[] =
    "# Wind Waker Recomp settings, read at every launch. KEY=VALUE; # starts a comment.\n"
    "#\n"
    "# The picture: 16:10 or 16:9 widescreen (remove the line for the original 4:3),\n"
    "# fullscreen, and Smooth Motion (60 frames a second from the game's 30).\n"
    "BLUEWAKE_ASPECT=16:10\n"
    "DOL_AURORA_FULLSCREEN=1\n"
    "DOL_AURORA_FRAME_INTERP=1\n"
    "# Experimental physics and gameplay at 60 Hz, instead of in-between frames.\n"
    "# Requires a rebuilt game module; cutscenes and transitions keep original timing.\n"
    "BLUEWAKE_SIMULATION_60HZ=0\n"
    "# A frames-a-second counter at the top of the screen.\n"
    "DOL_AURORA_SHOW_FPS=0\n"
    "# Render resolution as a multiple of 480 lines (0 = the window's pixels).\n"
    "# DOL_AURORA_RENDER_SCALE=3\n"
    "#\n"
    "# Better Wind Waker's settings (docs/MODS.md): on with its defaults. Change them with\n"
    "# BLUEWAKE_OPTIONS, e.g. unrestricted_boat,-instant_text (none,... starts from all off):\n"
    "# instant_text swift_sail brisk_sail faster_movement faster_grapple faster_block_push\n"
    "# faster_animations tingle_chests unrestricted_boat no_song_replays swing_turn\n"
    "# skip_intro_movie faster_ballad invert_camera_x reveal_sea_chart\n"
    "BLUEWAKE_MODS=betterww\n"
    "# BLUEWAKE_OPTIONS=\n"
    "#\n"
    "# HD textures: a Dolphin-format pack folder (tex1_... files) for GZLE01.\n"
    "# DOL_AURORA_TEXTURE_PACK=/path/to/Load/Textures/GZLE01\n";

static void set_default(const char* name, const char* value) {
    setenv(name, value, 0);
}

static void join(char* out, size_t size, const char* a, const char* b) {
    snprintf(out, size, "%s/%s", a, b);
}

static void read_settings(const char* path) {
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        file = fopen(path, "w");
        if (file != NULL) {
            fputs(kDefaultSettings, file);
            fclose(file);
        }
        file = fopen(path, "r");
        if (file == NULL)
            return;
    }
    char line[1024];
    while (fgets(line, sizeof line, file) != NULL) {
        char* comment = strchr(line, '#');
        if (comment != NULL)
            *comment = '\0';
        char* end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';
        char* key = line;
        while (*key == ' ' || *key == '\t')
            ++key;
        char* equals = strchr(key, '=');
        if (equals == NULL || equals == key)
            continue;
        *equals = '\0';
        set_default(key, equals + 1);
    }
    fclose(file);
}

int main(int argc, char** argv) {
    (void)argc;
    char exe[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof exe;
    if (_NSGetExecutablePath(exe, &size) != 0 || realpath(exe, real) == NULL)
        return 1;
    // .../Wind Waker Recomp.app/Contents/MacOS/launcher -> .../Contents
    char contents[PATH_MAX];
    snprintf(contents, sizeof contents, "%s", real);
    for (int i = 0; i < 2; ++i) {
        char* slash = strrchr(contents, '/');
        if (slash == NULL)
            return 1;
        *slash = '\0';
    }

    const char* home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        return 1;
    char support[PATH_MAX], logs[PATH_MAX], path[PATH_MAX];
    snprintf(support, sizeof support, "%s/Library/Application Support/Wind Waker Recomp", home);
    mkdir(support, 0755);
    join(logs, sizeof logs, support, "logs");
    mkdir(logs, 0755);

    join(path, sizeof path, support, "settings.txt");
    read_settings(path);

    char resources[PATH_MAX];
    join(resources, sizeof resources, contents, "Resources");
    set_default("BLUEWAKE_ROOT", resources);
    join(path, sizeof path, resources, "game/main.dol");
    set_default("BLUEWAKE_DOL", path);
    join(path, sizeof path, resources, "game/rels");
    set_default("BLUEWAKE_RELS_DIR", path);
    join(path, sizeof path, resources, "game/GZLE01.iso");
    set_default("BLUEWAKE_DISC", path);
    join(path, sizeof path, resources, "dsp/dsp_rom.bin");
    set_default("BLUEWAKE_DSP_IROM", path);
    join(path, sizeof path, resources, "dsp/dsp_coef.bin");
    set_default("BLUEWAKE_DSP_COEF", path);
    join(path, sizeof path, support, "MemoryCardA.raw");
    set_default("BLUEWAKE_CARD_PATH", path);
    join(path, sizeof path, support, "sram.bin");
    set_default("BLUEWAKE_SRAM", path);
    set_default("BLUEWAKE_CLOCK", "now");
    set_default("BLUEWAKE_RENDERER", "aurora");
    set_default("BLUEWAKE_DSP_MODE", "hle");
    set_default("BLUEWAKE_WALL_PACE", "1");

    // One log per session, as the iOS app keeps them.
    char stamp[32];
    const time_t now = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
    snprintf(path, sizeof path, "%s/session-%s.log", logs, stamp);
    if (freopen(path, "w", stderr) != NULL)
        dup2(fileno(stderr), fileno(stdout));
    if (chdir(support) != 0)
        fprintf(stderr, "[app] chdir %s: %s\n", support, strerror(errno));

    char host[PATH_MAX], module[PATH_MAX];
    join(host, sizeof host, contents, "MacOS/bluewake_host");
    join(module, sizeof module, contents, "Frameworks/gGZLE01_recomp.dylib");
    char* host_argv[] = {host, module, NULL};
    execv(host, host_argv);
    fprintf(stderr, "[app] could not run %s: %s\n", host, strerror(errno));
    (void)argv;
    return 1;
}
