"""Validate timing-source preparation without distributing game code."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "timing", Path(__file__).parents[1] / "scripts/mods/prepare_simulation_60hz.py")
TIMING = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIMING)


class SourcePreparationTests(unittest.TestCase):
    def test_variants_idempotence_and_atomic_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base, variant = root / "chunks_dol/a.c", root / "chunks_mod_test/b.c"
            base.parent.mkdir()
            variant.parent.mkdir()
            # Instruction identities only, not a translated game payload.
            source = "".join(f"    // {pc:08X}: {value[0]}\n    instruction();\n"
                             for pc, value in TIMING.SITES.items())
            base.write_text(source)
            variant.write_text(source)
            TIMING.prepare(root)
            paths = (base, variant, root / "simulation_60hz.json")
            first = [p.read_bytes() for p in paths]
            TIMING.prepare(root)
            self.assertEqual(first, [p.read_bytes() for p in paths])
            self.assertEqual(base.read_bytes(), variant.read_bytes())
            variant.write_text(source.replace("stwu r1, -16(r1)", "wrong instruction"))
            unchanged = base.read_bytes()
            with self.assertRaises(ValueError):
                TIMING.prepare(root)
            self.assertEqual(unchanged, base.read_bytes())
            variant.unlink()
            base.write_text(source.replace("// 8010950C:", "// 80109508:"))
            unchanged = base.read_bytes()
            with self.assertRaises(ValueError):
                TIMING.prepare(root)
            self.assertEqual(unchanged, base.read_bytes())

    def test_float_availability_precedes_injection(self):
        pc = 0x802EFBBC
        source = (f"    // {pc:08X}: {TIMING.SITES[pc][0]}\n"
                  "    if (!ppc_fp_available_inline(ctx)) return;\n"
                  "    original_instruction();\n")
        result, touched = TIMING.transform(source, dict.fromkeys(TIMING.SITES, 0))
        self.assertTrue(touched)
        self.assertLess(result.index("ppc_fp_available_inline"), result.index("/* bluewake60:"))


if __name__ == "__main__":
    unittest.main()
