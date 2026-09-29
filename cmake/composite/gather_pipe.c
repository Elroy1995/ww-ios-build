/* The host's GX writer for the chunks' gather-pipe stores (gather_pipe.h). */
#include "gather_pipe.h"

#if defined(_WIN32)
#define BW_GATHER_PIPE_EXPORT __declspec(dllexport)
#else
#define BW_GATHER_PIPE_EXPORT __attribute__((visibility("default")))
#endif

BwGatherPipeWrite bw_gather_pipe_write;

/* The function the host's MMIO handler calls for a pipe store, or NULL to
 * send pipe stores through that handler again. */
BW_GATHER_PIPE_EXPORT void bluewake_composite_set_gather_pipe(BwGatherPipeWrite write) {
    bw_gather_pipe_write = write;
}
