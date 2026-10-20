"""Pure policy checks; no device, firmware archives or private backups."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VersionPolicyTests(unittest.TestCase):
    def test_native_policy(self):
        with tempfile.TemporaryDirectory(prefix="bridge-version-policy-") as tmp:
            executable = Path(tmp) / "version-policy"
            flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
            if os.environ.get("BRIDGE_POLICY_SANITIZE") == "1":
                flags += ["-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
            subprocess.run([os.environ.get("CC", "cc"), *flags,
                            str(ROOT / "src/bridge_version.c"),
                            str(ROOT / "tests/version_policy_harness.c"),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
