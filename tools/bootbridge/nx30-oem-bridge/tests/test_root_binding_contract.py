"""Focused host tests for source-derived rootfs geometry contracts."""
import hashlib
import json
import sys
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
SRC = ROOT / "src"
UBOOT_INCLUDE = ROOT.parent / "u-boot-2016/include"
sys.path.insert(0, str(TOOLS))
import root_binding_contract as contract


class RootBindingContractTests(unittest.TestCase):
    def fake_meta(self, reserve=149, vid=2048, data=4096, table_sha=None):
        if table_sha is None:
            table_sha = hashlib.sha256(("table-%d" % reserve).encode()).hexdigest()
        return {
            "volume_table": [{"name": "ubi_rootfs", "reserved_pebs": reserve,
                              "capacity": reserve * 126976, "type": "dynamic"}],
            "volume_table_raw_sha256": table_sha,
            "rows": [{"kind": "EC_VID", "vid_offset": vid, "data_offset": data}],
        }

    def test_source_reserve_is_per_source_not_global(self):
        source = "1" * 64
        fake_ubi = b"u" * contract.PEB_BYTES
        with mock.patch.object(contract.vendor_ubi, "parse",
                return_value=(self.fake_meta(149), {}, fake_ubi, [])):
            row_149 = contract.derive(source, fake_ubi)
        with mock.patch.object(contract.vendor_ubi, "parse",
                return_value=(self.fake_meta(153), {}, fake_ubi, [])):
            row_153 = contract.derive("2" * 64, fake_ubi)
        with mock.patch.object(contract.vendor_ubi, "parse",
                return_value=(self.fake_meta(137), {}, fake_ubi, [])):
            row_137 = contract.derive("3" * 64, fake_ubi)
        self.assertEqual([row_149["volume_bytes"], row_153["volume_bytes"], row_137["volume_bytes"]],
                         [18919424, 19427328, 17395712])
        self.assertEqual([row_149["reserved_ebs"], row_153["reserved_ebs"], row_137["reserved_ebs"]],
                         [149, 153, 137])
        self.assertTrue(all(row["usable_leb_bytes"] == 126976 and row["mtd_write_bytes"] == 2048
                            for row in (row_149, row_153, row_137)))

    def test_header_pins_tuple_and_digest(self):
        fake_ubi = b"u" * contract.PEB_BYTES
        with mock.patch.object(contract.vendor_ubi, "parse",
                return_value=(self.fake_meta(149), {}, fake_ubi, [])):
            row = contract.derive("a" * 64, fake_ubi)
        header = contract.render_header(row)
        self.assertIn("#define BI_ROOT_BINDING_CONTRACT { UINT64_C(18919424), UINT64_C(126976), UINT64_C(149), UINT64_C(2048) }", header)
        self.assertIn(contract.contract_sha256(row), header)
        self.assertEqual(contract.report_record(row)["sha256"],
                         hashlib.sha256(header.encode("ascii")).hexdigest())

    def test_gate_config_hashes_executable_inputs_and_ignores_output_cache(self):
        with tempfile.TemporaryDirectory(prefix="root-binding-gate-config-") as temp:
            root = Path(temp)
            kernel, initrd, qemu = (root / "Image", root / "initrd", root / "qemu")
            for path, data in ((kernel, b"kernel"), (initrd, b"initrd"), (qemu, b"qemu")):
                path.write_bytes(data)
            modules = root / "modules"
            (modules / "kernel").mkdir(parents=True)
            (modules / "kernel/module.ko.xz").write_bytes(b"module")
            tools = root / "gate-tools"
            for name in ("build_guest_capsule.sh", "build_guest_modules.sh", "run_guest_gate.sh", "build_guest_probe.sh",
                         "verify_guest_module.sh",
                         "root_binding_guest_probe.c", "guest_mount_root.c", "guest-init",
                         "guest-tools/usr/sbin/nandwrite", "guest-tools/usr/sbin/ubiattach"):
                path = tools / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            for name, data in (("gluebi-module/Makefile", b"make"),
                               ("gluebi-module/gluebi.c", b"gluebi"),
                               ("gluebi-module/r3_mtd_slots.c", b"slots"),
                               ("gluebi-module/ubi-media.h", b"ubi"),
                               ("mtdblock-compat/Makefile", b"make compat"),
                               ("mtdblock-compat/mtd_blkdevs.c", b"blkdevs"),
                               ("mtdblock-compat/mtdblock.c", b"mtdblock"),
                               ("mtdblock-compat/mtdcore.h", b"mtdcore")):
                path = tools / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            guest_modules = tools / "guest-modules"
            guest_modules.mkdir(parents=True)
            module_rows = {}
            for name, data in (("modules.json", b"{}\n"), ("gluebi_guest.ko", b"gluebi"),
                               ("r3_mtd_slots.ko", b"slots"),
                               ("mtd_blkdevs_guest.ko", b"blkdevs"),
                               ("mtdblock_guest.ko", b"mtdblock")):
                (guest_modules / name).write_bytes(data)
                if name != "modules.json":
                    module_rows[name] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
            source_names = ("gluebi-module/Makefile", "gluebi.c",
                "r3_mtd_slots.c", "ubi-media.h", "mtdblock-compat/Makefile",
                "mtdblock-compat/mtd_blkdevs.c", "mtdblock-compat/mtdblock.c", "mtdblock-compat/mtdcore.h",
                "mtdblock-compat/SOURCE-PROVENANCE.json")
            (tools / source_names[-1]).write_text("{}\n")
            source_paths = {name: (tools / name if name.startswith("mtdblock-compat/") else
                tools / ("gluebi-module/Makefile" if name == "gluebi-module/Makefile" else
                         "gluebi-module/" + name)) for name in source_names}
            source_rows = {name: {"sha256": hashlib.sha256(source_paths[name].read_bytes()).hexdigest(),
                                  "bytes": source_paths[name].stat().st_size} for name in source_names}
            module_manifest = {"schema": "r3-guest-module-set-v1", "status": "BUILT",
                "kernel_release": "test-release",
                "kernel_build_inputs": {"config_sha256": "a" * 64, "module_symvers_sha256": "b" * 64},
                "modules": module_rows, "sources": source_rows,
                "variance": "guest-only-mtdblock-registration-compatibility-for-MTD_UBIVOLUME; production validator unchanged"}
            (guest_modules / "modules.json").write_text(json.dumps(module_manifest))
            (tools / "capsules").mkdir()
            config_path = root / "gate.json"
            config_path.write_text(json.dumps({"schema": "r3-live-gluebi-qemu-config-v1",
                "kernel_image": str(kernel), "base_initrd": str(initrd),
                "kernel_release": "test-release", "module_root": str(modules),
                "qemu_bin": str(qemu), "qemu_data_dir": "", "gate_tools_dir": str(tools)}))
            first = contract.read_gate_config(config_path)
            (tools / "capsules/disposable.log").write_text("ignored output")
            second = contract.read_gate_config(config_path)
            self.assertEqual(first["input_pins"], second["input_pins"])
            self.assertEqual(first["input_pins"]["kernel_image_sha256"],
                             hashlib.sha256(b"kernel").hexdigest())
            (tools / "verify_guest_module.sh").write_bytes(b"changed module verifier")
            third = contract.read_gate_config(config_path)
            self.assertNotEqual(first["input_pins"]["gate_tools_tree_sha256"],
                                third["input_pins"]["gate_tools_tree_sha256"])

    def test_rejects_unreviewed_geometry_and_rootfs_row(self):
        source = "a" * 64
        fake_ubi = b"u" * contract.PEB_BYTES
        for meta in (self.fake_meta(149, data=2048), self.fake_meta(149, vid=1024),
                     {**self.fake_meta(149), "volume_table": []}):
            with self.subTest(meta=meta), mock.patch.object(contract.vendor_ubi, "parse",
                    return_value=(meta, {}, fake_ubi, [])):
                with self.assertRaises(contract.RootBindingContractError):
                    contract.derive(source, fake_ubi)

    @unittest.skipUnless(shutil.which("cc"), "host C compiler unavailable")
    def test_production_install_requires_generated_contract_header(self):
        source = SRC / "bridge_install.c"
        with tempfile.TemporaryDirectory(prefix="root-binding-header-test-") as temp:
            include = Path(temp)
            shutil.copyfile(UBOOT_INCLUDE / "h3c_nand_catalog.h",
                            include / "h3c_nand_catalog.h")
            includes = ["-I", str(SRC), "-I", str(include)]
            failed = subprocess.run([shutil.which("cc"), "-E", "-std=c99", *includes, str(source)],
                                    capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("Production root binding requires BI_ROOT_BINDING_CONTRACT_HEADER", failed.stderr)
            fixture = subprocess.run([shutil.which("cc"), "-E", "-std=c99", "-DBRIDGE_INSTALL_TEST",
                                      *includes, str(source)], capture_output=True, text=True)
            self.assertEqual(fixture.returncode, 0, fixture.stderr)
            for value in ("17395712", "126976", "137", "2048"):
                self.assertIn(value, fixture.stdout)


if __name__ == "__main__":
    unittest.main()
