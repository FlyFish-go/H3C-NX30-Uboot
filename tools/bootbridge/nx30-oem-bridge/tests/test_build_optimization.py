"""Optimization must never turn the offline builder's assertions into no-ops."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
BUILDER = TOOLS / "build.py"
GUARD_MESSAGE = "NX30 bridge builder requires assertions; Python optimization is unsupported"


class BuildOptimizationTests(unittest.TestCase):
    def run_python(self, args, *, optimize=None):
        env = dict(os.environ)
        env.pop("PYTHONOPTIMIZE", None)
        if optimize is not None:
            env["PYTHONOPTIMIZE"] = optimize
        return subprocess.run([sys.executable, *args], env=env,
                              capture_output=True, text=True, timeout=15)

    def test_cli_refuses_optimized_interpreter_before_any_output(self):
        for flags, optimization in [(["-O"], None), (["-OO"], None), ([], "1")]:
            with self.subTest(flags=flags, optimization=optimization), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                output = root / "candidate"
                # Deliberately nonexistent ordinary inputs: the guard must run
                # before reading them, invoking tools, or creating the output.
                args = [str(BUILDER)]
                for key in ("official", "rootfs", "cc", "ubinize", "uboot", "payload-report"):
                    args += ["--" + key, str(root / key)]
                args += ["--output", str(output)]
                result = self.run_python([*flags, *args], optimize=optimization)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(GUARD_MESSAGE, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(list(root.iterdir()), [])

    def test_import_refuses_optimization_before_public_entry_points_are_exposed(self):
        for flags, optimization in [(["-O"], None), (["-OO"], None), ([], "1")]:
            with self.subTest(flags=flags, optimization=optimization), tempfile.TemporaryDirectory() as td:
                marker = Path(td) / "import-returned"
                script = ("import sys; from pathlib import Path; "
                          f"sys.path.insert(0, {str(TOOLS)!r}); "
                          "from build import build, bridge_kernel, validate_payload_report; "
                          f"Path({str(marker)!r}).write_text('unsafe import succeeded')")
                result = self.run_python([*flags, "-c", script], optimize=optimization)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(GUARD_MESSAGE, result.stderr)
                self.assertEqual(list(Path(td).iterdir()), [])

    def test_normal_cli_help_is_available(self):
        result = self.run_python([str(BUILDER), "--help"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--official", result.stdout)

    def test_normal_import_preserves_hash_validation(self):
        # Synthetic FIT nodes exercise a real hash mismatch without OEM inputs.
        script = fr"""
import sys
sys.path.insert(0, {str(TOOLS)!r})
import build
build.fit_nodes = lambda raw: {{
    '/images/test': {{'data': b'fixture'}},
    '/images/test/hash': {{'algo': b'sha1\x00', 'value': b'bad digest'}}}}
try:
    build.verify_fit(b'unused fixture container')
except ValueError as error:
    if 'FIT hash mismatch' not in str(error):
        raise
else:
    raise RuntimeError('invalid FIT was accepted')
"""
        result = self.run_python(["-c", script])
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
