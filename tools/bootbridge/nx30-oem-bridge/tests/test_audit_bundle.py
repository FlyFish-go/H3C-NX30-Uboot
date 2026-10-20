#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-only negative tests for source-bound bundle audit primitives."""

import sys
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import audit_bundle
import build_bundle
import build
import nand_schema
import candidate_manifest


class BundleAuditPrimitiveTests(unittest.TestCase):
    def test_full_archive_and_source_runtime_matrices_are_distinct(self):
        catalog_rows = {p["profile_id"]: p for p in nand_schema.profiles()["profiles"]}
        boards = ("A", "B", "C", "D")
        audited = audit_bundle.archive_candidate_format_matrix(catalog_rows, boards)
        self.assertEqual(audited, build_bundle.candidate_archive_format_set(catalog_rows, boards))
        self.assertEqual(len(audited), 84)
        source_sha = "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44"
        variants = ("A0", "C")
        table_mask = sum(nand_schema.TABLE_MASKS[v] for v in variants)
        _, profile_ids = nand_schema.profile_set_id(source_sha, variants)
        eligible = set()
        for profile_id in profile_ids:
            profile = catalog_rows[profile_id]
            for ecc, bit, source_key in ((4, 1, "appsbl_ecc4_source_mask"),
                                         (8, 2, "appsbl_ecc8_source_mask")):
                if (profile["appsbl_ecc_mode_mask"] & bit and profile[source_key] & table_mask and
                        profile["runtime_ecc_mode_mask"] & bit and profile["kernel_ecc_mode_mask"] & bit):
                    eligible.update((board, profile_id, ecc, ecc) for board in boards)
        self.assertEqual(len(eligible), 68)
        self.assertEqual(len(audited - eligible), 16)

    def facts(self, board):
        profile = build.PUBLIC_BOARD_INPUTS[board]
        return {
            "official_sha256": "a" * 64,
            "route": profile["route"],
            "compatible": {"A": "qcom,ipq5018-mp03.1", "B": "qcom,ipq5018-mp02.1",
                           "C": "qcom,ipq5018-mp03.1-c2", "D": "qcom,ipq5018-mp02.1"}[board],
            "revision_sha256": "b" * 64,
            "stock_kernel_sha256": "c" * 64,
            "bridge_kernel_sha256": "d" * 64,
            "nand_dt_sha256": "e" * 64,
        }

    def test_route_metadata_requires_exact_actual_route(self):
        expected = self.facts("C")
        row = {
            "source_sha256": expected["official_sha256"],
            "route": expected["route"],
            "route_compatible": expected["compatible"],
            "source_revision_sha256": expected["revision_sha256"],
            "stock_kernel_sha256": expected["stock_kernel_sha256"],
            "bridge_kernel_sha256": expected["bridge_kernel_sha256"],
            "nand_dt_sha256": expected["nand_dt_sha256"],
            "return_source": expected,
        }
        audit_bundle._compare_profile_row(row, expected, "C")
        row["route"] = "config@mp02.1"
        with self.assertRaises(audit_bundle.BundleAuditError):
            audit_bundle._compare_profile_row(row, expected, "C")

    def test_full_nand_tuple_distinguishes_same_route_b_and_d(self):
        b = build.PUBLIC_BOARD_INPUTS["B"]
        d = build.PUBLIC_BOARD_INPUTS["D"]
        observed = {
            "model": b["model"], "manufacturer_id": b["manufacturer"],
            "chip_id": b["chip"], "capacity_mib": 128, "page_bytes": 2048,
            "erase_bytes": 131072, "raw_spare_bytes": b["raw_spare"],
            "mtd_oob_bytes": b["mtd_oob"], "ecc_bits": b["ecc"],
            "jedec_bytes_observed": 2,
        }
        audit_bundle._compare_nand_tuple(observed, b, "B")
        with self.assertRaises(audit_bundle.BundleAuditError):
            audit_bundle._compare_nand_tuple(observed, d, "D")

    def test_unsafe_paths_are_rejected(self):
        for path in ("../etc/shadow", "/tmp/escape", "foo\\bar", "a/./b", "a//b"):
            with self.subTest(path=path), self.assertRaises(audit_bundle.BundleAuditError):
                audit_bundle._safe_relpath(path, "fixture")

    def test_stock_ubi_selection_ignores_other_firmware_components(self):
        nodes = {
            "/images/sbl1-abc": {"data": b"sbl", "type": b"firmware\0"},
            "/images/ddr-abc": {"data": b"ddr", "type": b"firmware\0"},
            "/images/ubi-abc": {"data": b"ubi", "type": b"firmware\0"},
            "/images/script": {"data": b"script", "type": b"script\0"},
        }
        self.assertEqual(audit_bundle._stock_ubi_rows(nodes),
                         [("/images/ubi-abc", b"ubi")])
        nodes["/images/ubi-extra"] = {"data": b"extra", "type": b"firmware\0"}
        self.assertEqual(len(audit_bundle._stock_ubi_rows(nodes)), 2)

    def test_rootfs_image_may_have_only_erased_ubi_volume_padding(self):
        image = b"hsqs-image"
        self.assertTrue(audit_bundle._squashfs_volume_matches_image(
            image + b"\xff" * 16, image))
        self.assertFalse(audit_bundle._squashfs_volume_matches_image(
            image + b"\xff\x00" + b"\xff" * 14, image))
        self.assertFalse(audit_bundle._squashfs_volume_matches_image(image[:-1], image))

    def test_numeric_squashfs_listing_requires_exact_declared_console_node(self):
        listing = ("drwxr-xr-x 0/0 173 2026-10-14 00:00 squashfs-root\n"
                   "drwxr-xr-x 0/0 30 2026-10-14 00:00 squashfs-root/dev\n"
                   "crw------- 0/0 5, 1 2026-10-14 00:00 squashfs-root/dev/console\n"
                   "-rw-r--r-- 0/0 12 2026-10-14 00:00 squashfs-root/etc/config\n"
                   "-rwsr-xr-x 0/0 12 2026-10-14 00:00 squashfs-root/usr/bin/suid\n")
        parsed = audit_bundle._parse_squashfs_listing(listing)
        self.assertEqual(parsed["dev/console"], {"path": "dev/console", "type": "char",
                                                  "mode": 0o600, "major": 5, "minor": 1})
        self.assertEqual(parsed["etc/config"]["mode"], 0o644)
        self.assertEqual(parsed["."]["mode"], 0o755)
        self.assertEqual(parsed["usr/bin/suid"]["mode"], 0o4755)
        for bad in (listing.replace("5, 1", "5, 2"),
                    listing + "crw------- 0/0 1, 3 2026-10-14 00:00 squashfs-root/dev/null\n",
                    listing.replace("0/0 12", "1000/1000 12")):
            with self.subTest(bad=bad), self.assertRaises(audit_bundle.BundleAuditError):
                audit_bundle._parse_squashfs_listing(bad)

    def test_compiler_alias_must_resolve_to_the_pinned_executable(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("host compiler unavailable")
        resolved = str(Path(cc).resolve())
        self.assertTrue(audit_bundle._same_executable(cc, resolved))
        with tempfile.TemporaryDirectory() as td:
            alias = Path(td) / "compiler-alias"
            alias.symlink_to(resolved)
            self.assertTrue(audit_bundle._same_executable(str(alias), resolved))
            impostor = Path(td) / "other-compiler"
            impostor.write_text("not the pinned compiler")
            self.assertFalse(audit_bundle._same_executable(str(impostor), resolved))

    def test_source_revision_binding_is_recomputed_from_exact_candidate_descriptor(self):
        descriptor = {
            "board": "C", "candidate_id": "candidate-c",
            "source_revision": "1" * 40,
            "files": {
                "appsbl.mbn": {"sha256": "a" * 64},
                "appsbl.partition.bin": {"sha256": "b" * 64},
            },
        }
        binding, digest = audit_bundle._source_revision_binding(descriptor, "C")
        self.assertEqual(binding["archive_sha256"], candidate_manifest.ARCHIVE_SHA256)
        self.assertEqual(binding["candidate_id"], "candidate-c")
        self.assertEqual(len(digest), 64)
        tampered = dict(descriptor)
        tampered["files"] = dict(descriptor["files"])
        tampered["files"]["appsbl.partition.bin"] = {"sha256": "c" * 64}
        _, changed = audit_bundle._source_revision_binding(tampered, "C")
        self.assertNotEqual(digest, changed)
        with self.assertRaises(audit_bundle.BundleAuditError):
            audit_bundle._source_revision_binding(descriptor, "B")

    def test_provider_install_mode_is_exactly_nonwritable_0555(self):
        self.assertEqual(audit_bundle.PROVIDER_MODES, (0o555,))
        self.assertEqual(audit_bundle.DISPATCHER_MODES, (0o555,))

    def test_provider_stage_rebuild_command_uses_production_source_order(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("host compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="provider-stage-command-") as td:
            root = Path(td)
            source = root / "src"
            include = root / "include"
            source.mkdir()
            include.mkdir()
            for index, name in enumerate(audit_bundle.PROVIDER_STAGE_SOURCES):
                body = "int main(void) { return 0; }\n" if index == 0 else f"int unit_{index};\n"
                (source / name).write_text(body)
            output = root / "provider"
            command = audit_bundle._provider_stage_command(compiler, include, source, output)
            source_start = command.index(str(source / audit_bundle.PROVIDER_STAGE_SOURCES[0]))
            self.assertEqual(command[source_start:-2], [str(source / name)
                for name in audit_bundle.PROVIDER_STAGE_SOURCES])
            self.assertEqual(command[1:10], ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Os",
                "-static", "-s", "-fno-ident", "-Wl,--build-id=none"])
            subprocess.run(command, check=True, capture_output=True, timeout=60)
            self.assertTrue(output.is_file())

    def test_allowed_provider_sets_are_bound_to_exact_official_sources(self):
        nx_hash = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"
        self.assertEqual(audit_bundle.SUPPORTED_SOURCE_CLASSES[nx_hash], ("A", "B", "C", "D"))
        rows = dict(audit_bundle.SUPPORTED_SOURCE_IDENTITIES[nx_hash])
        self.assertEqual(rows["NX30"], ("D",))
        self.assertEqual(rows["RX3000"], ("A", "B", "C", "D"))

    def test_source_identity_version_and_product_are_bound_to_official_token(self):
        source = "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b"
        identity = {"identity": "RT3000V100R015", "model": "RT3000",
                    "sha256": source, "bytes": 1234}
        report = {
            "official": {"version": "RT3000V100R015"},
            "source_identity": {"header_product": "RT3000", "version": "RT3000V100R015",
                                "sha256": source, "bytes": 1234},
            "supported_product_identities": [{"product": "RT3000", "source_sha256": source,
                                              "hardware_classes": ["A", "B", "C", "D"]}],
        }
        audit_bundle._validate_source_identity(report, identity, Path(__file__).resolve().parents[3])
        report["supported_product_identities"] = [{"product": "RX3000", "source_sha256": source,
                                                   "hardware_classes": ["A", "B", "C", "D"]}]
        with self.assertRaises(audit_bundle.BundleAuditError):
            audit_bundle._validate_source_identity(report, identity, Path(__file__).resolve().parents[3])

    def test_nx_source_can_bind_rx_classes_without_claiming_nx_a_to_c(self):
        source = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"
        identity = {"identity": "NX30V100R012", "model": "NX30", "sha256": source, "bytes": 10}
        report = {
            "official": {"version": "NX30V100R012"},
            "source_identity": {"header_product": "NX30", "version": "NX30V100R012",
                                "sha256": source, "bytes": 10},
            "supported_product_identities": [
                {"product": "NX30", "source_sha256": source, "hardware_classes": ["D"]},
                {"product": "RX3000", "source_sha256": source,
                 "hardware_classes": ["A", "B", "C", "D"],
                 "identity_evidence": dict(audit_bundle.SOURCE_IDENTITY_EVIDENCE[(source, "RX3000")] )},
            ],
        }
        audit_bundle._validate_source_identity(report, identity, Path(__file__).resolve().parents[3])
        self.assertEqual(dict(audit_bundle.SUPPORTED_SOURCE_IDENTITIES[source])["NX30"], ("D",))

    def test_runtime_header_projection_includes_provider_size(self):
        row = {
            "record_id": "record", "hardware_class": "A", "source_sha256": "a" * 64,
            "product": "RC3000", "route": "config@mp03.1",
            "route_compatible": "qcom,ipq5018-mp03.1",
            "source_revision_sha256": "b" * 64, "stock_kernel_sha256": "c" * 64,
            "bridge_kernel_sha256": "d" * 64, "nand_dt_sha256": "e" * 64,
            "nand": {"model": "NAND", "jedec_bytes_observed": 2,
                "manufacturer_id": 0xef, "chip_id": 0xba, "capacity_mib": 128,
                "page_bytes": 2048, "erase_bytes": 131072, "raw_spare_bytes": 64,
                "mtd_oob_bytes": 64, "ecc_bits": 4},
        }
        actual = audit_bundle._header_row(
            row, {"path": "/usr/sbin/bridge-check-HW-A", "sha256": "f" * 64,
                  "bytes": 123, "mode": 0o755},
            {"path": "/etc/bridge/providers/HW-A/uboot.bin", "sha256": "1" * 64,
             "bytes": 0x140000, "mode": 0o400})
        self.assertEqual(actual["provider_bytes"], 123)
        self.assertEqual(actual["payload_bytes"], 0x140000)

    def test_generated_c_header_is_compiled_and_records_are_read_from_abi(self):
        source = "a" * 64
        profile = {"profile_id": "gd5f1gq5reyig-128m-sp128-ecc8",
            "model_aliases": ["GD5F1GQ5REYIG"], "jedec_bytes_observed": 2,
            "manufacturer_id": 0xc8, "chip_id": 0x41, "capacity_mib": 128,
            "page_bytes": 2048, "erase_bytes": 131072, "raw_spare_bytes": 128,
            "mtd_oob_bytes": 128, "ecc_bits": 8, "catalogue_eligible": 2}
        row = {"record_id": "trial-record", "candidate_id": "trial-candidate",
            "hardware_class": "B", "source_sha256": source, "product": "RT3000",
            "route": "config@mp02.1", "route_compatible": "qcom,ipq5018-mp02.1",
            "source_revision_sha256": "b" * 64, "stock_kernel_sha256": "c" * 64,
            "bridge_kernel_sha256": "d" * 64, "nand_dt_sha256": "e" * 64,
            "profile_set_id": "nandset-rt3000-reviewed-v2",
            "candidate_set_id": "candset-0123456789abcdef", "nand_profiles": [profile],
            "provider_path": "/usr/sbin/bridge-candidates/c-0123456789abcdef01234/provider",
            "provider_sha256": "f" * 64, "provider_bytes": 777,
            "payload_path": "/etc/bridge/providers/c-0123456789abcdef01234/uboot.bin",
            "payload_sha256": "1" * 64, "payload_bytes": 0x140000}
        header = build.generated_provider_bundle_header(source, "RT3000", [row])
        probe = r'''#include <stdio.h>
#include "bridge_provider_bundle.h"
int main(void) {
  const bdi_provider_record *r=&bootbridge_provider_records[0];
  const bdi_nand_profile *n=&r->nand_profiles[0];
  printf("%u\t%u\t%s\t%s\t%s\t%s\t%s\t%zu\t%s\t%u\n",
    BI_PROVIDER_BUNDLE_ABI_VERSION, BI_PROVIDER_COUNT, r->record_id,
    r->candidate_id, r->hardware_class, r->profile_set_id,
    r->candidate_set_id, r->nand_profile_count, n->profile_id,
    n->catalogue_eligible);
  return 0;
}'''
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "bridge_provider_bundle.h").write_text(header, encoding="ascii")
            source_file = root / "probe.c"
            source_file.write_text(probe, encoding="ascii")
            binary = root / "probe"
            project = Path(__file__).resolve().parents[1]
            cc = os.environ.get("HOSTCC", "cc")
            compiler = Path(shutil.which(cc)).resolve()
            subprocess.run([str(compiler), "-std=c99", "-Wall", "-Wextra", "-Werror",
                "-I", str(root), "-I", str(project / "src"), str(source_file), "-o", str(binary)],
                check=True, capture_output=True)
            output = subprocess.run([str(binary)], check=True, capture_output=True,
                text=True).stdout.strip().split("\t")
            self.assertEqual(output, ["2", "1", "trial-record", "trial-candidate", "B",
                "nandset-rt3000-reviewed-v2", "candset-0123456789abcdef", "1",
                profile["profile_id"], "2"])


if __name__ == "__main__":
    unittest.main()
