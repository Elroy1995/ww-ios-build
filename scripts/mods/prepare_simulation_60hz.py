#!/usr/bin/env python3
"""Add opt-in timing sites to the player's own translated GZLE01 sources.

No translated game source is shipped here. Every site checks its instruction
before any files are changed; all mod variants containing it are covered too.
The operation is repeatable and produces a manifest for the module build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

SITES = {}


def site(pc, instruction, code, owner):
    assert pc not in SITES
    SITES[pc] = (" ".join(instruction.split()), code, owner)


def half(pc, instruction, reg, owner):
    site(pc, instruction,
         f"if (bluewake_simulation_step == 0.5f) ctx->fpr[{reg}] *= 0.5;", owner)


site(0x80006410, "lwz r3, -30860(r13)", "bluewake_simulation_begin(ctx);", "main loop")
site(0x80255D34, "stwu r1, -32(r1)",
     "if (bluewake_simulation_step == 0.5f && ctx->gpr[3] == 1350000u) ctx->gpr[3] = 675000u;", "display wait")
site(0x80006454, "bl 0x80007224",
     "if (!bluewake_simulation_legacy_tick) { ctx->pc = 0x80006458u; return; }", "game-frame audio")
site(0x802449AC, "cmpwi r3, 1",
     "if (!bluewake_simulation_legacy_tick && ctx->gpr[3] != 1u) { ctx->pc = ctx->lr; return; }", "authored frame counter")
site(0x8010950C, "bl 0x8030DCE0",
     "bluewake_simulation_note(1);\n"
     "if (bluewake_simulation_step == 0.5f) { bluewake_simulation_player_move(ctx); ctx->lr = 0x80109510u; ctx->pc = 0x80109510u; return; }", "Link velocity integration")
site(0x80244894, "stwu r1, -16(r1)", "bluewake_simulation_note(2);", "actor collision pass")
half(0x80025170, "fadds f2, f0, f2", 2, "actor gravity")
for pc in (0x800251A8, 0x800251B8, 0x800251C8):
    half(pc, "fadds f0, f1, f0", 0, "actor velocity integration")
for pc, old_velocity in ((0x80109490, 2), (0x801094C0, 1)):
    site(pc, f"fadds f0, f{old_velocity}, f0",
         f"if (bluewake_simulation_step == 0.5f) {{ bluewake_simulation_player_gravity(ctx->fpr[{old_velocity}]); ctx->fpr[0] *= 0.5; }}", "Link gravity")
half(0x80108D14, "fadds f0, f0, f27", 27, "Link acceleration")
site(0x80109070, "fmr f31, f1",
     "if (bluewake_simulation_step == 0.5f) ctx->fpr[1] *= 2.0;", "foot displacement to original velocity units")
half(0x802EFBBC, "fadds f0, f1, f0", 0, "animation advance")
half(0x802EF614, "fadds f3, f2, f0", 0, "animation crossing")

# Shared integer timers keep their original units. The function still returns
# its actual current value on each half-step, including an expired zero.
for pc in (0x80007484, 0x8007FAFC, 0x800D83EC, 0x800FAF00, 0x800FD7E4, 0x80224274):
    site(pc, "addi r0, r4, -1",
         "if (!bluewake_simulation_legacy_tick) ctx->gpr[4] += 1u;", "shared countdown")
site(0x80110684, "addi r0, r4, -1",
     "if (!bluewake_simulation_legacy_tick) ctx->gpr[4] += 1u;", "Link damage immunity")

# Convert approach coefficients and speed caps, retaining spatial snap radii.
for pc, instruction, coef, limits in (
    (0x802528E4, "lfs f5, 0(r3)", 2, (3, 4)),
    (0x802529A4, "lfs f0, 0(r3)", 2, (3,)),
    (0x802529E8, "lfs f0, 0(r3)", 1, (2,)),
    (0x80252A20, "stwu r1, -112(r1)", 1, (2,)),
    (0x80252C5C, "stwu r1, -160(r1)", 1, (2,)),
    (0x80252EE0, "stwu r1, -112(r1)", 1, (2,)),
    (0x80253038, "stwu r1, -112(r1)", 1, (2,)),
):
    code = f"ctx->fpr[{coef}] = bluewake_simulation_approach(ctx->fpr[{coef}]);\n"
    code += "if (bluewake_simulation_step == 0.5f) { "
    code += " ".join(f"ctx->fpr[{r}] *= 0.5;" for r in limits) + " }"
    site(pc, instruction, code, "shared smoothing")
for pc, instruction, reg in (
    (0x80253440, "lfs f0, -16352(r2)", 2),
    (0x802534AC, "stwu r1, -80(r1)", 1),
    (0x80253610, "stwu r1, -96(r1)", 1),
):
    half(pc, instruction, reg, "shared approach speed")

# Angle interpolation uses an integer divisor and integer speed caps. Keep
# signed wraparound and retain odd cap units on alternate half-steps.
for pc, instruction, limits in (
    (0x802531A8, "lha r8, 0(r3)", (6, 7)),
    (0x80253270, "lha r7, 0(r3)", (6,)),
):
    code = "if (bluewake_simulation_step == 0.5f) {\n"
    code += "    int scale = (int16_t)ctx->gpr[5];\n"
    code += "    if (scale > 1) ctx->gpr[5] = scale > 16383 ? 32767u : (unsigned)(scale * 2);\n"
    for reg in limits:
        code += f"    {{ int step = (int16_t)ctx->gpr[{reg}]; ctx->gpr[{reg}] = (uint32_t)(step / 2 + (bluewake_simulation_legacy_tick ? step % 2 : 0)); }}\n"
    code += "}"
    site(pc, instruction, code, "angular approach")
site(0x80253790, "extsh. r0, r5",
     "if (bluewake_simulation_step == 0.5f) { int step = (int16_t)ctx->gpr[5]; ctx->gpr[5] = (uint32_t)(step / 2 + (bluewake_simulation_legacy_tick ? step % 2 : 0)); }", "angular chase")

# Integrate particles every tick, keeping authored emission bursts at 30 Hz.
# Field velocity is recomputed from scratch; only accumulated acceleration
# takes the time step, not that temporary velocity sum.
half(0x8025EB30, "fadds f0, f1, f0", 0, "particle age")
for pc in (0x8025EBE8, 0x8025EBF8, 0x8025EC08,
           0x8025ECF0, 0x8025ED00, 0x8025ED10):
    half(pc, "fadds f0, f1, f0", 0, "particle motion")
site(0x8025EC6C, "fmuls f0, f0, f1",
     "if (bluewake_simulation_step == 0.5f && ctx->fpr[1] >= 0.0) ctx->fpr[1] = sqrt(ctx->fpr[1]);", "particle damping")
for pc in (0x8025A174, 0x8025A184, 0x8025A194, 0x8025A1A8, 0x8025A1B8, 0x8025A1C8):
    half(pc, "fadds f0, f1, f0", 0, "particle field acceleration")
half(0x8025D34C, "fadds f0, f1, f0", 0, "emitter age")
site(0x8025D3C0, "stwu r1, -48(r1)",
     "if (!bluewake_simulation_legacy_tick) { ctx->pc = ctx->lr; return; }", "emission cadence")
site(0x8025ED6C, "stwu r1, -48(r1)",
     "if (bluewake_simulation_step == 0.5f) {\n"
     "    uint32_t bits = mem_read32(ctx, ctx->gpr[3] + 120u); float age; memcpy(&age, &bits, 4);\n"
     "    if (age != floorf(age)) { ctx->gpr[3] = 0; ctx->pc = ctx->lr; return; }\n"
     "}", "child emission once per authored particle frame")

INSTRUCTION = re.compile(r"(^    // ([0-9A-F]{8}): ([^\n]+)\n)(    if \(!ppc_fp_available_inline[^\n]+\n)?", re.M)
BLOCK = re.compile(r"    /\* bluewake60:[^\n]+\n.*?    /\* end bluewake60 \*/\n", re.S)
HEADER = '#include "simulation_timing.h"\n#include <math.h>\n'


def transform(text, counts):
    text = BLOCK.sub("", text).replace(HEADER, "")
    touched = False

    def replace(match):
        nonlocal touched
        pc = int(match[2], 16)
        if pc not in SITES:
            return match[0]
        expected, code, owner = SITES[pc]
        if " ".join(match[3].split()) != expected:
            raise ValueError(f"{pc:08X}: expected {expected!r}, got {match[3]!r}")
        counts[pc] += 1
        touched = True
        body = "\n".join("    " + line for line in code.splitlines())
        return match[0] + f"    /* bluewake60:{pc:08X} {owner} */\n{body}\n    /* end bluewake60 */\n"

    text = INSTRUCTION.sub(replace, text)
    return (HEADER + text if touched else text), touched


def prepare(root):
    counts = dict.fromkeys(SITES, 0)
    pending = []
    for path in sorted(root.glob("chunks_*/*.c")):
        original = path.read_text()
        converted, touched = transform(original, counts)
        if touched or original != converted:
            pending.append((path, original, converted, touched))
    missing = [f"{pc:08X}" for pc, count in counts.items() if count == 0]
    if missing:
        raise ValueError("missing GZLE01 timing sites: " + ", ".join(missing))
    # Validate the entire input before writing anything. A drifted translation
    # cannot leave a half-patched module marked as supported.
    for path, original, converted, _ in pending:
        if original != converted:
            temporary = path.with_suffix(".c.tmp")
            temporary.write_text(converted)
            temporary.replace(path)
    manifest = {
        "abi": 1,
        "sites": {f"{pc:08X}": {"copies": count, "owner": SITES[pc][2]} for pc, count in counts.items()},
        "files": {str(p.relative_to(root)): hashlib.sha256(text.encode()).hexdigest() for p, _, text, touched in pending if touched},
    }
    (root / "simulation_60hz.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"60 Hz support: {len(SITES)} verified sites in {len(pending)} translated chunks; default off")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("composite", type=Path)
    args = parser.parse_args()
    try:
        prepare(args.composite)
    except ValueError as error:
        parser.exit(1, f"60 Hz preparation failed: {error}\n")
