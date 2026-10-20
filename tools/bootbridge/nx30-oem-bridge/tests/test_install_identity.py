"""Source-bound ENV identity and bootargs policy validator host tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]

class InstallIdentityTests(unittest.TestCase):
    def test_source_bound_identity_and_bootargs_rules(self):
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"identity-harness"
            subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror",
                "-I",str(PROJECT/"src"),str(PROJECT/"src/bridge_install_identity.c"),
                str(PROJECT/"tests/identity_harness.c"),"-o",str(exe)],check=True)
            result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
            self.assertIn("PASS 63 source-bound identity cases",result.stdout)

if __name__ == "__main__":
    unittest.main()
