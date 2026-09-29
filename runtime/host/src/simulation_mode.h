#ifndef BLUEWAKE_SIMULATION_MODE_H
#define BLUEWAKE_SIMULATION_MODE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// A restart-only option: the module must have matching, verified timing sites.
bool bluewake_simulation_init(void* module);
bool bluewake_simulation_supported(void);
bool bluewake_simulation_enabled(void);
void bluewake_simulation_renderer_ready(void);
void bluewake_simulation_retrace(uint64_t retrace);
#ifdef __cplusplus
}
#endif
#endif
