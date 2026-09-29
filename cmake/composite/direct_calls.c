/* The state behind direct calls between chunks (direct_calls.h). */
#include "direct_calls.h"

#if defined(_WIN32)
#define BW_DIRECT_EXPORT __declspec(dllexport)
#else
#define BW_DIRECT_EXPORT __attribute__((visibility("default")))
#endif

/* Off until the host hands over its flags; these keep the ready test safe to
 * read before then. */
static const bool k_attention = true;
static const bool k_clear = false;
static const u32 k_zero = 0u;

unsigned bw_direct_depth;
bool bw_direct_enabled;
const bool* bw_host_sources_dirty = &k_attention;
const bool* bw_host_decrementer_pending = &k_clear;
const u32* bw_host_pi_cause = &k_zero;
const u32* bw_host_pi_mask = &k_zero;

/* The host's edge-service state, read before and after each direct call.
 * enabled false (or NULL flags) turns direct calls off: every call then goes
 * round the chassis loop. Returns 1 when direct calls are on. */
BW_DIRECT_EXPORT int bluewake_composite_direct_calls(bool enabled, const bool* sources_dirty,
                                                     const bool* decrementer_pending, const u32* pi_cause,
                                                     const u32* pi_mask) {
    if (!enabled || sources_dirty == NULL || decrementer_pending == NULL || pi_cause == NULL ||
        pi_mask == NULL) {
        bw_direct_enabled = false;
        return 0;
    }
    bw_host_sources_dirty = sources_dirty;
    bw_host_decrementer_pending = decrementer_pending;
    bw_host_pi_cause = pi_cause;
    bw_host_pi_mask = pi_mask;
    bw_direct_enabled = true;
    return 1;
}
