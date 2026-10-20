#!/usr/bin/env python3
"""Negative tests against actual built bytes, enabled with explicit file inputs."""
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import audit


@unittest.skipUnless(os.environ.get("NX30_BRIDGE_CANDIDATE") and os.environ.get("NX30_BRIDGE_OFFICIAL") and
                     os.environ.get("NX30_BRIDGE_PAYLOAD_REPORT"),
                     "actual package and trusted external payload report not supplied")
class PackageTests(unittest.TestCase):
    def setUp(self):
        self.original = Path(os.environ["NX30_BRIDGE_OFFICIAL"])
        self.candidate = Path(os.environ["NX30_BRIDGE_CANDIDATE"])
        self.payload_report = Path(os.environ["NX30_BRIDGE_PAYLOAD_REPORT"])
        self.raw = bytearray(self.candidate.read_bytes())
        self.tmp = tempfile.TemporaryDirectory(prefix="nx30-negative-")
        self.modified = Path(self.tmp.name) / "candidate.bin"

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, checksum=False):
        if checksum:
            struct.pack_into(">I", self.raw, 72, 0)
            total = sum(w[0] for w in struct.iter_unpack(">I", self.raw[64:])) & 0xffffffff
            struct.pack_into(">I", self.raw, 72, (0xffffffff - total) & 0xffffffff)
        self.modified.write_bytes(self.raw)

    def test_finished_package_decodes_to_real_arm_programs(self):
        report = audit.audit(self.candidate, self.original, self.payload_report)
        self.assertEqual(report["status"], "PASS_OFFLINE_PACKAGE_AUDIT")
        self.assertEqual(len(report["arm_binaries"]), 3)
        self.assertTrue(report["original_kernel_bytes_preserved"])
        self.assertTrue(report["nand_catalog_sha256"])

    def test_bad_outer_checksum_rejected(self):
        self.raw[72] ^= 1
        self.write()
        with self.assertRaisesRegex(ValueError, "checksum"):
            audit.audit(self.modified, self.original, self.payload_report)

    def test_foreign_product_rejected_even_with_valid_checksum(self):
        self.raw[99:103] = b"RT30"
        self.write(checksum=True)
        with self.assertRaisesRegex(ValueError, "product/version"):
            audit.audit(self.modified, self.original, self.payload_report)

    def test_corrupt_fit_payload_rejected_even_with_valid_outer_checksum(self):
        # A byte near the middle is inside the UBI payload, not an FDT token.
        self.raw[len(self.raw) // 2] ^= 1
        self.write(checksum=True)
        with self.assertRaisesRegex(ValueError, "FIT integrity"):
            audit.audit(self.modified, self.original, self.payload_report)


if __name__ == "__main__":
    unittest.main()
