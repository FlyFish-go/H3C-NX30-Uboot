"""Offline parser tests for the pinned public ABCD candidate archive."""
import os
from pathlib import Path
import sys
import struct
import tarfile
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
import candidate_manifest as cm


class CandidateManifestTests(unittest.TestCase):
    def fixture(self):
        archive = os.environ.get("BOOTBRIDGE_PUBLIC_ARCHIVE")
        expected = os.environ.get("BOOTBRIDGE_PUBLIC_ARCHIVE_SHA256")
        if not archive or not expected:
            self.skipTest("set BOOTBRIDGE_PUBLIC_ARCHIVE and BOOTBRIDGE_PUBLIC_ARCHIVE_SHA256 for archive tests")
        return Path(archive).resolve(strict=True), expected

    def test_real_archive_produces_four_non_authorizing_descriptors(self):
        archive, expected = self.fixture()
        result = cm.read_public_candidates(archive, expected)
        self.assertEqual(set(result["boards"]), set("ABCD"))
        self.assertEqual(result["archive_sha256"], cm.ARCHIVE_SHA256)
        self.assertEqual(result["installation_qualification"], "NOT_GRANTED_BY_CANDIDATE_PARSER")
        for board in "ABCD":
            item = result["boards"][board]
            self.assertEqual(item["board"], board)
            self.assertEqual(item["files"]["appsbl.partition.bin"]["bytes"], cm.PARTITION_BYTES)
            self.assertEqual(item["container"]["format"], "Qualcomm MBN-v3 normalized ELF32 little-endian ARM")
            self.assertEqual(item["container"]["entry"], cm.LOAD_ADDRESS)
            self.assertGreater(item["container"]["load"]["bytes"], 0)
            self.assertIsNone(item["container"]["bss"]["bytes"])
            self.assertEqual(item["hardware_validation"], "NOT_EXECUTED")
            self.assertEqual(item["installation_qualification"], "NOT_GRANTED_BY_CANDIDATE_PARSER")

    def test_damaged_cross_board_and_missing_candidate_material_reject(self):
        archive, expected = self.fixture()
        descriptors = cm.read_public_candidates(archive, expected)["boards"]
        a = descriptors["A"]
        with tarfile.open(archive, "r:gz") as tf:
            prefix = "abcd-uboot-public-review-20261016/payloads/HW-B/"
            b_mbn = tf.extractfile(prefix + "appsbl.mbn").read()
            b_partition = tf.extractfile(prefix + "appsbl.partition.bin").read()
        with self.assertRaisesRegex(cm.CandidateError, "length or SHA-256"):
            cm.validate_candidate_material("A", a, b_mbn, b_partition)
        with self.assertRaisesRegex(cm.CandidateError, "length or SHA-256"):
            cm.validate_candidate_material("A", a, b"X" + b"\0" * (a["files"]["appsbl.mbn"]["bytes"] - 1),
                                           b"\xff" * cm.PARTITION_BYTES)
        with self.assertRaisesRegex(cm.CandidateError, "missing"):
            cm.validate_candidate_material("A", a, None, b"\xff" * cm.PARTITION_BYTES)

        with tempfile.TemporaryDirectory(prefix="candidate-archive-damage-") as td:
            damaged = Path(td) / "damaged.tar.gz"
            damaged.write_bytes(archive.read_bytes() + b"X")
            with self.assertRaisesRegex(cm.CandidateError, "outer SHA-256"):
                cm.read_public_candidates(damaged, expected)

    def test_pure_parser_rejects_malformed_mbn_without_fixtures(self):
        for data in (b"", b"\0" * 0x4000, b"\x7fELF\x01\x01\x01" + b"\0" * 100):
            with self.subTest(length=len(data)), self.assertRaises(cm.CandidateError):
                cm.parse_mbn(data)

    def test_fresh_mbn_header_allowlist_is_paired_with_exact_elf(self):
        ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
        def header(kind, flags):
            return cm.ELF_HEADER.pack(ident, kind, 40, 1, cm.LOAD_ADDRESS,
                cm.ELF_HEADER.size, 0, flags, cm.ELF_HEADER.size,
                cm.PROGRAM_HEADER.size, 4, 40, 0, 0)
        for kind, flags, label in ((2, 0x05000200, "ET_EXEC"),
                                   (3, 0x05000202, "ET_DYN")):
            elf_header = header(kind, flags)
            mbn = bytearray(0x3001)
            mbn[:len(elf_header)] = elf_header
            self.assertEqual(cm.validate_fresh_elf_mbn_pair(elf_header, bytes(mbn)),
                             {"elf_type": kind, "elf_flags": flags})
            with self.assertRaisesRegex(cm.CandidateError, "Qualcomm metadata"):
                cm.parse_mbn(bytes(mbn), allow_fresh=True)
            if kind == 2:
                with self.assertRaisesRegex(cm.CandidateError, "reviewed profile"):
                    cm.parse_mbn(bytes(mbn))
        with self.assertRaisesRegex(cm.CandidateError, "do not match"):
            cm.validate_fresh_elf_mbn_pair(header(2, 0x05000200), header(3, 0x05000202))

    def test_fresh_checksum_inventory_allows_safe_punctuation(self):
        digest = "a" * 64
        rows = {"source/arm,scu@v2.txt": digest,
                "source/include/a+b.h": "b" * 64}
        raw = "".join(value + "  " + path + "\n" for path, value in sorted(rows.items())).encode("utf-8")
        self.assertEqual(cm._parse_candidate_sums(raw, "test"), rows)
        with self.assertRaisesRegex(cm.CandidateError, "not canonical"):
            cm._parse_candidate_sums(raw.replace(b"  source/", b" source/", 1), "test")
        bad = digest.encode("ascii") + b"  source/bad\nname.txt\n"
        with self.assertRaises(cm.CandidateError):
            cm._parse_candidate_sums(bad, "test")

    def test_pure_candidate_material_rejects_missing_members_without_fixtures(self):
        row = {"board": "A", "files": {
            "appsbl.mbn": {"path": "payloads/HW-A/appsbl.mbn", "bytes": 1, "sha256": "0" * 64},
            "appsbl.partition.bin": {"path": "payloads/HW-A/appsbl.partition.bin", "bytes": cm.PARTITION_BYTES,
                                     "sha256": "0" * 64},
        }}
        with self.assertRaisesRegex(cm.CandidateError, "missing"):
            cm.validate_candidate_material("A", row, None, b"\xff" * cm.PARTITION_BYTES)

    def test_v2_member_cache_rechecks_outer_and_cached_member_hashes(self):
        member = "payloads/HW-A/profile/load4-store4/appsbl.mbn"
        payload = b"cached-test-member"
        with tempfile.TemporaryDirectory(prefix="candidate-cache-test-") as td:
            root = Path(td)
            archive = root / "archive.tar.gz"
            with tarfile.open(archive, "w:gz") as tf:
                info = tarfile.TarInfo("h3c-uboot-candidates-v2/" + member)
                info.size = len(payload)
                import io
                tf.addfile(info, io.BytesIO(payload))
            archive_digest = cm.sha256(archive.read_bytes())
            cache_path = root / "cached-member"
            cache_path.write_bytes(payload)
            cache_path.chmod(0o600)
            item = {"path": member, "bytes": len(payload), "sha256": cm.sha256(payload)}
            descriptor = {"archive_sha256": archive_digest,
                          "candidate_id": "test-candidate", "files": {"appsbl.mbn": item}}
            key = (archive_digest, member)
            previous = cm._V2_MEMBER_CACHE.get(key)
            cm._V2_MEMBER_CACHE[key] = (dict(item), cache_path)
            try:
                self.assertEqual(cm.extract_candidate_member_v2(archive, descriptor, "appsbl.mbn"), payload)
                cache_path.write_bytes(b"tampered")
                with self.assertRaisesRegex(cm.CandidateError, "cached candidate member digest"):
                    cm.extract_candidate_member_v2(archive, descriptor, "appsbl.mbn")
                cache_path.write_bytes(payload)
                archive.write_bytes(archive.read_bytes() + b"changed")
                with self.assertRaisesRegex(cm.CandidateError, "archive changed"):
                    cm.extract_candidate_member_v2(archive, descriptor, "appsbl.mbn")
            finally:
                if previous is None:
                    cm._V2_MEMBER_CACHE.pop(key, None)
                else:
                    cm._V2_MEMBER_CACHE[key] = previous


if __name__ == "__main__":
    unittest.main()
