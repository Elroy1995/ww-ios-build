#ifndef BLUEWAKE_COMPOSITE_GATHER_PIPE_H
#define BLUEWAKE_COMPOSITE_GATHER_PIPE_H

/* Stores to the GX gather pipe, straight to the host's GX writer
 * (scripts/windows/gather_pipe.py includes this in every chunk, ahead of the
 * generated header, so the header's own paired-single stores use it too).
 *
 * The game writes the pipe (0xCC008000) a word at a time: every matrix, TEV
 * register and immediate vertex it sends. Translated, each store looked for
 * guest RAM, tried the alias resolver twice, then called the host's MMIO
 * handler through the CPU state, which recognised the pipe and called
 * dol_platform_gx_write. The host's handler does nothing else for these
 * addresses (runtime/host/src/main.c, host_mmio_write: "The gather pipe
 * first"), so when it hands over its writer the store calls that directly.
 * Until then, or with the FIFO trace on, stores take the old path.
 *
 * No identifier here may be `ctx`: the chunks define it as a macro. */

#include "core/cpu.h"

typedef void (*BwGatherPipeWrite)(u64 value, u8 size);
extern BwGatherPipeWrite bw_gather_pipe_write;

static inline bool bw_gather_pipe(u32 addr) {
    /* The mirror bit first: guest RAM stores, which never have it, pay one test. */
    return (addr & 0x40000000u) != 0u && (addr & ~0x1Fu) == 0xCC008000u && bw_gather_pipe_write != NULL;
}

static inline void bw_mem_write8(CPUState* cpu, u32 addr, u8 value) {
    if (__builtin_expect(bw_gather_pipe(addr), 0)) {
        bw_gather_pipe_write(value, 1);
        return;
    }
    mem_write8(cpu, addr, value);
}

static inline void bw_mem_write16(CPUState* cpu, u32 addr, u16 value) {
    if (__builtin_expect(bw_gather_pipe(addr), 0)) {
        bw_gather_pipe_write(value, 2);
        return;
    }
    mem_write16(cpu, addr, value);
}

static inline void bw_mem_write32(CPUState* cpu, u32 addr, u32 value) {
    if (__builtin_expect(bw_gather_pipe(addr), 0)) {
        bw_gather_pipe_write(value, 4);
        return;
    }
    mem_write32(cpu, addr, value);
}

static inline void bw_mem_write64(CPUState* cpu, u32 addr, u64 value) {
    if (__builtin_expect(bw_gather_pipe(addr), 0)) {
        bw_gather_pipe_write(value, 8);
        return;
    }
    mem_write64(cpu, addr, value);
}

#define mem_write8 bw_mem_write8
#define mem_write16 bw_mem_write16
#define mem_write32 bw_mem_write32
#define mem_write64 bw_mem_write64

#endif
