"""Production MTD observer/CLI exercised with wrapped syscalls, never hardware."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WRAPS = ["ioctl", "fstat", "fstat64", "fcntl", "fcntl64", "pread", "pread64"]


class MTDObserverTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="bridge-mtd-observer-")
        cls.out = Path(cls.temp.name)
        cls.cc = [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall",
                  "-Wextra", "-Werror", "-I" + str(ROOT / "src")]
        cls.observer = str(ROOT / "src/bridge_mtd_observer.c")
        wrap = ["-Wl,--wrap=" + name for name in WRAPS]
        subprocess.run(cls.cc + [cls.observer, str(ROOT / "tests/mtd_observer_harness.c"),
                                 *wrap, "-o", str(cls.out / "harness")], check=True)
        subprocess.run(cls.cc + ["-Dmain=observer_probe_main", "-c",
                                 str(ROOT / "tools/observe_appsbl.c"),
                                 "-o", str(cls.out / "cli.o")], check=True)
        subprocess.run(cls.cc + [cls.observer, str(cls.out / "cli.o"),
                                 str(ROOT / "tests/mtd_observer_cli_fixture.c"),
                                 *wrap, "-Wl,--wrap=open", "-Wl,--wrap=open64",
                                 "-o", str(cls.out / "cli-fixture")], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_native_observer_syscall_failures_and_coverage(self):
        result = subprocess.run([str(self.out / "harness")], check=True,
                                capture_output=True, text=True, timeout=30)
        self.assertIn("RESULT 17 PASS", result.stdout)

    def test_cli_outputs_parse_and_bind_distinct_copies(self):
        expected = [hashlib.sha256(bytes([b]) * 0x140000).hexdigest()
                    for b in (0x11, 0x22)]
        for mode, code in (("clean", 0), ("nonclean", 3), ("reject", 4)):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.out / "cli-fixture"), mode],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, code, result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual(report["copies"]["copy0"]["sha256"], expected[0])
                self.assertEqual(report["pair_clean"], mode == "clean")
                self.assertEqual(report["pair_complete"], mode != "reject")
                if mode != "reject":
                    self.assertEqual(report["copies"]["copy1"]["sha256"], expected[1])
                else:
                    self.assertIn("bad eraseblock", report["copies"]["copy1"]["reason"])


if __name__ == "__main__":
    unittest.main()
