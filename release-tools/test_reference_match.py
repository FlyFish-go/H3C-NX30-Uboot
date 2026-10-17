#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Safety tests plus opt-in comparison against local canonical artifacts."""

import hashlib
import os
import pathlib
import struct
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from appsbl_packager import PackError, package_elf, padded_partition, write_outputs  # noqa: E402

REFERENCE_ROOT = os.environ.get("APPSBL_REFERENCE_ROOT")
EXPECTED = {
    "A": ("8735e7e0e986c2f76c5f65199c9115babaa54ad4d1e296fe02e93879d0f2cd3d",
          "a4604ecde101dd8368b1022a3ca0d819494e2a53c338f6f5c64d7129d14e6a0b"),
    "B": ("d576a16e21db2554600a310a2ed02b1b65242b9a45108b7bed23d202a40daee3",
          "0a9b9dce5f64f4c6d33f1e9e4007cea5c7282ea23d63cfe3c5e70eea4980f32a"),
    "C": ("aadd23981cc001112b9a56e7ba3bb4dcdd86ed7a4ae6744a0646fefb014df66e",
          "2297b54c99540a105664cd1e5456eeea22b02563fa669b55a86d39eee733ee93"),
    "D": ("7545523a9b52ce2cd469599a20804bcacdc6c602c97d5423c6433ec232505ff0",
          "61b10ffdec0af3101657bccd5f4425ca4e8a8e6dc62dd2e8d5f8d08e73bc3595"),
}


def synthetic_elf(load: bytes = b"portable-test-load") -> bytes:
    ident = b"\x7fELF\x01\x01\x01" + b"\x00" * 9
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 3, 40, 1, 0x4A920000,
                         52, 0, 0, 52, 32, 2, 0, 0, 0)
    phdrs = struct.pack("<IIIIIIII", 1, 0x80, 0x4A920000, 0x4A920000,
                        len(load), len(load), 7, 32)
    phdrs += struct.pack("<IIIIIIII", 0x6474E551, 0, 0, 0, 0, 0, 7, 0x10)
    return header + phdrs + b"\0" * (0x80 - len(header) - len(phdrs)) + load


class PackagerRegression(unittest.TestCase):
    @unittest.skipUnless(REFERENCE_ROOT, "set APPSBL_REFERENCE_ROOT to the local payload evidence root")
    def test_all_four_outputs_match_canonical_bytes(self):
        payloads = pathlib.Path(REFERENCE_ROOT)
        inputs = payloads / "input/evidence/abcd-release-20261016/baseline-build1"
        golden = payloads / "candidate-bundle"
        for board, (expected_mbn_sha, expected_partition_sha) in EXPECTED.items():
            with self.subTest(board=board):
                elf_path = next((inputs / board).glob("*.strip.elf"))
                mbn = package_elf(elf_path.read_bytes())
                partition = padded_partition(mbn)
                golden_dir = golden / f"HW-{board}"
                golden_mbn = (golden_dir / "appsbl.mbn").read_bytes()
                golden_partition = (golden_dir / "appsbl.partition.bin").read_bytes()
                self.assertEqual(mbn, golden_mbn)
                self.assertEqual(partition, golden_partition)
                self.assertEqual(hashlib.sha256(mbn).hexdigest(), expected_mbn_sha)
                self.assertEqual(hashlib.sha256(partition).hexdigest(), expected_partition_sha)
                self.assertEqual(len(partition), 0x140000)
                self.assertTrue(partition[len(mbn):] and set(partition[len(mbn):]) == {0xFF})

    def test_rejects_truncated_or_non_arm_input(self):
        with self.assertRaises(PackError):
            package_elf(b"\x7fELF")
        source = synthetic_elf()
        bad = bytearray(source)
        bad[18:20] = b"\x03\x00"  # EM_386
        with self.assertRaises(PackError):
            package_elf(bytes(bad))

    def test_rejects_truncated_load_and_nonempty_bss(self):
        source = synthetic_elf()
        with self.assertRaises(PackError):
            package_elf(source[:-1])
        bad = bytearray(source)
        old_memsz = struct.unpack_from("<I", source, 52 + 20)[0]
        bad[52 + 20:52 + 24] = (old_memsz + 1).to_bytes(4, "little")
        with self.assertRaises(PackError):
            package_elf(bytes(bad))

    def test_safe_exclusive_output_paths(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            source = root / "input.elf"
            source.write_bytes(synthetic_elf())
            before = source.read_bytes()

            with self.assertRaises(PackError):
                write_outputs(source, source)
            self.assertEqual(source.read_bytes(), before)

            existing = root / "existing.mbn"
            existing.write_bytes(b"keep")
            with self.assertRaises(PackError):
                write_outputs(source, existing)
            self.assertEqual(existing.read_bytes(), b"keep")

            link = root / "output-link.mbn"
            link.symlink_to(existing)
            with self.assertRaises(PackError):
                write_outputs(source, link)
            self.assertEqual(existing.read_bytes(), b"keep")

            same = root / "same.mbn"
            with self.assertRaises(PackError):
                write_outputs(source, same, same)
            self.assertFalse(same.exists())

            malformed = root / "malformed.elf"
            malformed.write_bytes(b"bad ELF")
            out_mbn, out_partition = root / "bad.mbn", root / "bad.partition"
            with self.assertRaises(PackError):
                write_outputs(malformed, out_mbn, out_partition)
            self.assertFalse(out_mbn.exists())
            self.assertFalse(out_partition.exists())

    def test_symlink_input_and_partial_output_rollback(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            source = root / "input.elf"
            source.write_bytes(synthetic_elf())
            source_link = root / "input-link.elf"
            source_link.symlink_to(source)
            with self.assertRaises(PackError):
                write_outputs(source_link, root / "out.mbn")
            self.assertFalse((root / "out.mbn").exists())

            blocked_parent = root / "not-a-directory"
            blocked_parent.write_text("block")
            first = root / "first.mbn"
            second = blocked_parent / "second.partition"
            with self.assertRaises(OSError):
                write_outputs(source, first, second)
            self.assertFalse(first.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
