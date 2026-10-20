"""Host vectors for the source-bound, read-only provider dispatcher."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class DispatcherTests(unittest.TestCase):
    def test_full_tuple_selection_artifact_pinning_and_sysfs_class_link(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "sys/class/mtd").mkdir(parents=True)
            (root / "sys/devices/virtual/mtd").mkdir(parents=True)
            (root / "resource.bin").write_bytes(b"test")
            (root / "resource.bin").chmod(0o444)
            (root / "link.bin").symlink_to("resource.bin")
            binary = root / "dispatch-harness"
            subprocess.run([
                os.environ.get("CC", "cc"), "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                "-DBDI_TEST", "-I", str(PROJECT / "src"),
                str(PROJECT / "src/bridge_dispatch.c"),
                str(PROJECT / "tests/dispatch_harness.c"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary), str(root),
                            str(PROJECT / "tests/fixtures/nand-dmesg.raw")], check=True)


if __name__ == "__main__":
    unittest.main()
