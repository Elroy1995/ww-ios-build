#!/usr/bin/env python3
"""Keep certified register-save calls inside their generated caller.

This upgrades old composites that predate DolRecomp's direct cross-chunk
calls. Only hashes and this transformation are distributed; translated
instructions remain in the player's private build directory.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

LEAVES = (
    (0x80328F04, 0x80328F50, 'b7fa7b91c185412cce8d7dfc7eccafd5c50d69f7f49b66d111c582d11ab8df1b'),
    (0x80328F50, 0x80328F9C, 'f525cbda6f2bed00dbaa48533f1a32c12ed19b571328e7045945a08b9c3ca2d4'),
)
DECLARATION = '''// Certified BlueWake nonvolatile-register call fast path.
extern int bluewake_inline_gpr_enabled;
int bluewake_native_gpr(CPUState* ctx, u32 address);
'''
CALL = re.compile(r'(// ([0-9A-F]{8}): bl\s+0x([0-9A-F]{8})\n'
                  r'    \{\n            ctx->lr = 0x([0-9A-F]{8})u;\n'
                  r'            ctx->pc = 0x\3u;\n)'
                  r'(            /\* BLUEWAKE_GPR_BEGIN \*/\n.*?'
                  r'            /\* BLUEWAKE_GPR_END \*/\n)?'
                  r'            return;\n    \}', re.DOTALL)


def transform(source):
    sites = 0

    def replace(match):
        nonlocal sites
        pc, target, continuation = (int(match[i], 16) for i in (2, 3, 4))
        eligible = (0x80328F04 <= target <= 0x80328F48 or
                    0x80328F50 <= target <= 0x80328F94) and target % 4 == 0
        if not eligible:
            return match[0]
        if continuation != pc + 4:
            raise ValueError(f'missing GPR continuation at {pc:08X}')
        if f'\nlabel_{continuation:08X}:' not in source:
            if match[5]:
                raise ValueError(f'missing marked continuation at {pc:08X}')
            return match[0]
        body = ('            /* BLUEWAKE_GPR_BEGIN */\n'
                f'            if (bluewake_inline_gpr_enabled && bluewake_native_gpr(ctx, 0x{target:08X}u))\n'
                f'                goto label_{continuation:08X};\n'
                '            /* BLUEWAKE_GPR_END */\n')
        if match[5] and match[5] != body:
            raise ValueError(f'modified GPR call guard at {pc:08X}')
        sites += 1
        return match[1] + body + '            return;\n    }'

    result = CALL.sub(replace, source)
    if result.count('/* BLUEWAKE_GPR_BEGIN */') != sites:
        raise ValueError('unrecognized GPR call marker')
    return result, sites


def prepare(root):
    pending = {}
    callee_paths = list(root.rglob('*803256E0*.c'))
    if not callee_paths:
        raise ValueError('missing translated GPR helper chunk')
    for path in callee_paths:
        source = path.read_text()
        for start, end, digest in LEAVES:
            begin = source.find(f'\nlabel_{start:08X}:')
            finish = source.find(f'\nlabel_{end:08X}:')
            if begin < 0 or finish <= begin:
                raise ValueError(f'missing GPR leaf {start:08X} in {path}')
            actual = hashlib.sha256(' '.join(source[begin:finish].split()).encode()).hexdigest()
            if actual != digest:
                raise ValueError(f'changed GPR leaf {start:08X} in {path}')
        pending[path] = source
    sites = 0
    for path in sorted(root.glob('chunks_*/*.c')):
        source = path.read_text()
        converted, count = transform(source)
        if count:
            pending[path] = converted
            sites += count
    header = root / 'generated.h'
    source = header.read_text()
    if DECLARATION not in source:
        anchor = '#include DOLRECOMP_CPU_HEADER\n'
        if source.count(anchor) != 1 or 'bluewake_inline_gpr_enabled' in source:
            raise ValueError('unsupported GPR header')
        source = source.replace(anchor, anchor + DECLARATION)
    # Validate all source before changing any file.
    for path, converted in list(pending.items()) + [(header, source)]:
        if converted != path.read_text():
            temporary = path.with_suffix(path.suffix + '.tmp')
            temporary.write_text(converted)
            temporary.replace(path)
    manifest = {'abi': 1, 'sites': sites, 'files': {
        str(path.relative_to(root)): hashlib.sha256(source.encode()).hexdigest()
        for path, source in pending.items()}}
    (root / 'native_gpr.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'native GPR: {sites} certified caller continuations in {len(pending)} chunks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('composite', type=Path)
    args = parser.parse_args()
    try:
        prepare(args.composite)
    except ValueError as error:
        parser.exit(1, f'{error}\n')
