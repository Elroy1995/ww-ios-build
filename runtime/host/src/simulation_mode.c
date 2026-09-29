#include "simulation_mode.h"
#include "../../../cmake/composite/simulation_timing.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void aurora_set_frame_interpolation(bool enabled);
static bool s_supported, s_enabled, s_log;
static int (*s_get_stats)(BluewakeSimulationStats*, unsigned);
static BluewakeSimulationStats s_last;

bool bluewake_simulation_init(void* module) {
    unsigned (*abi)(void) = dlsym(module, "bluewake_composite_simulation_abi");
    void (*enable)(unsigned) = dlsym(module, "bluewake_composite_simulation_enable");
    s_get_stats = dlsym(module, "bluewake_composite_simulation_stats");
    s_supported = abi && enable && s_get_stats && abi() == BLUEWAKE_SIMULATION_ABI;
    const char* option = getenv("BLUEWAKE_SIMULATION_60HZ");
    if (option && *option && strcmp(option, "0") && strcmp(option, "1")) {
        fprintf(stderr, "[simulation] BLUEWAKE_SIMULATION_60HZ must be 0 or 1\n");
        return false;
    }
    s_enabled = option && !strcmp(option, "1");
    s_log = getenv("BLUEWAKE_SIMULATION_LOG") != NULL;
    if (s_enabled && !s_supported) {
        fprintf(stderr, "[simulation] 60 Hz requires a rebuilt personal game module. Run scripts/mods/prepare_simulation_60hz.py on its generated sources, then rebuild.\n");
        return false;
    }
    if (s_supported) enable(s_enabled);
    fprintf(stderr, "[simulation] %s; module support=%s\n", s_enabled ?
        "experimental 60 Hz gameplay (authored scenes remain 30 Hz)" : "original 30 Hz", s_supported ? "yes" : "no");
    return true;
}

bool bluewake_simulation_supported(void) { return s_supported; }
bool bluewake_simulation_enabled(void) { return s_enabled; }
void bluewake_simulation_renderer_ready(void) {
    // Preserve the saved Smooth Motion preference for the next 30 Hz launch.
    if (s_enabled) aurora_set_frame_interpolation(false);
}

void bluewake_simulation_retrace(uint64_t retrace) {
    if (!s_supported || !s_log || retrace % 60) return;
    BluewakeSimulationStats now;
    if (!s_get_stats(&now, sizeof now)) return;
    fprintf(stderr, "[simulation] retrace=%llu active=%u ticks=%llu half=%llu player=%llu scene=%llu\n",
        (unsigned long long)retrace, now.active,
        (unsigned long long)(now.ticks - s_last.ticks),
        (unsigned long long)(now.half_ticks - s_last.half_ticks),
        (unsigned long long)(now.player_steps - s_last.player_steps),
        (unsigned long long)(now.collision_passes - s_last.collision_passes));
    s_last = now;
}
