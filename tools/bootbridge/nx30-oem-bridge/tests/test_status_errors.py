"""Synthetic-only regression for installation/status and reboot worker failures."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_install import make_fixture, bc_image, write_test_oem_manifest_header

ROOT = Path(__file__).resolve().parents[1]

class StatusErrorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="bridge-status-errors-")
        cls.exe = Path(cls.temp.name) / "harness"
        catalog = Path(os.environ["H3C_NAND_CATALOG_HEADER"])
        include = Path(cls.temp.name) / "include"
        include.mkdir()
        (include / "h3c_nand_catalog.h").write_bytes(catalog.read_bytes())
        write_test_oem_manifest_header(include)
        subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST", "-I", str(include), "-I", str(ROOT / "src"),
                        str(ROOT / "tests/status_errors_harness.c"), str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"), str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"), str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_install_identity.c"), "-o", str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, mode, active_payload=False):
        root = Path(tempfile.mkdtemp(dir=self.temp.name))
        before, payload = make_fixture(root, active_payload=active_payload)
        original = {p.name: p.read_bytes() for p in (root / "dev").iterdir()}
        run = subprocess.run([str(self.exe), str(root), mode], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr + run.stdout)
        states = [json.loads(line) for line in run.stdout.splitlines() if line.startswith("{")]
        return root, before, original, payload, run.stdout, states

    def test_accepted_status_failure_is_terminal_and_never_writes(self):
        root, _, original, _, output, states = self.run_case("accepted_failure")
        self.assertIn("start_rc=-1", output)
        self.assertIn("replay_rc=-1", output)
        self.assertIn("reboot_rc=-1", output)
        for state in states:
            self.assertEqual(state["phase"], "failed")
            self.assertEqual(state["stage"], "status_failed")
            self.assertFalse(state["install_ready"])
            self.assertFalse(state["may_have_modified"])
            self.assertFalse(state["can_reboot"])
        self.assertEqual(original, {p.name:p.read_bytes() for p in (root / "dev").iterdir()})

    def check_reboot_failure(self, already_installed):
        root, before, original, payload, output, states = self.run_case("reboot_failure", already_installed)
        prior, final = states
        self.assertEqual(prior["phase"], "complete")
        self.assertEqual(final["phase"], "complete")
        self.assertEqual(final["stage"], "reboot_failed")
        self.assertFalse(final["can_reboot"])
        self.assertFalse(final["install_ready"])
        self.assertTrue(final["may_have_modified"])
        self.assertIn("安装已完成", final["message"])
        self.assertIn("repeat_reboot_rc=-1", output)
        self.assertIn("retry_reboot_rc=-1", output)
        self.assertIn("reinstall_rc=-1", output)
        changed = {"mtd2", "mtd3"}
        if not already_installed:
            self.assertEqual((root/"dev/mtd6").read_bytes(), payload)
            changed.add("mtd6")
        for name, data in original.items():
            if name not in changed:
                self.assertEqual((root/"dev"/name).read_bytes(), data)
        expected = bc_image(age=3, appsbl=1 if already_installed else 0, rootfs=0)
        self.assertEqual((root/"dev/mtd2").read_bytes(), expected)
        self.assertEqual((root/"dev/mtd3").read_bytes(), expected)

    def test_reboot_failure_keeps_completed_installation(self):
        self.check_reboot_failure(False)

    def test_reboot_failure_keeps_skipped_appsbl_and_verified_oem_selector(self):
        self.check_reboot_failure(True)

if __name__ == "__main__":
    unittest.main()
