#!/usr/bin/env python3
"""Status JSON keeps dispatch blockers separate from install and return state."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_install import make_fixture, write_test_oem_manifest_header

PROJECT = Path(__file__).resolve().parents[1]

class DispatchStatusTests(unittest.TestCase):
    def test_dispatch_code_and_source_binding_are_visible_in_status_json(self):
        catalog = Path(__import__("os").environ.get("H3C_NAND_CATALOG_HEADER", ""))
        if not catalog.is_file():
            self.fail("set H3C_NAND_CATALOG_HEADER to the authoritative shared catalog header")
        with tempfile.TemporaryDirectory(prefix="dispatch-status-") as td:
            root = Path(td)
            include = root / "include"
            include.mkdir()
            (include / "h3c_nand_catalog.h").write_bytes(catalog.read_bytes())
            write_test_oem_manifest_header(include)
            binary = root / "status"
            fixture_root = root / "fixture"
            fixture_root.mkdir()
            make_fixture(fixture_root)
            command = ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
                "-I", str(include), "-I", str(PROJECT / "src"),
                str(PROJECT / "tests/install_harness.c"),
                str(PROJECT / "src/bridge_install.c"),
                str(PROJECT / "src/bridge_version.c"),
                str(PROJECT / "src/bridge_oem_slot.c"),
                str(PROJECT / "src/bridge_backup.c"),
                str(PROJECT / "src/bridge_root_binding.c"),
                str(PROJECT / "src/bridge_first_install.c"),
                str(PROJECT / "src/bridge_install_identity.c"), "-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            baseline_proc = subprocess.run([str(binary), str(fixture_root), "status"],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(baseline_proc.returncode, 0, baseline_proc.stderr)
            result = subprocess.run([str(binary), str(fixture_root), "dispatch_status"], capture_output=True,
                text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            detail_result = subprocess.run([str(binary), str(fixture_root), "dispatch_status_detail"],
                capture_output=True, text=True, timeout=5)
            self.assertEqual(detail_result.returncode, 0, detail_result.stderr)
            auth = fixture_root / "sys/devices/system/qfprom/qfprom0/authenticate"
            auth.write_text("1\n")
            inspection = subprocess.run([str(binary), str(fixture_root), "inspect"],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(inspection.returncode, 0, inspection.stderr)
            rejected = json.loads(inspection.stdout)
            self.assertFalse(rejected["install_ready"])
            self.assertTrue(rejected["return_ready"])
            self.assertEqual(rejected["rejected_check"], "qfprom_authenticate")
            self.assertIn("qfprom", rejected["reason"])
            auth.write_text("0\n")
            bc_path = fixture_root / "dev/mtd2"
            bc = bytearray(bc_path.read_bytes())
            bc[0] ^= 1
            bc_path.write_bytes(bc)
            inspection = subprocess.run([str(binary), str(fixture_root), "inspect"],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(inspection.returncode, 0, inspection.stderr)
            common_rejected = json.loads(inspection.stdout)
            self.assertFalse(common_rejected["install_ready"])
            self.assertEqual(common_rejected["rejected_check"], "bootconfig")
            self.assertNotIn(common_rejected["reason"], ("", "检查未完成"))
        baseline = json.loads(baseline_proc.stdout)
        status = json.loads(result.stdout)
        detailed = json.loads(detail_result.stdout)
        self.assertIs(type(status["install_ready"]), bool)
        self.assertEqual(status["install_ready"], baseline["install_ready"])
        self.assertEqual(status["actions"]["return_oem"], baseline["actions"]["return_oem"])
        self.assertEqual(status["oem_return"]["available"], baseline["oem_return"]["available"])
        self.assertEqual(status["dispatch"]["status"], "blocked")
        self.assertEqual(status["dispatch"]["code"], "candidate_unavailable")
        self.assertEqual(status["dispatch"]["result_code"], "candidate_unavailable")
        self.assertEqual(status["dispatch"]["reason"], "provider rejected its own contract")
        self.assertEqual(status["dispatch"]["candidate_source_revision_sha256"], "0123456789abcdef" * 4)
        self.assertEqual(status["dispatch_fallback"]["code"], "candidate_unavailable")
        self.assertIs(type(status["actions"]["return_oem"]), bool)
        self.assertEqual(detailed["dispatch"]["code"], "provider_exec_failed")
        self.assertEqual(detailed["dispatch"]["execution_detail"],
            'tool_revision=R6-preflight-v1;outcome=timeout;elapsed_ms=180000;last_stage=nand;note=escaped "quote"')
        self.assertEqual(detailed["dispatch_fallback"]["code"], "provider_exec_failed")
        self.assertEqual(detailed["install_ready"], baseline["install_ready"])


if __name__ == "__main__":
    unittest.main()
