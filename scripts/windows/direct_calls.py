#!/usr/bin/env python3
"""Call across translated chunks directly (cmake/composite/direct_calls.h).

  direct_calls.py COMPOSITE_SRC

A bl whose target is in another chunk is translated as "set lr and pc, leave
the chunk": the chassis loop then dispatches the target, and the callee's blr
leaves its chunk the same way to come back. Such calls and their returns are
about half of all block boundaries in play (the boundary census, over Outset).
At each one this adds a direct path: when bw_direct_call_ready says the loop
and the host's edge service would have nothing to do, the callee's chunk is
called through the chunk table, and if control comes back to the return
address with nothing to do there either, the caller carries on at its block
there. Otherwise the chunk leaves as before, with ctx->pc where the guest is.

Calls into the main executable's code are rewritten, including the REL
modules' calls to it through its 0xC0 mirror (which the dispatcher otherwise
resolves on its slow path), and calls between a REL module's chunks. None
whose target or return address the host names (runtime/host/src,
windows/src, either mirror form) is: the host hooks those addresses in its
edge service, which a direct call does not consult. Calls to the register save and restore routines are already inline
(scripts/windows/inline_save_restore_gpr.py) and keep that form.

The change is repeatable (a prepared chunk is left as it is) and keeps LF line
ends. Run it after inline_save_restore_gpr.py and before
scripts/mods/prepare_simulation_60hz.py, whose manifest hashes the chunks as
they finally are.
"""
import bisect
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MARK = "/* bluewake: direct calls between chunks (cmake/composite/direct_calls.h) */\n"
INCLUDE = '#include "../generated.h"\n'
CALL = re.compile(
    r"    // ([0-9A-F]{8}): bl      0x([0-9A-F]{8})\n"
    r"    \{\n"
    r"            ctx->lr = 0x([0-9A-F]{8})u;\n"
    r"            ctx->pc = 0x\2u;\n"
    r"            return;\n"
    r"    \}\n")
FUNCTION = re.compile(r"^(?:static )?void \w+\(CPUState\* ctx_param\) \{$", re.M)
TABLE = re.compile(r"static DolRecompFunction s_dolrecomp_chunk_fns\[\] = \{(.*?)\};", re.S)
DOL_CODE = (0x80003100, 0x80400000)
REL_CODE = (0xC0400000, 0xC2000000)  # the REL modules' translated code (rel_modules.inc)
MIRROR = 0x40000000
# Entries the dispatcher answers with native code when BLUEWAKE_NATIVE_MATH is
# on (cmake/composite/native_math.c: PSMTXCopy, PSMTXConcat, PSMTXMultVec,
# PSMTXMultVecArray); a direct call would reach the translated body instead.
DISPATCHER_NATIVE = {0x8030D0C8, 0x8030D0FC, 0x8030DA44, 0x8030DA98}


def watched_addresses():
    """Guest addresses the host names: its edge service may act at any of them.
    Both mirror forms of each: the service tests a boundary's address with the
    0x40000000 bit cleared (host_canonical_linked_pc), so a REL chunk's
    0xC1E01B88 is its 0x81E01B88."""
    found = set()
    for folder in ("runtime/host/src", "windows/src"):
        for path in (ROOT / folder).rglob("*"):
            if path.suffix in (".c", ".h", ".cpp", ".mm", ".m"):
                for m in re.finditer(r"0x([8C][0-9A-Fa-f]{7})u?\b", path.read_text(errors="replace")):
                    address = int(m.group(1), 16)
                    found.update((address, address | 0x40000000, address & ~0x40000000))
    return found


def chunk_table(root):
    """The dispatch table's order: chunk start address -> index."""
    text = (root / "generated_composite.h").read_text()
    body = TABLE.search(text)
    if body is None:
        raise ValueError("generated_composite.h: no s_dolrecomp_chunk_fns table")
    starts = [int(m.group(1), 16) for m in re.finditer(r"func_([0-9A-F]{8})\b", body.group(1))]
    return starts, {start: index for index, start in enumerate(starts)}


def resolve(target):
    """The address the dispatcher runs for a call to `target`, or None when a
    direct call cannot stand in for it: main-executable code; that code through
    its 0xC0 mirror, which REL modules call it by and which the dispatcher
    strips before it looks the code up (dolrecomp_call_slow); or REL code."""
    if DOL_CODE[0] <= target < DOL_CODE[1]:
        return target
    if DOL_CODE[0] | MIRROR <= target < DOL_CODE[1] | MIRROR:
        return target & ~MIRROR
    if REL_CODE[0] <= target < REL_CODE[1]:
        return target
    return None


def transform(text, own_start, starts, index_of, watched):
    if MARK in text:
        return text, 0
    if INCLUDE not in text:
        raise ValueError("no generated.h include")
    all_starts = sorted(starts)
    bounds = [m.start() for m in FUNCTION.finditer(text)] + [len(text)]
    out, done, last = [], 0, 0
    for begin, end in zip(bounds, bounds[1:]):
        body = text[begin:end]
        pieces, cursor = [], 0
        for m in CALL.finditer(body):
            site, target, ret = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16)
            run = resolve(target)
            if run is None:
                continue
            i = bisect.bisect_right(all_starts, run) - 1
            if i < 0 or all_starts[i] == own_start:
                continue
            chunk = all_starts[i]
            if (target in watched or run in watched or run in DISPATCHER_NATIVE or ret in watched
                    or site in watched or ret != site + 4
                    or f"\nlabel_{ret:08X}:\n" not in body):
                continue
            # The dispatcher enters the callee with ctx->pc at the address it
            # runs; a mirrored call leaves the original pc if it goes round.
            enter = f"                ctx->pc = 0x{run:08X}u;\n" if run != target else ""
            pieces.append(body[cursor:m.start()])
            pieces.append(
                f"    // {m.group(1)}: bl      0x{m.group(2)}\n"
                "    {\n"
                f"            ctx->lr = 0x{m.group(3)}u;\n"
                f"            ctx->pc = 0x{m.group(2)}u;\n"
                "            if (bw_direct_call_ready(ctx)) {\n"
                + enter +
                "                bw_direct_depth++;\n"
                f"                bw_chunk_fns[{index_of[chunk]}](ctx);\n"
                "                bw_direct_depth--;\n"
                f"                if (ctx->pc == 0x{m.group(3)}u && bw_direct_call_ready(ctx))\n"
                f"                    goto label_{m.group(3)};\n"
                "            }\n"
                "            return;\n"
                "    }\n")
            cursor = m.end()
            done += 1
        pieces.append(body[cursor:])
        out.append(text[last:begin])
        out.append("".join(pieces))
        last = end
    out.append(text[last:])
    converted = "".join(out)
    if done:
        converted = converted.replace(INCLUDE, INCLUDE + MARK + '#include "direct_calls.h"\n', 1)
    return converted, done


def main():
    root = Path(sys.argv[1])
    chunks = sorted(root.glob("chunks_*/*.c"))
    if not chunks:
        sys.exit(f"no chunks under {root}")
    starts, index_of = chunk_table(root)
    watched = watched_addresses()
    sites = files = 0
    for path in chunks:
        m = re.search(r"_([0-9A-F]{8})\.c$", path.name)
        own_start = int(m.group(1), 16) if m else None
        with open(path, encoding="utf-8", newline="") as file:
            original = file.read()
        converted, count = transform(original, own_start, starts, index_of, watched)
        if count:
            temporary = path.with_suffix(".c.tmp")
            with open(temporary, "w", encoding="utf-8", newline="") as file:
                file.write(converted)
            temporary.replace(path)
            sites += count
            files += 1
    print(f"direct calls between chunks: {sites} calls in {files} chunks")


if __name__ == "__main__":
    main()
