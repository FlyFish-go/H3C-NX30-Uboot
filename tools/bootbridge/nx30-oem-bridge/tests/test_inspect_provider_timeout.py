"""Regression coverage for the production inspect_provider poll loop."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class InspectProviderTimeoutTests(unittest.TestCase):
    @staticmethod
    def build_harness(output: Path, production_timing: bool = False):
        flags = ["-DBDI_INSPECT_PRODUCTION_TIMING=1"] if production_timing else []
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-ffunction-sections", "-fdata-sections", *flags,
            str(PROJECT / "tests/inspect_provider_timeout_harness.c"),
            "-Wl,--gc-sections", "-o", str(output),
        ], check=True, timeout=30)

    def test_timeout_override_is_confined_to_inspect_test_build(self):
        cases = [
            (["-DBDI_INSPECT_TEST"], "BDI_INSPECT_TEST_requires_BDI_TEST"),
            (["-DBDI_INSPECT_TIMEOUT_MS=100"], "BDI_INSPECT_TIMEOUT_MS_is_test_only"),
            (["-DBDI_TEST", "-DBDI_INSPECT_TIMEOUT_MS=100"],
             "BDI_INSPECT_TIMEOUT_MS_is_test_only"),
        ]
        for defines, marker in cases:
            with self.subTest(defines=defines), tempfile.TemporaryDirectory() as td:
                result = subprocess.run([
                    os.environ.get("CC", "cc"), "-std=c99", "-I", str(PROJECT / "src"),
                    *defines, "-c", str(PROJECT / "src/bridge_dispatch.c"),
                    "-o", str(Path(td) / "must-not-build.o"),
                ], capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(marker, result.stderr)

    def test_production_inspector_uses_elapsed_time_and_reaps_child(self):
        with tempfile.TemporaryDirectory() as td:
            binary = Path(td) / "inspect-provider-timeout"
            self.build_harness(binary)
            result = subprocess.run([str(binary)], check=True, capture_output=True,
                                    text=True, timeout=5)
            self.assertIn("EARLY_EOF result=-1", result.stdout)
            self.assertIn("HUNG_CHILD result=-1", result.stdout)
            self.assertIn("BAD_REPORT_AND_EXIT rejected; VALID_REPORT result=1", result.stdout)

    def test_production_deadline_accepts_valid_inspection_after_fifteen_seconds(self):
        with tempfile.TemporaryDirectory() as td:
            binary = Path(td) / "inspect-provider-production-timing"
            self.build_harness(binary, production_timing=True)
            result = subprocess.run([str(binary), "--long-valid"], check=True,
                                    capture_output=True, text=True, timeout=35)
            self.assertIn("SLOW_VALID result=1", result.stdout)


if __name__ == "__main__":
    unittest.main()
