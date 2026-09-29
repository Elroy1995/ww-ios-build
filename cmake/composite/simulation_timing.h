#ifndef BLUEWAKE_SIMULATION_TIMING_H
#define BLUEWAKE_SIMULATION_TIMING_H

#include "core/cpu.h"
#include <stdint.h>

#define BLUEWAKE_SIMULATION_ABI 1u
typedef struct BluewakeSimulationStats {
    uint32_t abi, size, requested, active;
    uint64_t ticks, half_ticks, player_steps, collision_passes;
} BluewakeSimulationStats;
unsigned bluewake_composite_simulation_abi(void);
void bluewake_composite_simulation_enable(unsigned enabled);
int bluewake_composite_simulation_stats(BluewakeSimulationStats* out, unsigned size);

extern float bluewake_simulation_step;
extern unsigned bluewake_simulation_legacy_tick;
void bluewake_simulation_begin(CPUState* cpu);
void bluewake_simulation_note(unsigned kind);
void bluewake_simulation_player_move(CPUState* cpu);
void bluewake_simulation_player_gravity(double previous_y_velocity);
double bluewake_simulation_approach(double coefficient);
void bluewake_simulation_reset(void);

#endif
