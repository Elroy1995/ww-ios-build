// Timing adapters for a personal, instrumented GZLE01 module. The ordinary
// mode has step 1 and executes the original instructions unchanged.
#include "simulation_timing.h"
#include <math.h>
#include <string.h>

#if defined(__GNUC__)
#define BW_EXPORT __attribute__((visibility("default")))
#else
#define BW_EXPORT
#endif

float bluewake_simulation_step = 1.0f;
unsigned bluewake_simulation_legacy_tick = 1;
static BluewakeSimulationStats s_stats = {BLUEWAKE_SIMULATION_ABI, sizeof(BluewakeSimulationStats)};
static unsigned s_phase;
static float s_player_previous_y;
static unsigned s_player_gravity_ready;

static int pointer_ok(uint32_t p, uint32_t size) {
    return p >= 0x80000000u && p <= 0x81800000u - size;
}

BW_EXPORT unsigned bluewake_composite_simulation_abi(void) { return BLUEWAKE_SIMULATION_ABI; }

void bluewake_simulation_reset(void) {
    bluewake_simulation_step = 1.0f;
    bluewake_simulation_legacy_tick = 1;
    s_phase = 0;
    s_stats.active = 0;
    s_player_gravity_ready = 0;
}

BW_EXPORT void bluewake_composite_simulation_enable(unsigned enabled) {
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.abi = BLUEWAKE_SIMULATION_ABI;
    s_stats.size = sizeof(s_stats);
    s_stats.requested = enabled == 1;
    bluewake_simulation_reset();
}

BW_EXPORT int bluewake_composite_simulation_stats(BluewakeSimulationStats* out, unsigned size) {
    if (out == NULL || size != sizeof(s_stats)) return 0;
    *out = s_stats;
    return 1;
}

void bluewake_simulation_begin(CPUState* cpu) {
    s_player_gravity_ready = 0;
    ++s_stats.ticks;
    if (!s_stats.requested) return;
    // Keep authored scenes, dialogues, menus and transitions on their original
    // clock until their timing has been converted. This is an experimental
    // gameplay mode, not a claim of full-game 60 Hz coverage.
    const uint32_t player = mem_read32(cpu, 0x803CA74Cu);
    const uint32_t display = mem_read32(cpu, 0x803F7390u);
    const uint32_t fader = mem_read32(cpu, 0x803F6898u);
    const int active = pointer_ok(player, 0x3620u) && pointer_ok(display, 0x28u) &&
        pointer_ok(fader, 0x10u) && mem_read32(cpu, display + 0x24u) == 1350000u &&
        mem_read16(cpu, player + 0x304u) == 0 && mem_read8(cpu, 0x803C9EA2u) == 0 &&
        mem_read8(cpu, 0x803F7097u) == 0 && mem_read32(cpu, 0x803F6160u) == 0 &&
        mem_read32(cpu, fader + 4u) == 1u;
    if (!active) {
        bluewake_simulation_reset();
        return;
    }
    if (!s_stats.active) s_phase = 0;
    s_stats.active = 1;
    bluewake_simulation_step = 0.5f;
    bluewake_simulation_legacy_tick = s_phase;
    s_phase ^= 1u;
    ++s_stats.half_ticks;
}

void bluewake_simulation_note(unsigned kind) {
    if (kind == 1) ++s_stats.player_steps;
    if (kind == 2) ++s_stats.collision_passes;
}

static float read_float(CPUState* cpu, uint32_t address) {
    uint32_t bits = mem_read32(cpu, address);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void bluewake_simulation_player_gravity(double previous_y_velocity) {
    s_player_previous_y = (float)previous_y_velocity;
    s_player_gravity_ready = 1;
}

void bluewake_simulation_player_move(CPUState* cpu) {
    // The call at 8010950C adds Link's velocity to his position. Collision
    // corrections and animation-root displacements use other calls and retain
    // their own units. This only changes the integration of his velocity.
    const uint32_t dst = cpu->gpr[5], pos = cpu->gpr[3], vel = cpu->gpr[4];
    float result[3];
    for (unsigned i = 0; i < 3; ++i)
        result[i] = read_float(cpu, pos + 4*i) +
                    read_float(cpu, vel + 4*i) * bluewake_simulation_step;
    // Preserve the authored semi-implicit 30 Hz trajectory. Two uncorrected
    // half-steps otherwise increase a standing jump's apex by about 12%.
    // Use the actual velocity change so a terminal-speed clamp adds no force.
    if (s_player_gravity_ready && bluewake_simulation_step == 0.5f)
        result[1] += 0.25f * (read_float(cpu, vel + 4) - s_player_previous_y);
    s_player_gravity_ready = 0;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t bits;
        memcpy(&bits, &result[i], sizeof(bits));
        mem_write32(cpu, dst + 4*i, bits);
    }
}

double bluewake_simulation_approach(double a) {
    // Preserve exponential convergence in real time; callers separately scale
    // speed limits. Coefficients outside [0,1] keep their original behavior.
    return bluewake_simulation_step == 0.5f && a >= 0.0 && a <= 1.0 ?
        1.0 - sqrt(1.0 - a) : a;
}
