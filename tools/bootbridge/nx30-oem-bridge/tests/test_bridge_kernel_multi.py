"""Pinned ordinary OEM inputs only; never execute their binaries or scripts."""
import hashlib
import importlib
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bridge_kernel as bk
import build as builder
import vendor_fit
import vendor_ubi

INPUTS = [
    ("NX30", "BB_OFFICIAL_NX30", "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"),
    ("RC3000", "BB_OFFICIAL_RC3000", "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44"),
    ("RT3000", "BB_OFFICIAL_RT3000", "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b"),
    ("RW3000", "BB_OFFICIAL_RW3000", "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5"),
]


class MemoryInput:
    def __init__(self, data): self.data = data
    def read_bytes(self): return self.data
    def __str__(self): return "verified ordinary official UBI in memory"


def kernel_from_package(path, expected_sha):
    path = Path(path)
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        raise ValueError("ordinary official package required")
    data = path.read_bytes()
    if bk.sha(data) != expected_sha or data[:6] != b"H3C_+k":
        raise ValueError("whole official package identity mismatch")
    identity = builder.identify_official(data)
    if identity["sha256"] != expected_sha:
        raise ValueError("builder source identity disagreement")
    total = struct.unpack_from(">I", data, 164)[0]
    if total < 40 or 160 + total > len(data): raise ValueError("outer FIT bounds")
    outer = vendor_fit.fdt_props(data[160:160 + total])
    for name, props in outer.items():
        if "algo" in props:
            parent = name.rsplit("/", 1)[0]
            if props.get("value") != bk._digest(outer[parent]["data"], bk._string(props["algo"])):
                raise ValueError("official outer FIT component digest mismatch")
    images = [v["data"] for p, v in outer.items()
              if p.startswith("/images/ubi-") and p.count("/") == 2]
    if len(images) != 1: raise ValueError("exact one official system UBI required")
    with mock.patch.object(vendor_ubi, "Path", lambda value: value):
        _, streams, _, _ = vendor_ubi.parse(MemoryInput(images[0]))
    return streams["kernel"], images[0]


class KernelMultiTest(unittest.TestCase):
    def test_unknown_source_and_optimized_interpreter(self):
        with self.assertRaises(ValueError): bk.transform_kernel(b"invalid", "0" * 64)
        with self.assertRaises(ValueError): bk.transform_kernel(b"invalid", INPUTS[0][2])
        result = subprocess.run([sys.executable, "-O", "-c",
            "import sys; sys.path.insert(0, %r); import bridge_kernel" % str(ROOT / "tools")],
            capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Python -O is unsupported", result.stderr)

    def test_four_pinned_official_inputs(self):
        reports = []
        missing = []
        configured = [(model, os.environ.get(env_name), source_sha)
                      for model, env_name, source_sha in INPUTS]
        for model, path, source_sha in configured:
            if not path or not Path(path).is_file():
                missing.append(model)
                continue
            with self.subTest(model=model):
                stock, source_ubi = kernel_from_package(path, source_sha)
                with tempfile.TemporaryDirectory(prefix="bb-source-rules-") as td:
                    root = Path(td); include = root / "include"; include.mkdir()
                    rules = builder.generate_oem_header(root, include, source_ubi, "1" * 64,
                                                       builder.OFFICIAL_SOURCES[source_sha])
                    self.assertEqual(rules["count"], 6)
                    header = (include / "bridge_oem_manifest.h").read_text()
                    self.assertIn("BI_OEM_POOL_COUNT", header)
                    self.assertIn("BI_OEM_POOL_REVISION", header)
                    self.assertEqual(rules['exit_policy'],1 if model=='NX30' else 0)
                before = bk._verify(stock)
                output, report = builder.bridge_kernel(stock, source_sha)
                after = bk._verify(output)
                self.assertEqual(set(before), set(after))
                images = [p for p in before if p.startswith("/images/") and p.count("/") == 2]
                configs = [p for p in before if p.startswith("/configurations/")]
                self.assertEqual(len(images), 25)
                self.assertEqual(len(configs), 24)
                for path in configs + ["/configurations"]:
                    self.assertEqual(before[path], after[path])
                self.assertEqual(before["/images/kernel@1"], after["/images/kernel@1"])
                selected = {"/images/" + value[0] for value in bk.ROUTES.values()}
                for path in images:
                    if path not in selected:
                        self.assertEqual(before[path], after[path])
                for config, (image, _, _) in bk.ROUTES.items():
                    path = "/images/" + image
                    old_dt = bk._parse(before[path]["data"])
                    new_dt = bk._parse(after[path]["data"])
                    expected_changes = {(p, "status") for p in bk.DISABLED} | {
                        ("/", "h3c,bridge-profile"), ("/chosen", "bootargs")}
                    self.assertEqual(bk._changes(old_dt, new_dt), expected_changes)
                    self.assertEqual(new_dt["/"]["h3c,bridge-profile"], bk.PROFILE.encode() + b"\0")
                    self.assertEqual(new_dt["/chosen"]["bootargs"], bk.BOOTARGS)
                    for child in bk.DISABLED:
                        self.assertEqual(new_dt[child]["status"], b"disabled\0")
                    for node in old_dt:
                        if "qpic" in node or node.startswith("/soc/dp") or "ess-switch" in node:
                            self.assertEqual(old_dt[node], new_dt[node])
                with self.assertRaises(ValueError): bk.transform_kernel(output, source_sha)
                broken = bytearray(stock); broken[-1] ^= 1
                with self.assertRaises(ValueError): bk.transform_kernel(bytes(broken), source_sha)
                wrong_model = next(row[2] for row in INPUTS if row[2] != source_sha)
                with self.assertRaises(ValueError): bk.transform_kernel(stock, wrong_model)
                # Structural defense-in-depth: a synthetic trusted-registry
                # fault is confined to this test; production SHA pins do not change.
                changed = bk._FDT(stock, 4096)
                changed.set("/configurations/config@mp02.1", "fdt", b"fdt@mp03.1\0")
                unknown_layout = changed.bytes()
                with mock.patch.dict(bk.SOURCES, {source_sha: (bk.SOURCES[source_sha][0], bk.sha(unknown_layout))}):
                    with self.assertRaisesRegex(ValueError, "config references"):
                        bk.transform_kernel(unknown_layout, source_sha)
                reports.append(report)
        report_path = os.environ.get("BB_MULTI_REPORT")
        if report_path:
            Path(report_path).write_text(json.dumps({"reports": reports, "missing_inputs": missing,
                "hardware": "NOT_EXECUTED", "installation": "NOT_QUALIFIED"}, indent=2) + "\n")
        if missing:
            self.skipTest("official ordinary inputs absent here: " + ",".join(missing))
        self.assertEqual(len(reports), 4)


if __name__ == "__main__": unittest.main()
