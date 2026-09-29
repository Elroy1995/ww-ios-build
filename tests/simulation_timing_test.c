#include "simulation_timing.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static float read_f(CPUState* cpu, u32 p) {
    u32 bits = mem_read32(cpu, p); float f; memcpy(&f, &bits, 4); return f;
}
static void write_f(CPUState* cpu, u32 p, float f) {
    u32 bits; memcpy(&bits, &f, 4); mem_write32(cpu, p, bits);
}

int main(void) {
    CPUState cpu = {0};
    cpu.ram_size = GC_MAIN_RAM_SIZE;
    cpu.ram = calloc(1, cpu.ram_size);
    assert(cpu.ram);
    const u32 player = 0x80500000, display = 0x80510000, fader = 0x80520000;
    mem_write32(&cpu, 0x803CA74C, player);
    mem_write32(&cpu, 0x803F7390, display);
    mem_write32(&cpu, 0x803F6898, fader);
    mem_write32(&cpu, display + 0x24, 1350000);
    mem_write32(&cpu, fader + 4, 1);

    // A default launch leaves the original time units and movement unchanged.
    bluewake_composite_simulation_enable(0);
    bluewake_simulation_begin(&cpu);
    assert(bluewake_simulation_step == 1 && bluewake_simulation_legacy_tick == 1);
    assert(bluewake_simulation_approach(0.25) == 0.25);

    // Equal elapsed time at a constant velocity covers the same distance;
    // each of the 60 ticks still integrates, including the timer-off half.
    cpu.gpr[3] = player + 0x1F8; cpu.gpr[4] = player + 0x220; cpu.gpr[5] = cpu.gpr[3];
    write_f(&cpu, cpu.gpr[4], 12); write_f(&cpu, cpu.gpr[4] + 4, -3);
    write_f(&cpu, cpu.gpr[4] + 8, 8);
    bluewake_composite_simulation_enable(1);
    unsigned original_timer_ticks = 0;
    for (unsigned i = 0; i < 60; ++i) {
        bluewake_simulation_begin(&cpu);
        assert(bluewake_simulation_step == 0.5f);
        original_timer_ticks += bluewake_simulation_legacy_tick;
        bluewake_simulation_player_move(&cpu);
        assert(read_f(&cpu, cpu.gpr[3]) == 6 * (i + 1));
    }
    assert(original_timer_ticks == 30);
    assert(read_f(&cpu, cpu.gpr[3]) == 360);
    assert(read_f(&cpu, cpu.gpr[3] + 4) == -90);
    assert(read_f(&cpu, cpu.gpr[3] + 8) == 240);
    double half = bluewake_simulation_approach(0.25);
    assert(fabs((1 - half) * (1 - half) - 0.75) < 1e-12);
    assert(bluewake_simulation_approach(2) == 2);

    // The 60 Hz jump must retain the original 30 Hz apex/trajectory, rather
    // than gaining height because Euler integration uses a smaller step.
    float original_y = 0, original_v = 12.5f;
    write_f(&cpu, cpu.gpr[3] + 4, 0);
    write_f(&cpu, cpu.gpr[4] + 4, original_v);
    for (unsigned i = 0; i < 20; ++i) {
        original_v -= 2.5f; original_y += original_v;
        for (unsigned j = 0; j < 2; ++j) {
            bluewake_simulation_begin(&cpu);
            float old_v = read_f(&cpu, cpu.gpr[4] + 4);
            bluewake_simulation_player_gravity(old_v);
            write_f(&cpu, cpu.gpr[4] + 4, old_v - 1.25f);
            bluewake_simulation_player_move(&cpu);
        }
        assert(read_f(&cpu, cpu.gpr[3] + 4) == original_y);
        assert(read_f(&cpu, cpu.gpr[4] + 4) == original_v);
    }

    // Each gameplay exclusion must restore original timing immediately.
    const u32 byte_gates[] = {0x803C9EA2, 0x803F7097};
    for (unsigned i = 0; i < 2; ++i) {
        mem_write8(&cpu, byte_gates[i], 1);
        bluewake_simulation_begin(&cpu);
        assert(bluewake_simulation_step == 1 && bluewake_simulation_legacy_tick == 1);
        mem_write8(&cpu, byte_gates[i], 0);
        bluewake_simulation_begin(&cpu);
        assert(bluewake_simulation_step == 0.5f);
    }
    const u32 word_gates[] = {0x803F6160, fader + 4, display + 0x24, 0x803CA74C};
    const u32 bad[] = {0x80530000, 3, 0, 0};
    for (unsigned i = 0; i < 4; ++i) {
        u32 old = mem_read32(&cpu, word_gates[i]);
        mem_write32(&cpu, word_gates[i], bad[i]);
        bluewake_simulation_begin(&cpu);
        assert(bluewake_simulation_step == 1);
        mem_write32(&cpu, word_gates[i], old);
        bluewake_simulation_begin(&cpu);
        assert(bluewake_simulation_step == 0.5f);
    }
    mem_write16(&cpu, player + 0x304, 1);
    bluewake_simulation_begin(&cpu);
    assert(bluewake_simulation_step == 1);
    mem_write16(&cpu, player + 0x304, 0);
    bluewake_simulation_begin(&cpu);
    assert(bluewake_simulation_step == 0.5f);

    // Save-state reload must not carry the previous frame's half-step into
    // the restored scene; the next main-loop gate decides afresh.
    bluewake_simulation_reset();
    assert(bluewake_simulation_step == 1 && bluewake_simulation_legacy_tick == 1);
    BluewakeSimulationStats stats;
    assert(!bluewake_composite_simulation_stats(&stats, sizeof stats - 1));
    assert(bluewake_composite_simulation_stats(&stats, sizeof stats));
    assert(stats.requested && !stats.active && stats.half_ticks >= 60);
    bluewake_composite_simulation_enable(0);
    bluewake_simulation_begin(&cpu);
    assert(bluewake_simulation_step == 1);
    free(cpu.ram);
    puts("simulation timing: defaults, 60 integrations, 30 timer ticks, scene gates and reset passed");
}
