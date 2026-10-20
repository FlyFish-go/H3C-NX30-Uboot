import hashlib
import copy
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build


class BuildQualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = Path(os.environ.get("H3C_NAND_CATALOG_HEADER", ""))
        if not cls.catalog.is_file():
            raise RuntimeError("set H3C_NAND_CATALOG_HEADER to the authoritative shared catalog header")
        cls.profile = "h3c-oem-efba-128m-2k-sp64"
        cls.payload = b"offline-test-candidate" * (1 + 0x140000 // len(b"offline-test-candidate"))
        cls.payload = cls.payload[:0x140000]

    def report(self):
        return {
            "partition_sha256": hashlib.sha256(self.payload).hexdigest(),
            "release_identity": {"scope": "synthetic-d-ecc4", "sequence": 2,
                                 "candidate_sha256": hashlib.sha256(self.payload).hexdigest(),
                                 "candidate_bytes": 0x140000},
            "known_releases": [{"scope": "synthetic-d-ecc4", "sequence": 0,
                                "sha256": next(iter(build.REVIEWED_OEM_APPSBL)),
                                "bytes": 0x140000, "kind": "oem_baseline"}],
            "payload_version": "host-test-build",
            "base_commit": "a" * 40,
            "derivative_commit": "b" * 40,
            "nand_catalog_header_path": str(self.catalog),
            "nand_catalog_sha256": hashlib.sha256(self.catalog.read_bytes()).hexdigest(),
            "supported_nand_profile_ids": [self.profile],
            "appsbl_load_profiles": [self.profile],
            "appsbl_storage_profiles": [self.profile],
            "ecc_bits": 4,
            "appsbl_load_ecc_bits": 4,
            "appsbl_storage_ecc_bits": 4,
            "sbl_sha256": build.SBL_SHA,
            "sbl_valid_bytes": build.SBL_VALID_BYTES,
            "ip": "192.168.10.1",
            "candidate_service_ram_role": 0,
            "result": "PASS_OFFLINE_MBN_ROLE0",
            "target": {"size": "0x140000"},
            "network_changes_persistent_env": False,
            "hardware_boot_or_write": "NOT_EXECUTED",
        }

    def test_catalog_profile_sets_and_generated_header_are_bound(self):
        report = self.report()
        qualification = build.validate_payload_report(self.payload, report)
        header = build.generated_payload_header(
            Path(__file__).resolve().parents[1] / "src/bridge_payload.h",
            self.payload, report, qualification)
        self.assertIn(hashlib.sha256(self.payload).hexdigest(), header)
        self.assertIn(qualification["catalog_sha256"], header)
        self.assertIn(self.profile, header)
        self.assertIn("BV_OEM_BASELINE", header)
        self.assertIn("UINT64_C(2), BV_CUSTOM_RELEASE", header)
        self.assertNotIn("BI_ALLOWED_APPSBL", header)
        self.assertIs(qualification["install_qualification_available"], False)
        self.assertEqual(qualification["boot_chain_qualification"], "UNAVAILABLE")
        self.assertIn("#define BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE 0", header)
        self.assertIn('#define BI_FIRST_INSTALL_RECORD ""', header)
        # A report cannot self-certify missing physical qualification.
        report["install_qualification_available"] = True
        report["boot_chain_qualification"] = "PASS"
        again = build.validate_payload_report(self.payload, report)
        self.assertIs(again["install_qualification_available"], False)

    def test_missing_or_malformed_release_identity_is_rejected(self):
        mutations = [({}, None), ({"sequence": True}, None), ({"candidate_bytes": True}, None),
                     ({"candidate_bytes": 1}, None), ({"sequence": 0}, None),
                     ({"sequence": 1 << 64}, None), ({"candidate_sha256": "0" * 64}, None),
                     ({"scope": "bad scope"}, None), ({"unexpected": 1}, None)]
        for changes, _ in mutations:
            report = self.report()
            if not changes:
                del report["release_identity"]
            else:
                report["release_identity"].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                build.validate_release_identity(self.payload, report)

    def test_registry_conflicts_scope_oem_extension_and_unknown_fields_rejected(self):
        baseline = self.report()["known_releases"][0]
        custom = dict(baseline, kind="custom_release", sequence=1, sha256="1" * 64)
        cases = [[], [dict(baseline, extra=1)], [dict(baseline, sequence=True)],
                 [dict(baseline, bytes=True)], [dict(baseline, scope="other")],
                 [dict(baseline, sha256="2" * 64)], [baseline, baseline],
                 [baseline, dict(custom, sequence=2)], [baseline, custom, custom],
                 [baseline, dict(custom, sequence=0)], [baseline, dict(custom, sha256="0" * 64)],
                 [custom]]
        for records in cases:
            report = self.report(); report["known_releases"] = copy.deepcopy(records)
            with self.subTest(records=records), self.assertRaises(ValueError):
                build.validate_release_identity(self.payload, report)

    def test_valid_reviewed_old_and_new_releases_preserve_explicit_sequences(self):
        report = self.report(); base = report["known_releases"][0]
        report["known_releases"] += [dict(base, kind="custom_release", sequence=1, sha256="1" * 64),
                                      dict(base, kind="custom_release", sequence=3, sha256="3" * 64)]
        self.assertEqual([r["sequence"] for r in build.validate_release_identity(self.payload, report)], [0, 1, 3, 2])

    def test_duplicate_json_fields_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "duplicate payload report field"):
            build.payload_report_json('{"release_identity":{"sequence":1,"sequence":2}}')

    def test_generated_production_registry_and_empty_template_compile_real_policy(self):
        report = self.report(); baseline = report["known_releases"][0]
        report["known_releases"] += [dict(baseline, kind="custom_release", sequence=1, sha256="1" * 64),
                                      dict(baseline, kind="custom_release", sequence=3, sha256="3" * 64)]
        qualification = build.validate_payload_report(self.payload, report)
        source_root = Path(__file__).resolve().parents[1] / "src"
        template = source_root / "bridge_payload.h"
        generated = build.generated_payload_header(template, self.payload, report, qualification)
        installed = [baseline["sha256"], hashlib.sha256(self.payload).hexdigest(), "1" * 64, "3" * 64, "f" * 64]
        probe = '#include <stdio.h>\n#include <bridge_payload.h>\nint main(void){\n'
        for fingerprint in installed:
            probe += ('printf("%%d\\n",(int)bv_evaluate(BI_VERSION_REGISTRY,BI_VERSION_REGISTRY_COUNT,'
                      'BI_VERSION_SCOPE,BI_PAYLOAD_SHA256,BI_PAYLOAD_BYTES,"%s",BI_PAYLOAD_BYTES).decision);\n' % fingerprint)
        probe += 'return 0;}\n'
        with tempfile.TemporaryDirectory(prefix="bridge-version-header-") as td:
            temp = Path(td); (temp / "probe.c").write_text(probe)
            for header, expected in [(generated, [0, 1, 2, 3, 4]), (template.read_text(), [4] * 5)]:
                (temp / "bridge_payload.h").write_text(header)
                exe = temp / "probe"
                subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(temp),
                                "-I", str(source_root), str(temp / "probe.c"),
                                str(source_root / "bridge_version.c"), "-o", str(exe)], check=True)
                decisions = subprocess.check_output([str(exe)], text=True).splitlines()
                self.assertEqual(list(map(int, decisions)), expected)

    def test_stale_payload_digest_is_rejected(self):
        report = self.report()
        report["partition_sha256"] = "c7f9ee8d82cffe86c7bbd896e6f6d063c5658abf079d19fbb962341dd2b84246"
        with self.assertRaisesRegex(ValueError, "exact embedded APPSBL"):
            build.validate_payload_report(self.payload, report)

    def test_source_bound_256m_geometry_is_accepted(self):
        report = self.report()
        report["supported_nand_profile_ids"].append("h3c-oem-c822-256m-2k-sp64")
        report["appsbl_load_profiles"] = ["h3c-oem-c822-256m-2k-sp64"]
        report["appsbl_storage_profiles"] = ["h3c-oem-c822-256m-2k-sp64"]
        build.validate_payload_report(self.payload, report)


if __name__ == "__main__":
    unittest.main()
