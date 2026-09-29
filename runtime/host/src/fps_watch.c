#include "fps_watch.h"

#include "fast_load.h"

#include "gxruntime/aurora_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Once a second of wall time, when fewer than kDipBelow frames reached the
// screen: what the game and the in-between frames did in that second, and
// where Link was, so the places where Smooth Motion does not hold 60 can be
// found and fixed. A scene change's fast-forward (nothing to see) is skipped.
enum {
    kCurStage = 0x803C9D3Cu,      // g_dComIfG_gameInfo.play.mCurStage (name[8], point, room, layer)
    kStayRoom = 0x803F6A78u,      // dStage_roomControl_c::mStayNo
    kPlayerPointer = 0x803CA74Cu, // dComIfGp_getPlayer(0)
    kPos = 0x1F8u,                // fopAc_ac_c::current.pos
    kEventMode = 0x803C9EA2u,     // g_dComIfG_gameInfo.play.mEvtCtrl's mode
};

static const double kDipBelow = 57.0;

static CPUState* g_cpu;
static bool g_enabled = true;
static DolAuroraFrameTiming g_last;
static unsigned long long g_last_wall_us, g_last_cpu_us, g_last_retrace;
static unsigned long long g_retrace;
static unsigned long long g_dips;

static unsigned long long now_us(clockid_t clock) {
    struct timespec ts;
    clock_gettime(clock, &ts);
    return (unsigned long long)ts.tv_sec * 1000000ull + (unsigned long long)ts.tv_nsec / 1000ull;
}

static float read_f32(CPUState* cpu, u32 address) {
    const u32 bits = mem_read32(cpu, address);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

void bluewake_fps_watch_attach(CPUState* cpu) {
    g_cpu = cpu;
    const char* on = getenv("BLUEWAKE_FPS_WATCH");
    g_enabled = on == NULL || on[0] != '0';
}

void bluewake_fps_watch_retrace(void) {
    ++g_retrace;
    if (!g_enabled || g_cpu == NULL)
        return;
    const unsigned long long wall = now_us(CLOCK_MONOTONIC);
    if (g_last_wall_us == 0u) {
        g_last_wall_us = wall;
        g_last_cpu_us = now_us(CLOCK_THREAD_CPUTIME_ID);
        g_last_retrace = g_retrace;
        dol_aurora_frame_timing(&g_last);
        return;
    }
    if (wall - g_last_wall_us < 1000000ull)
        return;
    DolAuroraFrameTiming now;
    dol_aurora_frame_timing(&now);
    const unsigned long long cpu_us = now_us(CLOCK_THREAD_CPUTIME_ID);
    const double seconds = (double)(wall - g_last_wall_us) / 1e6;
    const double shown = (double)(now.shown - g_last.shown) / seconds;
    const double speed = (double)(g_retrace - g_last_retrace) / seconds / 59.94;
    const unsigned long long game = now.presents - g_last.presents;
    const unsigned long long frames = now.interp_frames - g_last.interp_frames;
    const unsigned long long interpolated = now.interp_interpolated - g_last.interp_interpolated;
    const unsigned long long draws = now.interp_draws - g_last.interp_draws;
    const unsigned long long rejected = now.interp_rejected - g_last.interp_rejected;
    const unsigned long long unmatched = now.interp_unmatched - g_last.interp_unmatched;
    const double busy = 100.0 * (double)(cpu_us - g_last_cpu_us) / (double)(wall - g_last_wall_us);
    // Where the emulation thread waited, in milliseconds of this second: for
    // the GX translation worker at the game's draw barriers, in presents, and
    // the part of those in the GPU submission and waiting for a drawable.
    const double gx_ms = (double)(now.drain_us - g_last.drain_us) / 1000.0 / seconds;
    const double present_ms = (double)(now.present_us - g_last.present_us) / 1000.0 / seconds;
    const double gpu_ms = (double)(now.end_frame_us - g_last.end_frame_us) / 1000.0 / seconds;
    // Not a second with a scene change's fast-forward in it (the game ran
    // faster than real time), nor the title and file screens (no Link).
    const u32 link = mem_read32(g_cpu, kPlayerPointer);
    const bool skip = bluewake_fast_load_fast_forward() || now.shown == g_last.shown || speed > 1.05 ||
                      link < 0x80000000u || link >= 0x81800000u;
    if (!skip && shown < kDipBelow) {
        CPUState* cpu = g_cpu;
        char stage[9] = {0};
        for (u32 i = 0; i < 8u; ++i)
            stage[i] = (char)mem_read8(cpu, kCurStage + i);
        const u32 player = mem_read32(cpu, kPlayerPointer);
        float x = 0.f, y = 0.f, z = 0.f;
        if (player >= 0x80000000u && player < 0x81800000u) {
            x = read_f32(cpu, player + kPos);
            y = read_f32(cpu, player + kPos + 4u);
            z = read_f32(cpu, player + kPos + 8u);
        }
        // What held it under 60: the game itself below full speed (the
        // emulation or the GX worker), or game frames shown without an
        // in-between frame (Smooth Motion judged them a cut, or off).
        const char* reason = speed < 0.97                                ? "game below full speed"
                             : frames > 0u && interpolated * 10u < frames * 9u ? "frames not interpolated"
                                                                               : "presents late";
        ++g_dips;
        fprintf(stderr,
                "[fps-dip] retrace=%llu shown=%.1f game=%llu speed=%.0f%% interpolated=%llu/%llu "
                "draws/frame=%llu rejected=%.1f%% unmatched=%.1f%% busy=%.0f%% waits: gx=%.0fms present=%.0fms "
                "gpu=%.0fms stage=%s room=%d event=%u pos=%.0f,%.0f,%.0f reason=%s\n",
                g_retrace, shown, game, speed * 100.0, interpolated, frames, frames ? draws / frames : 0ull,
                draws ? 100.0 * (double)rejected / (double)draws : 0.0,
                draws ? 100.0 * (double)unmatched / (double)draws : 0.0, busy, gx_ms, present_ms, gpu_ms, stage,
                (int)(signed char)mem_read8(cpu, kStayRoom), mem_read8(cpu, kEventMode), x, y, z, reason);
    }
    g_last = now;
    g_last_wall_us = wall;
    g_last_cpu_us = cpu_us;
    g_last_retrace = g_retrace;
}
