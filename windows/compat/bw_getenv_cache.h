/* getenv, remembered per call site, for GXRuntime's C runtime on Windows.
 *
 * The Windows C runtime's getenv takes a lock and scans the whole environment
 * block. GXRuntime reads trace switches on hot paths: ppc_take_exception checks
 * one on every guest exception, which was 4 percent of the game thread in a
 * profile at Outset, where macOS's getenv costs next to nothing.
 *
 * BlueWake sets its variables before the game starts, so each call site keeps
 * its first answer (copied, so a later change to the environment cannot leave
 * it dangling). That is only right where every call names a literal variable:
 * force-include this into GXRuntime's src/ and the game module's runtime
 * sources, never into code that passes a variable name.
 */
#ifndef BW_GETENV_CACHE_H
#define BW_GETENV_CACHE_H

#if defined(_WIN32)
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BW_GETENV_UNSET ((char*)(~(uintptr_t)0))
#define getenv(name)                                    \
    (__extension__({                                    \
        static char* volatile bw_getenv_value_ = BW_GETENV_UNSET; \
        char* bw_getenv_result_ = bw_getenv_value_;     \
        if (bw_getenv_result_ == BW_GETENV_UNSET) {     \
            bw_getenv_result_ = (getenv)(name);         \
            if (bw_getenv_result_ != NULL)              \
                bw_getenv_result_ = _strdup(bw_getenv_result_); \
            bw_getenv_value_ = bw_getenv_result_;       \
        }                                               \
        bw_getenv_result_;                              \
    }))
#endif

#endif /* BW_GETENV_CACHE_H */
