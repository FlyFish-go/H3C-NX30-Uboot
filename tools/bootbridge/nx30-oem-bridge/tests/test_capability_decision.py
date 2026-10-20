"""Compile and run the standalone fail-closed capability decision core."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CapabilityDecisionTests(unittest.TestCase):
    def test_typed_observations_and_per_operation_grants(self):
        with tempfile.TemporaryDirectory(prefix="bridge-capability-") as tmp:
            binary = Path(tmp) / "capability-test"
            subprocess.run(
                [
                    "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "src"),
                    str(ROOT / "tests/test_capability_decision.c"),
                    str(ROOT / "src/bridge_capability.c"),
                    "-o", str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True, timeout=10
            )
            self.assertEqual(result.stdout.strip(), "capability-decision: PASS")


if __name__ == "__main__":
    unittest.main()
