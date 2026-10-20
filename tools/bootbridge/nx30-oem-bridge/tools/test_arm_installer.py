#!/usr/bin/env python3
"""Exercise the ARM TEST installer against isolated ordinary-file NAND fixtures.

The test build cannot open a real MTD or invoke a real reboot. No device nodes
or network are exposed to the guest. Production payload bytes are not changed.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))
from test_install import bc_image, make_fixture, write_env_entries, write_test_oem_manifest_header


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        raise ValueError("use a new ordinary-file output directory")
    args.output.mkdir(parents=True, mode=0o700)
    root = args.output / "guest"
    root.mkdir()
    (root / "fixture").mkdir()
    (root / "qemu-arm").touch()
    binary = root / "harness"
    catalog = Path(os.environ.get("H3C_NAND_CATALOG_HEADER", ""))
    if not catalog.is_file():
        raise RuntimeError("set H3C_NAND_CATALOG_HEADER to the authoritative shared catalog header")
    catalog_include = args.output / "catalog-include"
    catalog_include.mkdir()
    shutil.copyfile(catalog, catalog_include / "h3c_nand_catalog.h")
    write_test_oem_manifest_header(catalog_include)
    environment = dict(os.environ, STAGING_DIR=str(args.cc.resolve().parents[2]))
    subprocess.run([str(args.cc), "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static",
                    "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST", "-I", str(catalog_include), "-I", str(PROJECT / "src"),
                    str(PROJECT / "tests/install_harness.c"),
                    str(PROJECT / "src/bridge_install.c"), str(PROJECT / "src/bridge_version.c"), str(PROJECT / "src/bridge_oem_slot.c"), str(PROJECT / "src/bridge_backup.c"), str(PROJECT / "src/bridge_root_binding.c"), str(PROJECT / "src/bridge_first_install.c"), str(PROJECT / "src/bridge_install_identity.c"), "-o", str(binary)], check=True, env=environment)
    subprocess.run([str(args.cc), "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static",
                    "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST", "-I", str(catalog_include), "-I", str(PROJECT / "src"),
                    str(PROJECT / "tests/status_errors_harness.c"), str(PROJECT / "src/bridge_version.c"), str(PROJECT / "src/bridge_oem_slot.c"), str(PROJECT / "src/bridge_backup.c"), str(PROJECT / "src/bridge_root_binding.c"), str(PROJECT / "src/bridge_first_install.c"), str(PROJECT / "src/bridge_install_identity.c"), "-o", str(root / "status-errors-harness")],
                   check=True, env=environment)
    qemu, bwrap = shutil.which("qemu-arm-static"), shutil.which("bwrap")
    if not qemu or not bwrap:
        raise ValueError("existing offline QEMU and bwrap required")
    rows = []
    for label, fault, mode, want, modified, env_case in [
        ("optional_backup_install_replay_reboot", "backupfail", "replay", "complete", True, None),
        ("partial_appsbl_write", "partial_write", "install", "failed", True, None),
        ("partial_first_bootconfig", "bc1_partial", "install", "failed", True, None),
        ("unknown_boot_authentication", None, "status", "ready", False, None),
        ("accepted_status_failure", None, "accepted_failure", "failed", False, None),
        ("reboot_worker_failure", None, "reboot_failure", "complete", True, None),
        ("already_installed_reboot_failure", None, "reboot_failure", "complete", True, None),
        ("env_alias_exact", None, "status", "ready", False, "exact"),
        ("env_alias_near_miss", None, "status", "ready", False, "near"),
        ("env_alias_duplicate", None, "status", "blocked", False, "duplicate"),
        ("env_alias_bad_crc", None, "status", "blocked", False, "bad_crc"),
        ("candidate_missing_oem_return", None, "return_oem", "complete", True, "candidate_missing"),
        ("candidate_corrupt_oem_return", None, "return_oem", "complete", True, "candidate_corrupt"),
    ]:
        fixture = args.output / label
        make_fixture(fixture, fault=fault, active_payload=label == "already_installed_reboot_failure")
        pre_action = None
        if env_case in ("exact", "near", "duplicate", "bad_crc"):
            entries = [("productname", "NX30_2i5x"),
                       ("productconfig", "/etc/config/NX30/product_NX30.ini"),
                       ("machid", "8040000"),
                       ("bootcmd", "bootipq"),
                       ("bootargs", "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),
                       ("bootdelay", "3")]
            if env_case == "near":
                entries[0] = ("productname", "NX30_2i5x-extra")
            elif env_case == "duplicate":
                entries.append(("productname", "NX30_2i5x"))
            write_env_entries(fixture, entries, bad_crc=env_case == "bad_crc")
        elif env_case in ("candidate_missing", "candidate_corrupt"):
            candidate = fixture / "etc/bridge/uboot.bin"
            if env_case == "candidate_missing":
                candidate.unlink()
            else:
                payload = bytearray(candidate.read_bytes())
                payload[0] ^= 0x80
                candidate.write_bytes(payload)
        if label == "unknown_boot_authentication":
            (fixture / "sys/devices/system/qfprom/qfprom0/authenticate").unlink()
        before = {p.name:p.read_bytes() for p in (fixture / "dev").iterdir()}
        command = [bwrap, "--unshare-all", "--die-with-parent", "--new-session",
                   "--ro-bind", str(root), "/", "--ro-bind", qemu, "/qemu-arm",
                   "--bind", str(fixture), "/fixture", "--chdir", "/",
                   "/qemu-arm", "/status-errors-harness" if mode in ("accepted_failure", "reboot_failure") else "/harness", "/fixture", mode]
        if env_case in ("candidate_missing", "candidate_corrupt"):
            status_result = subprocess.run(command[:-1] + ["status"], capture_output=True, text=True, timeout=45)
            if status_result.returncode:
                raise AssertionError(label + " pre-action status failed; see private fixture")
            pre = json.loads(next(line for line in status_result.stdout.splitlines() if line.startswith("{")))
            if pre["phase"] != "ready" or pre["install_ready"] or not pre["actions"]["return_oem"]:
                raise AssertionError(label + " must refuse install while exposing only the independently qualified return")
            pre_action = {"phase": pre["phase"], "install_ready": pre["install_ready"],
                          "return_oem": pre["actions"]["return_oem"]}
        result = subprocess.run(command, capture_output=True, text=True, timeout=45)
        (args.output / (label + ".log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError(label + " failed; see private log")
        states = [json.loads(s) for s in result.stdout.splitlines() if s.startswith("{")]
        state = states[-1] if mode in ("accepted_failure", "reboot_failure") else states[0]
        if state["phase"] != want or state["may_have_modified"] != modified:
            raise AssertionError(label + " returned unexpected phase/modification status")
        if env_case == "exact":
            if not state["install_ready"] or not state["actions"]["return_oem"]:
                raise AssertionError(label + " is an explicitly synthetic positive install and OEM-return fixture")
            checks = {check["name"]: check for check in state["checks"]}
            provider = checks.get("install_chain_qualification", {})
            if provider.get("status") != "pass" or "合成独立资格provider" not in provider.get("detail", ""):
                raise AssertionError(label + " lacks the explicit test-only synthetic provider diagnostic")
        if label == "unknown_boot_authentication" or env_case == "near":
            if state["install_ready"] or not state["actions"]["return_oem"]:
                raise AssertionError(label + " must keep installation unavailable while allowing only verified OEM return")
        if env_case in ("duplicate", "bad_crc"):
            if state["install_ready"] or state["actions"]["return_oem"]:
                raise AssertionError(label + " must remain closed for malformed or CRC-invalid ENV")
        if env_case in ("candidate_missing", "candidate_corrupt"):
            if state["phase"] != "complete" or state["stage"] != "oem_return_reboot_simulated":
                raise AssertionError(label + " failed to complete the synthetic OEM-return path")
            if state["actions"]["return_oem"] or state["can_reboot"]:
                raise AssertionError(label + " exposed an action after the one-shot return")
            after = {p.name:p.read_bytes() for p in (fixture / "dev").iterdir()}
            for name, data in before.items():
                if name not in ("mtd2", "mtd3") and after[name] != data:
                    raise AssertionError(label + " changed protected partition " + name)
            expected = bc_image(age=3, appsbl=1, rootfs=0)
            if after["mtd3"] != expected or after["mtd2"] != expected:
                raise AssertionError(label + " did not write matching OEM selector records")
        if mode == "replay":
            assert "replay_rc=-1" in result.stdout and "reboot_simulated" in states[-1]["stage"]
        if mode in ("accepted_failure", "reboot_failure"):
            expected_stage = "status_failed" if mode == "accepted_failure" else "reboot_failed"
            if state["stage"] != expected_stage or state["install_ready"] or state["can_reboot"]:
                raise AssertionError(label + " returned unsafe terminal status")
            if mode == "accepted_failure":
                if "start_rc=-1" not in result.stdout or "replay_rc=-1" not in result.stdout or "reboot_rc=-1" not in result.stdout:
                    raise AssertionError(label + " accepted an invalid action")
            else:
                if any(marker not in result.stdout for marker in ("repeat_reboot_rc=-1", "retry_reboot_rc=-1", "reinstall_rc=-1")):
                    raise AssertionError(label + " accepted a repeated action")
        if not modified and before != {p.name:p.read_bytes() for p in (fixture / "dev").iterdir()}:
            raise AssertionError(label + " changed synthetic NAND unexpectedly")
        rows.append({"test": label, "phase": state["phase"], "stage": state["stage"],
                     "may_have_modified": modified, "install_ready": state["install_ready"],
                     "return_oem": state["actions"]["return_oem"], "pre_action": pre_action})
    report = {"status": "PASS_ARM_INSTALLER_FILE_FIXTURES", "device_operations": False,
              "real_MTD_or_reboot": False, "tests": rows}
    (args.output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
