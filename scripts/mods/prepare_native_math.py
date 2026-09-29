#!/usr/bin/env python3
"""Certify the exact translated SDK bodies accepted by native_math.c.

Only hashes are distributed. Inputs come from the player's own disc. Check
all variants before writing the manifest; even a body edit that retains the
instruction comments must invalidate this optimization.
"""
import argparse
import hashlib
import json
from pathlib import Path

LEAVES = (
    (0x8030D0C8, 0x8030D0FC, '803096E0', '8d89ffc5251b41bc39532c918e0855ceceab319616a5c0ef12acfb9635d9b55d'),
    (0x8030D0FC, 0x8030D1C8, '803096E0', '8ab502958cfcc5e434d35b0848d89ae59a9af3b00b160835ec3dde947709f1ea'),
    (0x8030DA44, 0x8030DA98, '8030D6E0', 'cf4cfa14c036cdcc192f4b3ee9ed8ceda627c576a3a26de1fd922be96b51287e'),
)


def prepare(root):
    files = {}
    for start, end, chunk, expected in LEAVES:
        matches = sorted(root.rglob(f'*{chunk}*.c'))
        if not matches:
            raise ValueError(f'missing translated SDK chunk {chunk}')
        for path in matches:
            source = path.read_text()
            begin = source.find(f'\nlabel_{start:08X}:')
            finish = source.find(f'\nlabel_{end:08X}:')
            if begin < 0 or finish <= begin:
                raise ValueError(f'missing SDK function {start:08X} in {path}')
            body = ' '.join(source[begin:finish].split())
            if hashlib.sha256(body.encode()).hexdigest() != expected:
                raise ValueError(f'changed SDK function {start:08X} in {path}; native math not certified')
            files[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = root / 'native_math.json'
    temporary = manifest.with_suffix('.json.tmp')
    temporary.write_text(json.dumps({'abi': 1, 'files': files}, indent=2) + '\n')
    temporary.replace(manifest)
    print(f'native math: {len(LEAVES)} SDK leaves verified across {len(files)} translated chunks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('composite', type=Path)
    args = parser.parse_args()
    try:
        prepare(args.composite)
    except ValueError as error:
        parser.exit(1, f'{error}\n')
