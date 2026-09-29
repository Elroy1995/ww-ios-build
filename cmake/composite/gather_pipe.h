#ifndef BLUEWAKE_COMPOSITE_GATHER_PIPE_H
#define BLUEWAKE_COMPOSITE_GATHER_PIPE_H

/* Stores to the GX gather pipe, straight to the host's GX writer
 * (scripts/windows/chunk_headers.py includes this in every chunk, ahead of the
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
 * When the host also hands over a writer for runs of bytes, the words collect
 * in a batch instead (gather_pipe_batch.h). Everything that could observe the
 * pipe's progress hands the batch over first: any other access to the
 * hardware (0xC8000000 up: the EFB and the registers), whether made here or
 * by the interpreter for an instruction or a quantised paired single, and
 * every boundary where the chassis loop asks the host's edge service or
 * returns to the host (dispatch_loop.h). The host sees the same bytes in the
 * same order before anything that depends on them.
 *
 * No identifier here may be `ctx`: the chunks define it as a macro. */

#include "core/cpu.h"
#include "gather_pipe_batch.h"

typedef void (*BwGatherPipeWrite)(u64 value, u8 size);
extern BwGatherPipeWrite bw_gather_pipe_write;

static inline bool bw_gather_pipe(u32 addr) {
    /* The mirror bit first: guest RAM stores, which never have it, pay one test. */
    return (addr & 0x40000000u) != 0u && (addr & ~0x1Fu) == 0xCC008000u && bw_gather_pipe_write != NULL;
}

/* The EFB and the hardware registers: an access there may see how far the
 * GPU has read the pipe. The mirror bit first, the test the RAM fast path
 * makes anyway, so plain RAM accesses share its branch and pay nothing more. */
static inline bool bw_hardware(u32 addr) {
    return (addr & 0x40000000u) != 0u && (addr & 0xF8000000u) == 0xC8000000u;
}

static inline void bw_gather_pipe_put(u64 value, u8 size) {
    if (bw_gather_pipe_bytes == NULL) {
        bw_gather_pipe_write(value, size);
        return;
    }
    u8* const out = bw_gather_pipe_buffer + bw_gather_pipe_length;
    switch (size) {
    case 1:
        out[0] = (u8)value;
        break;
    case 2: {
        const u16 word = __builtin_bswap16((u16)value);
        memcpy(out, &word, 2);
        break;
    }
    case 4: {
        const u32 word = __builtin_bswap32((u32)value);
        memcpy(out, &word, 4);
        break;
    }
    default: {
        const u64 word = __builtin_bswap64(value);
        memcpy(out, &word, 8);
        break;
    }
    }
    bw_gather_pipe_length += size;
    if (bw_gather_pipe_length >= BW_GATHER_PIPE_BATCH)
        bw_gather_pipe_flush();
}

static inline void bw_mem_write8(CPUState* cpu, u32 addr, u8 value) {
    if (__builtin_expect(bw_hardware(addr), 0)) {
        if (bw_gather_pipe(addr)) {
            bw_gather_pipe_put(value, 1);
            return;
        }
        bw_gather_pipe_drain();
    }
    mem_write8(cpu, addr, value);
}

static inline void bw_mem_write16(CPUState* cpu, u32 addr, u16 value) {
    if (__builtin_expect(bw_hardware(addr), 0)) {
        if (bw_gather_pipe(addr)) {
            bw_gather_pipe_put(value, 2);
            return;
        }
        bw_gather_pipe_drain();
    }
    mem_write16(cpu, addr, value);
}

static inline void bw_mem_write32(CPUState* cpu, u32 addr, u32 value) {
    if (__builtin_expect(bw_hardware(addr), 0)) {
        if (bw_gather_pipe(addr)) {
            bw_gather_pipe_put(value, 4);
            return;
        }
        bw_gather_pipe_drain();
    }
    mem_write32(cpu, addr, value);
}

static inline void bw_mem_write64(CPUState* cpu, u32 addr, u64 value) {
    if (__builtin_expect(bw_hardware(addr), 0)) {
        if (bw_gather_pipe(addr)) {
            bw_gather_pipe_put(value, 8);
            return;
        }
        bw_gather_pipe_drain();
    }
    mem_write64(cpu, addr, value);
}

static inline u8 bw_mem_read8(CPUState* cpu, u32 addr) {
    if (__builtin_expect(bw_hardware(addr), 0))
        bw_gather_pipe_drain();
    return mem_read8(cpu, addr);
}

static inline u16 bw_mem_read16(CPUState* cpu, u32 addr) {
    if (__builtin_expect(bw_hardware(addr), 0))
        bw_gather_pipe_drain();
    return mem_read16(cpu, addr);
}

static inline u32 bw_mem_read32(CPUState* cpu, u32 addr) {
    if (__builtin_expect(bw_hardware(addr), 0))
        bw_gather_pipe_drain();
    return mem_read32(cpu, addr);
}

static inline u64 bw_mem_read64(CPUState* cpu, u32 addr) {
    if (__builtin_expect(bw_hardware(addr), 0))
        bw_gather_pipe_drain();
    return mem_read64(cpu, addr);
}

/* What the interpreter runs for the chunks makes its own accesses, outside
 * these wrappers: an instruction the translation hands it, the quantised
 * paired-single types (the generated inline forms take only type 0) and the
 * locked-cache zero. Quantised loads and stores are common in vertex and
 * skinning code, so they hand the batch over only on the way to the hardware. */
static inline void bw_fallback_instruction(CPUState* cpu, u32 raw, u32 cia) {
    bw_gather_pipe_drain();
    ppc_fallback_instruction(cpu, raw, cia);
}

static inline bool bw_psq_load(CPUState* cpu, u8 frD, u32 ea, bool w, u8 gqr, bool indexed, u32 cia) {
    if (__builtin_expect(bw_hardware(ea), 0))
        bw_gather_pipe_drain();
    return ppc_psq_load(cpu, frD, ea, w, gqr, indexed, cia);
}

static inline bool bw_psq_store(CPUState* cpu, u8 frS, u32 ea, bool w, u8 gqr, bool indexed, u32 cia) {
    if (__builtin_expect(bw_hardware(ea), 0))
        bw_gather_pipe_drain();
    return ppc_psq_store(cpu, frS, ea, w, gqr, indexed, cia);
}

static inline void bw_dcbz_l(CPUState* cpu, u32 ea, u32 cia) {
    bw_gather_pipe_drain();
    ppc_dcbz_l(cpu, ea, cia);
}

#define ppc_fallback_instruction bw_fallback_instruction
#define ppc_psq_load bw_psq_load
#define ppc_psq_store bw_psq_store
#define ppc_dcbz_l bw_dcbz_l
#define mem_write8 bw_mem_write8
#define mem_write16 bw_mem_write16
#define mem_write32 bw_mem_write32
#define mem_write64 bw_mem_write64
#define mem_read8 bw_mem_read8
#define mem_read16 bw_mem_read16
#define mem_read32 bw_mem_read32
#define mem_read64 bw_mem_read64

#endif
