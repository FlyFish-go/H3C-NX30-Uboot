# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import re
import unittest

from tools import payload_patch_codegen as patchgen


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def candidate(candidate_id, payload, board="C", profile="h3c-oem-efba-128m-2k-sp64",
              load=4, storage=4, snapshot="a" * 64):
    return {
        "board": board,
        "candidate_id": candidate_id,
        "nand_profile_id": profile,
        "load_ecc_bits": load,
        "storage_ecc_bits": storage,
        "source_snapshot_sha256": snapshot,
        "profile": "release",
        "files": {"appsbl.partition.bin": {
            "path": f"payloads/{candidate_id}/appsbl.partition.bin",
            "bytes": len(payload), "sha256": sha(payload)}},
    }


class PayloadPatchCodegenTests(unittest.TestCase):
    def setUp(self):
        self.base = bytearray(b"\xff" * patchgen.APPSBL_BYTES)
        self.target = bytearray(self.base)
        self.base_desc = candidate("base-C-efba", bytes(self.base))
        self.target_desc = candidate("fresh-C-other", bytes(self.target),
                                      profile="h3c-oem-gd5f-128m-2k-sp128")

    def test_sorted_diff_ranges_merge_only_gaps_at_most_eight(self):
        for offset, value in ((10, 0x11), (19, 0x22), (40, 0x33)):
            self.target[offset] = value
        self.target_desc = candidate("fresh-C-other", bytes(self.target),
            profile="h3c-oem-gd5f-128m-2k-sp128")
        header, meta = patchgen.generate_patch_header(
            self.base_desc, bytes(self.base), self.target_desc, bytes(self.target))
        self.assertEqual(meta["patch_count"], 2)
        self.assertEqual(meta["patch_bytes"], 11)
        self.assertEqual(meta["patch_max_range_bytes"], 10)
        self.assertIn("{10u, 10u, 0u}", header)
        self.assertIn("{40u, 1u, 10u}", header)
        self.assertIn("BI_PAYLOAD_BASE_BYTES 1310720u", header)
        self.assertIn(f'BI_PAYLOAD_PATCH_FINAL_SHA256 "{sha(self.target)}"', header)
        self.assertIn(f'BI_PAYLOAD_BASE_DESCRIPTOR_SHA256 "{meta["base_descriptor_sha256"]}"', header)
        self.assertEqual(meta["reconstructed_sha256"], sha(self.target))

    def test_identical_images_emit_safe_zero_patch_table(self):
        header, meta = patchgen.generate_patch_header(
            self.base_desc, bytes(self.base), self.target_desc, bytes(self.target))
        self.assertEqual(meta["patch_count"], 0)
        self.assertEqual(meta["patch_bytes"], 0)
        self.assertIn("#define BI_PAYLOAD_PATCH_COUNT 0u", header)
        self.assertIn("{0u, 0u, 0u}", header)
        self.assertIn("#define BI_PAYLOAD_PATCH_DATA_BYTES 0u", header)

    def test_accepts_exact_ecc8_target_from_ecc4_common_base(self):
        target_ecc8 = candidate("fresh-C-ecc8", bytes(self.target),
            profile="h3c-oem-gd5f-128m-2k-sp128", load=8, storage=8)
        header, metadata = patchgen.generate_patch_header(
            self.base_desc, bytes(self.base), target_ecc8, bytes(self.target))
        self.assertIn("BI_PAYLOAD_PATCH_LOAD_ECC_BITS 8u", header)
        self.assertIn("BI_PAYLOAD_PATCH_STORAGE_ECC_BITS 8u", header)
        self.assertEqual(metadata["base_storage_ecc_bits"], 4)
        self.assertEqual(metadata["candidate_storage_ecc_bits"], 8)

    def test_rejects_wrong_pins_geometry_and_identity(self):
        bad = bytearray(self.target)
        bad[0] ^= 1
        with self.assertRaisesRegex(patchgen.PayloadPatchError, "descriptor"):
            patchgen.generate_patch_header(self.base_desc, bytes(self.base),
                self.target_desc, bytes(bad))
        for changes in ({"board": "D"}, {"source_snapshot_sha256": "b" * 64},
                        {"load_ecc_bits": 8}, {"storage_ecc_bits": 8}):
            with self.subTest(changes=changes), self.assertRaises(patchgen.PayloadPatchError):
                patchgen.generate_patch_header(self.base_desc, bytes(self.base),
                    dict(self.target_desc, **changes), bytes(self.target))
        with self.assertRaisesRegex(patchgen.PayloadPatchError, "base must be"):
            patchgen.generate_patch_header(
                dict(self.base_desc, nand_profile_id="wrong-profile"), bytes(self.base),
                self.target_desc, bytes(self.target))
        with self.assertRaisesRegex(patchgen.PayloadPatchError, "candidate source candidate"):
            patchgen.generate_patch_header(self.base_desc, bytes(self.base),
                dict(self.target_desc, load_ecc_bits=8), bytes(self.target))
        with self.assertRaisesRegex(patchgen.PayloadPatchError, "exactly 0x140000"):
            patchgen.generate_patch_header(self.base_desc, bytes(self.base[:-1]),
                self.target_desc, bytes(self.target[:-1]))

    def test_merge_gap_is_bounded(self):
        with self.assertRaisesRegex(patchgen.PayloadPatchError, "merge_gap"):
            patchgen.generate_patch_header(self.base_desc, bytes(self.base),
                self.target_desc, bytes(self.target), merge_gap=65)


if __name__ == "__main__":
    unittest.main(verbosity=2)
