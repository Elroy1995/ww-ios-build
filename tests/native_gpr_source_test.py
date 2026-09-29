#!/usr/bin/env python3
"""The caller rewrite must be repeatable and reject a drifted helper/guard."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

script = Path(__file__).resolve().parents[1] / 'scripts/mods/prepare_native_gpr.py'
spec = importlib.util.spec_from_file_location('gpr_prepare', script)
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)

CALL = '''// 80000100: bl      0x80328F40
    {
            ctx->lr = 0x80000104u;
            ctx->pc = 0x80328F40u;
            return;
    }

label_80000104:
'''


class GprCertificationTest(unittest.TestCase):
    def test_repeat_and_guard_drift(self):
        first, count = prepare.transform(CALL)
        self.assertEqual(count, 1)
        self.assertIn('goto label_80000104;', first)
        self.assertEqual(prepare.transform(first), (first, 1))
        with self.assertRaises(ValueError):
            prepare.transform(first.replace('bluewake_inline_gpr_enabled && ', ''))

    def test_continuation_and_non_gpr_calls(self):
        tail = CALL[:CALL.index('\nlabel_')]
        self.assertEqual(prepare.transform(tail), (tail, 0))
        normal = CALL.replace('80328F40', '80301234')
        self.assertEqual(prepare.transform(normal), (normal, 0))
        with self.assertRaises(ValueError):
            prepare.transform(CALL.replace('lr = 0x80000104', 'lr = 0x80000108'))

    def test_modified_helper_does_not_patch_callers(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            chunks = root / 'chunks_dol'
            chunks.mkdir()
            body = '\nlabel_80328F04:\n    return;\n'
            original = prepare.LEAVES
            prepare.LEAVES = ((0x80328F04, 0x80328F50,
                hashlib.sha256(' '.join(body.split()).encode()).hexdigest()),)
            try:
                helper = chunks / 'chunk_803256E0.c'
                helper.write_text(body + '\nlabel_80328F50:\n')
                caller = chunks / 'chunk_80000100.c'
                caller.write_text(CALL)
                (root / 'generated.h').write_text('#include DOLRECOMP_CPU_HEADER\n')
                prepare.prepare(root)
                manifest = (root / 'native_gpr.json').read_text()
                prepare.prepare(root)
                self.assertEqual(manifest, (root / 'native_gpr.json').read_text())
                caller.write_text(CALL)
                helper.write_text(body.replace('return;', 'ctx->pc=0;') + '\nlabel_80328F50:\n')
                with self.assertRaises(ValueError):
                    prepare.prepare(root)
                self.assertEqual(caller.read_text(), CALL)
            finally:
                prepare.LEAVES = original


if __name__ == '__main__':
    unittest.main()
