// BlueWake iOS/iPadOS entry shim.
//
// SDL owns main() on iOS (SDL_main.h renames ours to SDL_main and starts
// UIApplicationMain first). This file only fills in default paths inside the
// app container, then runs the unchanged host.
//
// Data layout, all user-provided and never bundled:
//   Documents/BlueWake/GZLE01.iso              the user's disc image
//   Documents/BlueWake/main.dol, rels/         prepared from that disc on the
//                                              device (first_run.m)
//   Documents/BlueWake/GZLE01.card             the memory card (saves)
//   Frameworks/gGZLE01_recomp.dylib            the translated composite, built
//                                              on a Mac from the same disc
//                                              (Documents/BlueWake in the
//                                              simulator)
//   Documents/BlueWake/dsp_rom.bin, dsp_coef.bin  only for BLUEWAKE_DSP_MODE=lle
// When any required piece is missing, the first-run screen says which and
// imports the disc.
// In the simulator dev loop, BLUEWAKE_ROOT (passed as SIMCTL_CHILD_BLUEWAKE_ROOT)
// points at the repository instead and the host resolves its usual layout.
#import <Foundation/Foundation.h>
#include <SDL3/SDL_main.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "first_run.h"
#include "controller_settings.h"
#include "touch_controls.h"

int bluewake_host_main(int argc, char** argv);

static void bw_default(const char* name, NSString* value) {
    const char* existing = getenv(name);
    if (existing != NULL && existing[0] != '\0') return;
    setenv(name, value.fileSystemRepresentation, 1);
}

static void bw_default_if_exists(const char* name, NSString* path) {
    if ([[NSFileManager defaultManager] fileExistsAtPath:path])
        bw_default(name, path);
}

// Session log. Everything the app writes to stdout and stderr also goes to
// Documents/BlueWake/logs/session-YYYYMMDD-HHMMSS.log, each line stamped with
// the local wall-clock time, so a session can be read back from the device
// (Finder, Files, or afcclient) without a console attached, and a moment the
// player remembers ("it lagged around 3:42") can be found in it. The original
// stderr still receives every line, so devicectl --console keeps working. The
// newest eight sessions are kept.
static int g_log_console_fd = -1;
static FILE* g_log_file = NULL;

static void* bw_log_pump(void* arg) {
    const int read_fd = (int)(intptr_t)arg;
    char buffer[16384];
    char line[8192];
    size_t line_len = 0;
    for (;;) {
        const ssize_t n = read(read_fd, buffer, sizeof buffer);
        if (n <= 0) break;
        if (g_log_console_fd >= 0) (void)write(g_log_console_fd, buffer, (size_t)n);
        for (ssize_t i = 0; i < n; i++) {
            if (line_len < sizeof line - 1) line[line_len++] = buffer[i];
            if (buffer[i] != '\n') continue;
            struct timeval tv;
            gettimeofday(&tv, NULL);
            struct tm local;
            localtime_r(&tv.tv_sec, &local);
            fprintf(g_log_file, "%02d:%02d:%02d.%03d ", local.tm_hour, local.tm_min,
                    local.tm_sec, (int)(tv.tv_usec / 1000));
            fwrite(line, 1, line_len, g_log_file);
            line_len = 0;
        }
        fflush(g_log_file);
    }
    return NULL;
}

static void bw_start_session_log(NSString* data) {
    NSString* dir = [data stringByAppendingPathComponent:@"logs"];
    NSFileManager* fm = [NSFileManager defaultManager];
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSArray<NSString*>* old = [[[fm contentsOfDirectoryAtPath:dir error:nil]
        filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF BEGINSWITH 'session-'"]]
        sortedArrayUsingSelector:@selector(compare:)];
    for (NSUInteger i = 0; i + 7 < old.count; i++)
        [fm removeItemAtPath:[dir stringByAppendingPathComponent:old[i]] error:nil];
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char name[64];
    strftime(name, sizeof name, "session-%Y%m%d-%H%M%S.log", &local);
    NSString* path = [dir stringByAppendingPathComponent:@(name)];
    g_log_file = fopen(path.fileSystemRepresentation, "w");
    int fds[2];
    if (g_log_file == NULL || pipe(fds) != 0) return;
    g_log_console_fd = dup(STDERR_FILENO);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    pthread_t thread;
    pthread_create(&thread, NULL, bw_log_pump, (void*)(intptr_t)fds[0]);
    pthread_detach(thread);
}

int main(int argc, char** argv) {
    @autoreleasepool {
        // Logs go to files under simctl/devicectl; keep them readable live.
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IOLBF, 0);
        NSString* docs = [NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
        NSString* data = [docs stringByAppendingPathComponent:@"BlueWake"];
        [[NSFileManager defaultManager] createDirectoryAtPath:data
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        if (getenv("BLUEWAKE_SESSION_LOG") == NULL ||
            strcmp(getenv("BLUEWAKE_SESSION_LOG"), "0") != 0)
            bw_start_session_log(data);
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IOLBF, 0);
        // One line a second: speed, the worst frame gap, dropped frames and
        // how busy the game thread was (runtime/host/src/main.c). And the
        // player's inputs next to the moments the game reads them.
        bw_default("BLUEWAKE_PERF_LOG", @"1");
        bw_default("BLUEWAKE_INPUT_LOG", @"1");
        // Pace retraces by the wall clock (runtime/host/src/main.c
        // host_wall_pace) instead of by the audio queue alone.
        bw_default("BLUEWAKE_WALL_PACE", @"1");
        bw_default("DOL_AUDIO_NO_THROTTLE", @"1");

        bw_default("BLUEWAKE_RENDERER", @"aurora");
        bw_default("BLUEWAKE_CYCLE_CAP", @"16384");
        bw_default("BLUEWAKE_MAX_BLOCKS", @"100000000000");
        // Dolphin's high-level Zelda ucode: about 15% fewer play-window cycles
        // than the LLE interpreter, with an audio envelope that matches it at
        // r=0.999 (docs/status/CURRENT.md). BLUEWAKE_DSP_MODE=lle restores LLE.
        bw_default("BLUEWAKE_DSP_MODE", @"hle");
        // The console's SRAM (ipl_sram.h): without it the game read an empty
        // SRAM, took mono from OSGetSoundMode and played mono. Dolphin's
        // defaults (stereo), kept in the data folder so the game's own
        // Stereo/Mono option persists. BLUEWAKE_SRAM=0 turns it off.
        bw_default("BLUEWAKE_SRAM", [data stringByAppendingPathComponent:@"sram.bin"]);
        // The console clock: saves carry the real local date and time (the
        // file select shows it) instead of 01/01/2000.
        bw_default("BLUEWAKE_CLOCK", @"now");
        bw_default("BLUEWAKE_SIMULATION_60HZ",
                   [[NSUserDefaults standardUserDefaults] boolForKey:@BW_SIMULATION_60HZ_KEY] ? @"1" : @"0");
        // The shell's aspect choice (BWGameOverlay.mm) applies at launch:
        // 0 keeps the original 4:3 picture, 1 fills the screen.
        if ([[NSUserDefaults standardUserDefaults] integerForKey:@"BlueWake.AspectMode"] == 1)
            bw_default("DOL_AURORA_ASPECT_FIT", @"0");
        // Mods (the Mods menu in BWGameOverlay.mm), applied at launch. The
        // widescreen code renders anamorphic 16:9 (or 16:10), so the picture
        // is letterboxed to that shape whatever the aspect setting.
        {
            NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
            NSMutableArray<NSString*>* mods = [NSMutableArray array];
            if ([d boolForKey:@"BlueWake.Mod.Widescreen1610"]) {
                [mods addObject:@"widescreen1610"];
                bw_default("DOL_AURORA_ASPECT_RATIO", @"1.6");
            } else if ([d boolForKey:@"BlueWake.Mod.Widescreen"]) {
                [mods addObject:@"widescreen"];
                bw_default("DOL_AURORA_ASPECT_RATIO", @"1.7778");
            }
            // HD textures: Dolphin-format packs go in Documents/BlueWake/Load/
            // Textures/GZLE01 (Dolphin's own layout), which the Files app shows.
            NSString* pack = [data stringByAppendingPathComponent:@"Load/Textures/GZLE01"];
            [[NSFileManager defaultManager] createDirectoryAtPath:pack withIntermediateDirectories:YES
                                                       attributes:nil error:nil];
            if ([d boolForKey:@"BlueWake.Mod.HDTextures"])
                bw_default("DOL_AURORA_TEXTURE_PACK", pack);
            // Better Wind Waker's settings (game options: mods/betterww/
            // options.txt, runtime/host/src/game_options.c): the mod carries
            // their code, and each setting the player changed from its default
            // in Mods > Better Wind Waker Settings is passed as name or -name.
            if ([d boolForKey:@"BlueWake.Mod.BetterWW"]) {
                [mods addObject:@"betterww"];
                NSMutableArray<NSString*>* options = [NSMutableArray array];
                for (NSString* key in [[d dictionaryRepresentation] allKeys]) {
                    if (![key hasPrefix:@BW_OPTION_KEY_PREFIX])
                        continue;
                    NSString* name = [key substringFromIndex:strlen(BW_OPTION_KEY_PREFIX)];
                    [options addObject:[d boolForKey:key] ? name : [@"-" stringByAppendingString:name]];
                }
                if (options.count > 0)
                    bw_default("BLUEWAKE_OPTIONS", [options componentsJoinedByString:@","]);
            }
            if (mods.count > 0)
                bw_default("BLUEWAKE_MODS", [mods componentsJoinedByString:@","]);
        }

        const char* root = getenv("BLUEWAKE_ROOT");
        NSString* composite = nil;
        const char* composite_env = getenv("BLUEWAKE_COMPOSITE");
        if (composite_env != NULL && composite_env[0] != '\0') {
            composite = [NSString stringWithUTF8String:composite_env];
        }
        if (root == NULL || root[0] == '\0') {
            if (composite == nil) {
                NSString* inData =
                    [data stringByAppendingPathComponent:@"gGZLE01_recomp.dylib"];
                NSString* inBundle = [[[NSBundle mainBundle] privateFrameworksPath]
                    stringByAppendingPathComponent:@"gGZLE01_recomp.dylib"];
                composite = [[NSFileManager defaultManager] fileExistsAtPath:inBundle]
                                ? inBundle : inData;
            }
            if (bluewake_first_run_needed(data.fileSystemRepresentation,
                                          composite.fileSystemRepresentation))
                bluewake_first_run_present(data.fileSystemRepresentation,
                                           composite.fileSystemRepresentation);
            bw_default_if_exists("BLUEWAKE_DOL",
                [data stringByAppendingPathComponent:@"main.dol"]);
            bw_default_if_exists("BLUEWAKE_RELS_DIR",
                [data stringByAppendingPathComponent:@"rels"]);
            bw_default_if_exists("BLUEWAKE_DISC",
                [data stringByAppendingPathComponent:@"GZLE01.iso"]);
            bw_default_if_exists("BLUEWAKE_DSP_IROM",
                [data stringByAppendingPathComponent:@"dsp_rom.bin"]);
            bw_default_if_exists("BLUEWAKE_DSP_COEF",
                [data stringByAppendingPathComponent:@"dsp_coef.bin"]);
            // Saves live beside the other data so Files and Finder show them.
            bw_default("BLUEWAKE_CARD_PATH",
                [data stringByAppendingPathComponent:@"GZLE01.card"]);
        }

        fprintf(stderr, "[ios] data=%s root=%s composite=%s\n",
                data.fileSystemRepresentation, root ? root : "(container)",
                composite ? composite.fileSystemRepresentation : "(host default)");

        char* host_argv[3] = {argv[0], NULL, NULL};
        int host_argc = 1;
        if (composite != nil) {
            host_argv[1] = strdup(composite.fileSystemRepresentation);
            host_argc = 2;
        }
        // Drawn in the game's own frame through Aurora's overlay hook.
        bluewake_touch_controls_install();
        const int status = bluewake_host_main(host_argc, host_argv);
        // Returning from SDL's main leaves UIKit running with no game. The
        // host only returns at a bounded stop (BLUEWAKE_MAX_RETRACES), a quit
        // or a fatal error, and in each case the process should end.
        fflush(stdout);
        fflush(stderr);
        exit(status);
    }
}
