"""Exact first-install record selection from explicitly supplied artifacts."""
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tarfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
import build
import audit


class DLoaderRecordTests(unittest.TestCase):
    PUBLIC_MEMBER = "abcd-uboot-public-review-20261016/payloads/HW-D/appsbl.partition.bin"
    CHECKSUMS_MEMBER = "abcd-uboot-public-review-20261016/source/SHA256SUMS"
    PROVENANCE_MEMBER = "abcd-uboot-public-review-20261016/source/SOURCE-PROVENANCE.json"

    def public_fixture(self):
        archive_name = os.environ.get("BOOTBRIDGE_PUBLIC_ARCHIVE")
        report_name = os.environ.get("BOOTBRIDGE_D_PUBLIC_REPORT")
        if not archive_name or not report_name:
            self.skipTest("set BOOTBRIDGE_PUBLIC_ARCHIVE and BOOTBRIDGE_D_PUBLIC_REPORT for public-D record tests")
        archive = Path(archive_name).resolve(strict=True)
        report_path = Path(report_name).resolve(strict=True)
        with tarfile.open(archive, "r:gz") as tf:
            payload = tf.extractfile(self.PUBLIC_MEMBER).read()
            checksums = tf.extractfile(self.CHECKSUMS_MEMBER).read()
            provenance = tf.extractfile(self.PROVENANCE_MEMBER).read()
        report = json.loads(report_path.read_text())
        return (payload, report, hashlib.sha256(checksums).hexdigest(),
                hashlib.sha256(provenance).hexdigest(), hashlib.sha256(archive.read_bytes()).hexdigest())

    def canonical_fixture(self):
        payload_name = os.environ.get("BOOTBRIDGE_D_5F_PAYLOAD")
        report_name = os.environ.get("BOOTBRIDGE_D_5F_REPORT")
        if not payload_name or not report_name:
            self.skipTest("set BOOTBRIDGE_D_5F_PAYLOAD and BOOTBRIDGE_D_5F_REPORT for canonical D record test")
        payload = Path(payload_name).resolve(strict=True).read_bytes()
        report = json.loads(Path(report_name).resolve(strict=True).read_text())
        return payload, report

    def test_public_trial_selects_exact_record_in_builder_and_independent_audit(self):
        payload, report, source_sha, provenance_sha, archive_sha = self.public_fixture()
        self.assertEqual(archive_sha, build.D_PUBLIC_SOURCE_ARCHIVE_SHA)
        self.assertEqual(source_sha, build.D_PUBLIC_SOURCE_SHA)
        self.assertEqual(provenance_sha, build.D_PUBLIC_SOURCE_PROVENANCE_SHA)
        self.assertEqual(hashlib.sha256(payload).hexdigest(), build.D_PUBLIC_FIRST_INSTALL_SHA)
        expected = build.D_PUBLIC_FIRST_INSTALL_RECORD
        self.assertEqual(build.resolve_d_loader_record(payload, report), expected)
        self.assertEqual(audit.audit_d_loader_record(payload, report), expected)
        qualification = {"install_qualification_available": True,
                         "boot_chain_qualification": expected,
                         "catalog_sha256": report["nand_catalog_sha256"],
                         "supported": report["supported_nand_profile_ids"],
                         "load": report["appsbl_load_profiles"],
                         "storage": report["appsbl_storage_profiles"]}
        generated = build.generated_payload_header(
            PROJECT / "src/bridge_payload.h", payload, report, qualification)
        self.assertIn(f'#define BI_FIRST_INSTALL_RECORD "{expected}"', generated)
        self.assertIn("#define BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE 1", generated)

    def test_canonical_record_resolution_is_unchanged(self):
        payload, report = self.canonical_fixture()
        self.assertEqual(build.resolve_d_loader_record(payload, report), build.D_FIRST_INSTALL_RECORD)
        self.assertEqual(audit.audit_d_loader_record(payload, report), build.D_FIRST_INSTALL_RECORD)
        self.assertTrue(build.finite_d_loader_record(payload, report))

    def test_public_record_rejects_source_candidate_and_registry_mismatches(self):
        payload, original, _, _, _ = self.public_fixture()
        mutations = [
            ("source_sha256", "b1144b5a8b20ca543acd7278083b6e7246e21c5b66a8f7d8e6fa1645c7acd0c5"),
            ("source_digest_kind", "git-tree-sha256"),
            ("public_source_archive_sha256", "0" * 64),
            ("public_source_provenance_sha256", "0" * 64),
            ("mbn_sha256", "0" * 64),
            ("load_sha256", "0" * 64),
            ("build_profile", "debug"),
            ("appsbl_storage_ecc_bits", 8),
        ]
        for field, value in mutations:
            report = copy.deepcopy(original)
            report[field] = value
            report["boot_chain_qualification"] = "PASS"
            report["install_qualification_available"] = True
            with self.subTest(field=field):
                self.assertEqual(build.resolve_d_loader_record(payload, report), "UNAVAILABLE")
                self.assertEqual(audit.audit_d_loader_record(payload, report), "UNAVAILABLE")

        for field in ("release_identity", "known_releases"):
            report = copy.deepcopy(original)
            if field == "release_identity":
                report[field]["sequence"] = 1
            else:
                report[field][1]["sha256"] = "0" * 64
            self.assertEqual(build.resolve_d_loader_record(payload, report), "UNAVAILABLE")
            self.assertEqual(audit.audit_d_loader_record(payload, report), "UNAVAILABLE")

        self.assertEqual(build.resolve_d_loader_record(payload[:-1] + b"X", original), "UNAVAILABLE")

    def test_unknown_source_and_self_pass_claim_do_not_grant_without_fixtures(self):
        payload = b"unknown input" * 8192
        report = {"source_sha256": "0" * 64,
                  "boot_chain_qualification": "PASS",
                  "install_qualification_available": True,
                  "hardware_boot_or_write": "PASS"}
        self.assertEqual(build.resolve_d_loader_record(payload, report), "UNAVAILABLE")
        self.assertEqual(audit.audit_d_loader_record(payload, report), "UNAVAILABLE")

    def test_public_explanatory_fields_do_not_change_exact_record(self):
        payload, report, _, _, _ = self.public_fixture()
        report.pop("validation_reference", None)
        report.pop("artifact_generation_scope", None)
        self.assertEqual(build.resolve_d_loader_record(payload, report), build.D_PUBLIC_FIRST_INSTALL_RECORD)
        self.assertEqual(audit.audit_d_loader_record(payload, report), build.D_PUBLIC_FIRST_INSTALL_RECORD)


if __name__ == "__main__":
    unittest.main()
