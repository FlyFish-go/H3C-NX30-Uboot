import importlib.util
import hashlib
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("build_bundle", TOOLS / "build_bundle.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)
import build
import nand_schema


class BundleBuilderTests(unittest.TestCase):
    def test_archive_inventory_matrix_keeps_kernel_ineligible_rows(self):
        catalog_rows = {p["profile_id"]: p for p in nand_schema.profiles()["profiles"]}
        boards = ("A", "B", "C", "D")
        archive = bundle.candidate_archive_format_set(catalog_rows, boards)
        self.assertEqual(len(archive), 84)  # 21 U-Boot formats × four hardware classes
        source_sha = "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44"
        variants = ("A0", "C")
        table_mask = sum(nand_schema.TABLE_MASKS[v] for v in variants)
        _, profile_ids = nand_schema.profile_set_id(source_sha, variants)
        eligible = set()
        for profile_id in profile_ids:
            profile = catalog_rows[profile_id]
            for ecc, bit, field in ((4, 1, "appsbl_ecc4_source_mask"),
                                    (8, 2, "appsbl_ecc8_source_mask")):
                if (profile["appsbl_ecc_mode_mask"] & bit and profile[field] & table_mask and
                        profile["runtime_ecc_mode_mask"] & bit and profile["kernel_ecc_mode_mask"] & bit):
                    eligible.update((board, profile_id, ecc, ecc) for board in boards)
        self.assertEqual(len(eligible), 68)  # 17 source-runtime profiles × four boards
        self.assertEqual(len(archive - eligible), 16)

    def identity(self, digest, product):
        return {"sha256": digest, "model": product}

    def test_exact_official_matrix_and_unknown_rejection(self):
        expected = {
            "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": ("A", "B", "C", "D"),
            "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": ("A", "B", "C", "D"),
            "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": ("A", "B", "C", "D"),
            "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922": ("A", "B", "C", "D"),
        }
        for digest, boards in expected.items():
            product = build.OFFICIAL_SOURCES[digest]["model"]
            self.assertEqual(bundle.source_boards({"sha256": digest, "model": product}), boards)
        with self.assertRaises(ValueError):
            bundle.source_boards({"sha256": "0" * 64, "model": "RX3000"})
        with self.assertRaises(ValueError):
            bundle.source_boards({"sha256": next(iter(expected)), "model": "RW3000"})

    def test_source_bundle_product_coverage_is_separate_from_source_model(self):
        nx_sha = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"
        self.assertEqual(bundle.source_boards({"sha256": nx_sha, "model": "NX30"}),
                         ("A", "B", "C", "D"))
        self.assertEqual(bundle.supported_product_identities(nx_sha), [
            {"product": "NX30", "source_sha256": nx_sha, "hardware_classes": ["D"]},
            {"product": "RX3000", "source_sha256": nx_sha,
             "hardware_classes": ["A", "B", "C", "D"],
             "identity_evidence": {"path": "source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json",
                 "bytes": 13544,
                 "sha256": "9834e1b2b92b59d4822905fc5ebe9096462e44106534b440598c85a67c2bf4c6"}}])
        with self.assertRaises(ValueError):
            bundle.supported_product_identities("0" * 64)

    def test_source_nand_profile_set_uses_exact_oem_tables_and_kernel_masks(self):
        digest = "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44"
        inventory = bundle.source_nand_profile_set({"sha256": digest})
        self.assertEqual(inventory["source_variants"], ["A0", "C"])
        self.assertEqual(len(inventory["profile_ids"]), 17)
        self.assertNotIn("h3c-oem-c881-128m-2k-sp128", inventory["profile_ids"])
        self.assertNotIn("h3c-oem-e521-128m-2k-sp128", inventory["profile_ids"])

    def test_shared_nx_source_d_alias_is_product_bound(self):
        nand_tuple = nand_schema.runtime_profiles(("A0", "C"), 8)[0]
        descriptor = {"build_profile": {"board": "D", "env_alias": "NX30_2i5x",
            "filter_env_root_keys": 1, "allow_missing_env_machid": 1}}
        nx = build.candidate_build_profile(descriptor, "D", "NX30", [nand_tuple])
        rx = build.candidate_build_profile(descriptor, "D", "RX3000", [nand_tuple])
        self.assertEqual(nx["env_alias"], "NX30_2i5x")
        self.assertIsNone(rx["env_alias"])
        self.assertEqual((rx["filter_env_root_keys"], rx["allow_missing_env_machid"]), (1, 1))

    def test_version_comes_from_header_identity_without_changing_identity_record(self):
        self.assertEqual(bundle.official_version({"identity": "RT3000V100R015", "model": "RT3000"}),
                         "RT3000V100R015")
        with self.assertRaises(ValueError):
            bundle.official_version({"identity": "NX30V100R012", "model": "RX3000"})

    def test_candidate_revision_digest_is_archive_and_board_bound(self):
        descriptor = {"board": "B", "candidate_id": "5f486f5f9852:HW-B:neutral-path-rebuild:offline",
            "source_revision": "5f486f5f9852c26d7d16eb5980a06cccea2a5217",
            "files": {"appsbl.mbn": {"sha256": "1" * 64},
                      "appsbl.partition.bin": {"sha256": "2" * 64}}}
        binding, digest = bundle.source_revision_binding(descriptor)
        self.assertEqual(len(digest), 64)
        self.assertEqual(binding["archive_sha256"], bundle.candidate_manifest.ARCHIVE_SHA256)
        other = dict(descriptor, board="C")
        _, other_digest = bundle.source_revision_binding(other)
        self.assertNotEqual(digest, other_digest)
        self.assertNotEqual(digest, descriptor["source_revision"])

    def test_bundle_rootfs_copy_does_not_mutate_child_build_tree(self):
        with tempfile.TemporaryDirectory(prefix="bootbridge-rootfs-copy-") as temp:
            root = Path(temp)
            source = root / "child-rootfs"
            source.mkdir()
            (source / "usr").mkdir()
            original = source / "usr" / "marker"
            original.write_bytes(b"child build")
            (source / "usr" / "link").symlink_to("marker")
            combined = bundle.copy_provider_rootfs(source, root / "combined-rootfs")
            (combined / "usr" / "dispatcher").write_bytes(b"bundle delta")
            self.assertEqual(original.read_bytes(), b"child build")
            self.assertFalse((source / "usr" / "dispatcher").exists())
            self.assertTrue((combined / "usr" / "link").is_symlink())

    def test_bundle_volume_check_allows_only_reviewed_kernel_transform(self):
        with tempfile.TemporaryDirectory(prefix="bootbridge-volume-check-") as temp:
            bridge_kernel = Path(temp) / "kernel.fit"
            bridge_kernel.write_bytes(b"reviewed transformed FIT")
            stock = {"kernel": b"stock FIT", "wifi_fw": b"wifi", "bt_fw": b"bt"}
            output = {"kernel": bridge_kernel.read_bytes(), "wifi_fw": b"wifi", "bt_fw": b"bt"}
            bundle.verify_bundle_ubi_volumes(output, stock, bridge_kernel)
            bad = dict(output, wifi_fw=b"changed")
            with self.assertRaisesRegex(ValueError, "wifi_fw"):
                bundle.verify_bundle_ubi_volumes(bad, stock, bridge_kernel)
            bad = dict(output, kernel=b"unreviewed")
            with self.assertRaisesRegex(ValueError, "bridge FIT"):
                bundle.verify_bundle_ubi_volumes(bad, stock, bridge_kernel)

    def test_bundle_file_retains_identical_readonly_file_without_copying(self):
        with tempfile.TemporaryDirectory(prefix="bootbridge-immutable-copy-") as temp:
            root = Path(temp)
            source = root / "source"
            target = root / "target"
            source.write_bytes(b"same immutable bytes")
            target.write_bytes(b"same immutable bytes")
            source.chmod(0o555)
            target.chmod(0o555)
            with mock.patch.object(bundle.shutil, "copyfile",
                                   side_effect=AssertionError("must not overwrite existing read-only file")):
                bundle.install_bundle_file(source, target, 0o555)
            self.assertEqual(target.stat().st_mode & 0o777, 0o555)
            self.assertEqual(target.read_bytes(), source.read_bytes())

    def test_bundle_file_rejects_mismatched_existing_file_and_copies_absent_file(self):
        with tempfile.TemporaryDirectory(prefix="bootbridge-bundle-copy-") as temp:
            root = Path(temp)
            source = root / "source"
            target = root / "nested" / "target"
            source.write_bytes(b"expected")
            target.parent.mkdir()
            target.write_bytes(b"different")
            target.chmod(0o555)
            with self.assertRaisesRegex(ValueError, "differs"):
                bundle.install_bundle_file(source, target, 0o555)
            target.unlink()
            bundle.install_bundle_file(source, target, 0o555)
            self.assertEqual(target.read_bytes(), b"expected")
            self.assertEqual(target.stat().st_mode & 0o777, 0o555)

    def test_outer_fit_uses_audit_wrapper_node_name(self):
        with tempfile.TemporaryDirectory(prefix="bootbridge-fit-node-") as temp:
            ubi = Path(temp) / "bridge.ubi"
            ubi.write_bytes(b"known UBI bytes")
            self.assertEqual(bundle.ubi_fit_node_name(ubi),
                "ubi-bridge-" + hashlib.sha1(b"known UBI bytes").hexdigest())

    def test_stage_rootfs_uses_explicit_bundle_provider_mode(self):
        with tempfile.TemporaryDirectory(prefix="bootbridge-stage-provider-") as temp:
            root = Path(temp)
            original, output, include = root / "stock", root / "staged", root / "include"
            original.mkdir()
            include.mkdir()

            def copy_fixture(_original, target_root, _stock_squashfs, rel):
                target = target_root / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"bound stock file")
                return {"fixture": rel}

            def compile_fixture(argv, _log):
                out_index = argv.index("-o") + 1
                target = Path(argv[out_index])
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"compiled " + target.name.encode())

            with mock.patch.object(build, "copy_stock_bound", side_effect=copy_fixture), \
                 mock.patch.object(build, "audit_elf_runtime"), \
                 mock.patch.object(build, "command", side_effect=compile_fixture):
                build.stage_rootfs(original, output, "cc", root / "logs", root / "stock.squashfs",
                    b"payload", None, include, runtime_metadata={},
                    provider_install_path="/usr/sbin/bridge-check-HW-B",
                    payload_install_path="/etc/bridge/providers/HW-B/uboot.bin",
                    include_payload_metadata=False, provider_executable_mode=0o555)
            provider = output / "usr/sbin/bridge-check-HW-B"
            self.assertEqual(provider.stat().st_mode & 0o777, 0o555)
            self.assertTrue((output / "etc/bridge/providers/HW-B/uboot.bin").is_file())

    def row(self, board="B"):
        p = build.PUBLIC_BOARD_INPUTS[board]
        profile_id = p["nand_profile_id"]
        board_mask = {"A":1,"B":2,"C":4,"D":8}[board]
        variant = next(x for x in nand_schema.runtime_profiles(["A0", "C"], board_mask)
                       if x["profile_id"] == profile_id and x["ecc_bits"] == p["ecc"])
        slug = "c-test-variant-" + board.lower()
        return {"record_id": "hw-" + board.lower() + "-" + profile_id + "-e" + str(p["ecc"]),
            "candidate_id": "candidate-v2-" + board + "-" + profile_id,
            "hardware_class": board,
            "source_sha256": "1" * 64, "product": "RT3000", "route": p["route"],
            "route_compatible": build.kernel_transform.ROUTES[p["route"]][1],
            "source_revision_sha256": "2" * 64, "stock_kernel_sha256": "3" * 64,
            "bridge_kernel_sha256": "4" * 64, "nand_dt_sha256": "5" * 64,
            "profile_set_id": "nandset-111111111111-0123456789abcdef",
            "candidate_set_id": "candset-0123456789abcdef", "nand_profiles": [variant],
            "provider_path": "/usr/sbin/bridge-candidates/" + slug + "/provider",
            "provider_sha256": "6" * 64, "provider_bytes": 4096,
            "payload_path": "/etc/bridge/providers/" + slug + "/uboot.bin",
            "payload_sha256": "7" * 64, "payload_bytes": 0x140000}

    def test_header_binds_full_runtime_row_and_allows_shared_route(self):
        b = self.row("B")
        c = self.row("D")
        c["product"] = b["product"]
        c["source_sha256"] = b["source_sha256"]
        header = build.generated_provider_bundle_header(b["source_sha256"], "RT3000", [b, c])
        self.assertIn("#define BI_PROVIDER_COUNT 2u", header)
        self.assertIn("bootbridge_provider_records[BI_PROVIDER_COUNT]", header)
        self.assertIn('"/usr/sbin/bridge-candidates/c-test-variant-b/provider"', header)
        self.assertIn('"/etc/bridge/providers/c-test-variant-d/uboot.bin"', header)
        # A content-addressed data-only base can serve multiple compiled patch
        # contracts when the exact physical file identity is identical.
        c["payload_path"] = "/etc/bridge/provider-payload-bases/" + "7" * 64 + ".bin"
        b["payload_path"] = c["payload_path"]
        shared = build.generated_provider_bundle_header(b["source_sha256"], "RT3000", [b, c])
        self.assertIn(c["payload_path"], shared)
        # Same route/compatible is legal: full runtime NAND facts distinguish records.
        d = self.row("B")
        with self.assertRaises(ValueError):
            build.generated_provider_bundle_header(b["source_sha256"], "RT3000", [b, d])
        bad = dict(b)
        bad["provider_bytes"] = 0
        with self.assertRaises(ValueError):
            build.generated_provider_bundle_header(b["source_sha256"], "RT3000", [bad])
        bad = dict(b)
        del bad["nand_dt_sha256"]
        with self.assertRaises(ValueError):
            build.generated_provider_bundle_header(b["source_sha256"], "RT3000", [bad])

    @unittest.skipUnless(shutil.which("cc"), "host C compiler unavailable")
    def test_dispatcher_compiles_with_generated_provider_abi(self):
        row = self.row("B")
        with tempfile.TemporaryDirectory(prefix="bootbridge-bundle-test-") as temp:
            root = Path(temp)
            include = root / "include"
            include.mkdir()
            (include / "bridge_provider_bundle.h").write_text(
                build.generated_provider_bundle_header(row["source_sha256"], row["product"], [row]))
            output = root / "bridge-check"
            meta = bundle.compile_dispatcher("cc", include, output, root / "compile.log")
            self.assertTrue(output.is_file())
            self.assertEqual(output.stat().st_mode & 0o777, 0o555)
            self.assertEqual(meta["command"][0], meta["compiler_path"])
            self.assertIn("-s", meta["command"])
            self.assertEqual(meta["sources"]["bridge_dispatch.h"], build.sha(
                (TOOLS.parent / "src/bridge_dispatch.h").read_bytes()))
if __name__ == "__main__":
    unittest.main()
