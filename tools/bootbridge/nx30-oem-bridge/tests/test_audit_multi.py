"""Adversarial semantic checks against the independent package auditor."""
import copy
import json
import os
import tempfile
from pathlib import Path
import struct
import subprocess
import sys
import unittest
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import audit
import build_return_source

class PublicQualificationAudit(unittest.TestCase):
    def test_public_class_record_adapts_to_common_audit_contract(self):
        catalog={"catalog_path":Path("catalog.h"),"catalog_bytes":b"catalog",
            "catalog_sha256":"a"*64,"supported":["profile"],
            "load":["profile"],"storage":["profile"]}
        for board in "ABCD":
            profile=dict(audit.build.PUBLIC_BOARD_INPUTS[board], **{"class":board})
            with mock.patch.object(audit.build,"public_catalog_binding",return_value=catalog) as bind:
                result=audit.public_trial_qualification({"nand_catalog_sha256":"a"*64},profile)
            bind.assert_called_once_with({"nand_catalog_sha256":"a"*64},profile,"cc")
            self.assertEqual(result["boot_chain_qualification"],profile["record"])
            self.assertIs(result["install_qualification_available"],True)
            self.assertEqual(result["catalog_sha256"],"a"*64)
            self.assertEqual(result["supported"],["profile"])

    def test_public_trial_refuses_missing_finite_record(self):
        catalog={"catalog_sha256":"a"*64}
        with mock.patch.object(audit.build,"public_catalog_binding",return_value=catalog):
            with self.assertRaisesRegex(ValueError,"exact class-bound"):
                audit.public_trial_qualification({}, {"class":"A"})

    def test_public_provider_paths_are_derived_only_from_explicit_class(self):
        for board in "ABCD":
            self.assertEqual(audit.public_provider_paths(board),
                ("usr/sbin/bridge-check-HW-" + board,
                 "etc/bridge/providers/HW-" + board + "/uboot.bin"))
        for bad in (None, "", "E", "B/../../tmp/provider", ["B"]):
            with self.subTest(value=bad), self.assertRaises(ValueError):
                audit.public_provider_paths(bad)

    def test_public_provider_path_contract_keeps_legacy_and_bundle_layouts_explicit(self):
        self.assertEqual(audit.provider_path_contract("A"),
                         (("usr/sbin/bridge-check", "etc/bridge/uboot.bin"), 0o755))
        self.assertEqual(audit.provider_path_contract("A", bundle_provider_paths=True),
                         (("usr/sbin/bridge-check-HW-A",
                           "etc/bridge/providers/HW-A/uboot.bin"), 0o555))

    @unittest.skipUnless(__import__("shutil").which("mksquashfs") and
                         __import__("shutil").which("unsquashfs"),
                         "SquashFS host tools unavailable")
    def test_squashfs_provider_and_payload_modes_are_checked_at_fixed_paths(self):
        with tempfile.TemporaryDirectory(prefix="audit-public-provider-mode-") as td:
            root = Path(td)
            tree = root / "tree"
            provider_rel, payload_rel = audit.public_provider_paths("B")
            provider = tree / provider_rel
            payload = tree / payload_rel
            provider.parent.mkdir(parents=True)
            payload.parent.mkdir(parents=True)
            provider.write_bytes(b"synthetic provider ELF bytes")
            payload.write_bytes(b"synthetic candidate bytes")
            provider.chmod(0o555)
            payload.chmod(0o400)
            image = root / "rootfs.squashfs"
            subprocess.run(["mksquashfs", str(tree), str(image), "-noappend", "-no-progress"],
                           check=True, capture_output=True)
            self.assertEqual(audit.squashfs_regular_file(image, provider_rel, 0o555,
                             "provider"), provider.read_bytes())
            self.assertEqual(audit.squashfs_regular_file(image, payload_rel, 0o400,
                             "payload"), payload.read_bytes())
            with self.assertRaisesRegex(ValueError, "mode mismatch"):
                audit.squashfs_regular_file(image, provider_rel, 0o755, "provider")
            self.assertFalse(audit.squashfs_has_file(image, "etc/bridge/uboot.json"))

class MultiAudit(unittest.TestCase):
    def test_optimized_import_and_cli_refuse_before_input(self):
        tool = Path(audit.__file__).resolve()
        for flag in ("-O", "-OO"):
            result = subprocess.run([sys.executable, flag, str(tool), "--help"], capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn("Python optimization is unsupported",result.stderr)
            result = subprocess.run([sys.executable, flag, "-c", "import sys;sys.path.insert(0,"+repr(str(tool.parent))+");import audit"],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn("Python optimization is unsupported",result.stderr)

    def fixture(self):
        old={'/':{},'/configurations':{'default':b'config@mp03.1\0'}}
        for n in range(24):old[f'/configurations/config{n}']={'fdt':f'fdt{n}'.encode()}
        names=['kernel@1']+[v[0] for v in audit.bridge_kernel.ROUTES.values()]+[f'other{i}' for i in range(21)]
        for name in names:old['/images/'+name]={'data':name.encode(),'type':b'flat_dt\0'}
        new=copy.deepcopy(old);dts={}
        for _,(name,_,_) in audit.bridge_kernel.ROUTES.items():
            base={'/':{},'/chosen':{}}
            for path in audit.bridge_kernel.DISABLED:base[path]={'status':b'okay\0'}
            changed=copy.deepcopy(base);changed['/']['h3c,bridge-profile']=b'H3C-BOOTBRIDGE-MULTICONFIG-V1\0'
            changed['/chosen']['bootargs']=b'console=ttyMSM0,115200,n8 ro init=/sbin/init\0'
            for path in audit.bridge_kernel.DISABLED:changed[path]['status']=b'disabled\0'
            dts[name.encode()]=base;dts[(name+'new').encode()]=changed;new['/images/'+name]['data']=(name+'new').encode()
        header=b'1234'+struct.pack('>I',40)+b'\0'*32
        return old,new,dts,header
    def check(self,mutate=None,expected=True):
        old,new,dts,h=self.fixture()
        if mutate:mutate(new,dts)
        with mock.patch.object(audit,'nodes',side_effect=[new,old]),mock.patch.object(audit.vendor_fit,'fdt_props',side_effect=lambda b:dts[b]):
            if expected:self.assertEqual(len(audit.audit_kernel(h,h)),3)
            else:
                with self.assertRaises(ValueError):audit.audit_kernel(h,h)
    def test_complete_preservation_accepts_only_six_changes_per_route(self):self.check()
    def test_config_default_tamper_rejected(self):self.check(lambda n,d:n['/configurations'].update(default=b'other\0'),False)
    def test_config_removal_rejected(self):self.check(lambda n,d:n.pop('/configurations/config1'),False)
    def test_compressed_kernel_tamper_rejected(self):self.check(lambda n,d:n['/images/kernel@1'].update(data=b'patched'),False)
    def test_unreviewed_dtb_tamper_rejected(self):self.check(lambda n,d:n['/images/other0'].update(data=b'patched'),False)
    def test_route_image_metadata_tamper_rejected(self):self.check(lambda n,d:n['/images/fdt@mp02.1'].update(type=b'kernel\0'),False)
    def test_missing_guard_change_rejected(self):self.check(lambda n,d:d[b'fdt@mp02.1new']['/soc/wifi@c000000'].update(status=b'okay\0'),False)
    def test_wrong_marker_and_extra_property_rejected(self):
        self.check(lambda n,d:d[b'fdt@mp03.1new']['/'].update({'h3c,bridge-profile':b'HW-D\0'}),False)
        self.check(lambda n,d:d[b'fdt@mp03.1new']['/'].update({'unknown':b'1'}),False)
    def test_trailing_kernel_data_and_unknown_original_rejected(self):
        with self.assertRaises(ValueError):audit.audit_kernel(b'1234'+struct.pack('>I',40)+b'\0'*33,b'bad')
        with self.assertRaises(ValueError):audit.audit_wrapper(b'x'*200,b'H3C_+k'+b'\0'*200)
class OuterGuards(unittest.TestCase):
    def fixture(self):
        import hashlib
        component=b"UBI fixture"
        node="/images/ubi-bridge-"+hashlib.sha1(component).hexdigest()
        tree={"/":{},"/images":{},node:{"data":component,"description":b"fixture\0","type":b"firmware\0","arch":b"arm\0","compression":b"none\0"},
              node+"/hash@1":{"algo":b"crc32\0","value":b"digest"},node+"/hash@2":{"algo":b"sha1\0","value":b"digest"},
              "/configurations":{"default":b"conf-bridge\0"},"/configurations/conf-bridge":{"firmware":node.split("/")[-1].encode()+b"\0"}}
        raw=bytearray(200);raw[:6]=b"H3C_+k";struct.pack_into(">I",raw,68,136);struct.pack_into(">I",raw,80,40);raw[160:164]=b"\xd0\x0d\xfe\xed";struct.pack_into(">I",raw,164,40)
        total=sum(x[0]for x in struct.iter_unpack(">I",raw[64:]))&0xffffffff;struct.pack_into(">I",raw,72,(0xffffffff-total)&0xffffffff)
        return bytes(raw),tree,node
    def check(self,mutation=None,accepted=False):
        raw,tree,node=self.fixture()
        if mutation:mutation(tree,node)
        with mock.patch.object(audit.build,'identify_official',return_value={"identity":"fixture"}),mock.patch.object(audit,'nodes',return_value=tree):
            if accepted:audit.audit_wrapper(raw,raw)
            else:
                with self.assertRaises(ValueError):audit.audit_wrapper(raw,raw)
    def test_reviewed_single_ubi_topology(self):self.check(accepted=True)
    def test_bootloader_component_and_hidden_node_rejected(self):
        self.check(lambda t,n:t.update({"/images/u-boot":{}}))
        self.check(lambda t,n:t.update({"/hidden-payload":{}}))
    def test_config_boot_property_and_entry_address_rejected(self):
        self.check(lambda t,n:t["/configurations/conf-bridge"].update(kernel=b"u-boot\0"))
        self.check(lambda t,n:t[n].update(entry=b"address"))
    def test_wrong_hash_layout_rejected(self):self.check(lambda t,n:t[n+"/hash@1"].update(algo=b"md5\0"))

class ReturnSourceAudit(unittest.TestCase):
    OFFICIAL = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"

    def fixture(self):
        dt = {"/": {"compatible": b"qcom,ipq5018-mp02.1\0qcom,ipq5018\0"}}
        for path in audit.RETURN_NAND_NODES:
            if path not in dt: dt[path] = {"reg": b"\x01\x02", "status": b"okay\0"}
        stock_dt, bridge_dt = b"stock-dt-fixture", b"bridge-dt-fixture"
        compressed = b"actual compressed kernel fixture"
        def fit_tree(dt_blob):
            return {"/configurations/config@mp02.1": {"kernel": b"kernel@1\0", "fdt": b"fdt@mp02.1\0"},
                    "/images/kernel@1": {"data": compressed},
                    "/images/fdt@mp02.1": {"data": dt_blob}}
        closure = audit.return_canonical({p: {k:v.hex() for k,v in props.items()}
            for p,props in dt.items() if any(p == r or p.startswith(r + "/") for r in audit.RETURN_NAND_ROOTS)})
        fit_bytes = b"\xd0\x0d\xfe\xed" + struct.pack(">I", 40) + b"\0" * 32
        return fit_bytes, fit_bytes, fit_tree(stock_dt), fit_tree(bridge_dt), \
               {stock_dt: dt, bridge_dt: dt}, audit.sha(closure)

    def test_actual_contract_fields_header_and_arm_marker(self):
        stock, bridge, stock_tree, bridge_tree, dts, closure_sha = self.fixture()
        with mock.patch.object(audit.bridge_kernel, "SOURCES", {self.OFFICIAL: ("test", audit.sha(stock))}), \
             mock.patch.object(audit, "RETURN_NAND_SHA256", closure_sha), \
             mock.patch.object(audit, "nodes", side_effect=[stock_tree, bridge_tree]), \
             mock.patch.object(audit.vendor_fit, "fdt_props", side_effect=lambda raw: dts[raw]):
            meta = audit.return_source_metadata(stock, bridge, self.OFFICIAL)
        self.assertEqual(meta["route"], "config@mp02.1")
        self.assertEqual(meta["compatible"], "qcom,ipq5018-mp02.1")
        self.assertEqual(meta["nand_dt_sha256"], closure_sha)
        self.assertEqual(meta["compressed_kernel_sha256"], audit.sha(b"actual compressed kernel fixture"))
        self.assertEqual(meta["revision_sha256"], audit.sha(audit.return_canonical({k:v for k,v in meta.items() if k != "revision_sha256"})))
        header = audit.return_source_header(meta)
        self.assertIn(b'#define BI_RETURN_SOURCE_ROUTE "config@mp02.1"\n', header)
        marker = ("BB_RETURN_SOURCE_REVISION:" + meta["revision_sha256"]).encode()
        self.assertEqual(audit.require_arm_source_marker(b"ARM ELF\0" + marker + b"\0", meta["revision_sha256"]), marker.decode())
        with self.assertRaisesRegex(ValueError, "exactly one"):
            audit.require_arm_source_marker(b"ARM ELF without marker", meta["revision_sha256"])

    def test_contract_pins_and_closure_shape_are_fixed(self):
        self.assertEqual(audit.RETURN_NAND_SHA256, "8a59fdb5d6864a98fa833d0fa56aa615a13acc99038c459d7e0039fed460acfd")
        self.assertEqual(len(audit.RETURN_NAND_NODES), 9)
        dt = {path: {"status": b"okay\0"} for path in audit.RETURN_NAND_NODES if path != audit.RETURN_NAND_ROOTS[1]}
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            audit.return_nand_closure(dt)

    def test_mismatched_stock_pin_compressed_kernel_or_nand_closure_rejected(self):
        stock, bridge, stock_tree, bridge_tree, dts, closure_sha = self.fixture()
        with mock.patch.object(audit.bridge_kernel, "SOURCES", {self.OFFICIAL: ("test", "0" * 64)}):
            with self.assertRaisesRegex(ValueError, "FIT pin"):
                audit.return_source_metadata(stock, bridge, self.OFFICIAL)
        altered = copy.deepcopy(bridge_tree)
        altered["/images/kernel@1"]["data"] = b"different compressed kernel"
        with mock.patch.object(audit.bridge_kernel, "SOURCES", {self.OFFICIAL: ("test", audit.sha(stock))}), \
             mock.patch.object(audit, "RETURN_NAND_SHA256", closure_sha), \
             mock.patch.object(audit, "nodes", side_effect=[stock_tree, altered]), \
             mock.patch.object(audit.vendor_fit, "fdt_props", side_effect=lambda raw: dts[raw]):
            with self.assertRaisesRegex(ValueError, "compressed kernel bytes differ"):
                audit.return_source_metadata(stock, bridge, self.OFFICIAL)

    def test_nand_closure_mutation_rejected_even_when_kernel_is_equal(self):
        stock, bridge, stock_tree, bridge_tree, dts, closure_sha = self.fixture()
        changed_dt = copy.deepcopy(dts[b"bridge-dt-fixture"])
        changed_dt["/soc/qpic-nand@79b0000/nandcs@0"]["reg"] = b"changed"
        dts[b"bridge-dt-fixture"] = changed_dt
        with mock.patch.object(audit.bridge_kernel, "SOURCES", {self.OFFICIAL: ("test", audit.sha(stock))}), \
             mock.patch.object(audit, "RETURN_NAND_SHA256", closure_sha), \
             mock.patch.object(audit, "nodes", side_effect=[stock_tree, bridge_tree]), \
             mock.patch.object(audit.vendor_fit, "fdt_props", side_effect=lambda raw: dts[raw]):
            with self.assertRaisesRegex(ValueError, "NAND/pinctrl closure differs"):
                audit.return_source_metadata(stock, bridge, self.OFFICIAL)

    def test_actual_nx30_official_bytes_recompute_source_and_header_contract(self):
        configured_path = os.environ.get("BOOTBRIDGE_NX30_R012_OFFICIAL")
        if not configured_path:
            self.skipTest("set BOOTBRIDGE_NX30_R012_OFFICIAL to the exact ordinary package")
        official_path = Path(configured_path).expanduser()
        if not official_path.is_file(): self.skipTest("ordinary exact NX30 R012 official package unavailable")
        official = official_path.read_bytes()
        identity = audit.build.identify_official(official)
        self.assertEqual(identity["sha256"], self.OFFICIAL)
        total = struct.unpack_from(">I", official, 164)[0]
        outer = audit.nodes(official[160:160 + total])
        ubi_images = [props["data"] for path,props in outer.items()
                      if path.startswith("/images/ubi-") and path.count("/") == 2 and "data" in props]
        self.assertEqual(len(ubi_images), 1)
        with tempfile.TemporaryDirectory(prefix="bootbridge-audit-source-") as td:
            ubi_path = Path(td) / "stock.ubi"
            ubi_path.write_bytes(ubi_images[0])
            _, volumes, _, _ = audit.vendor_ubi.parse(ubi_path)
        stock_kernel = volumes["kernel"]
        bridge_kernel, _ = audit.bridge_kernel.transform_kernel(stock_kernel, identity["sha256"])
        self.assertEqual(len(audit.audit_kernel(bridge_kernel, stock_kernel)), 3)
        metadata = audit.return_source_metadata(stock_kernel, bridge_kernel, identity["sha256"])
        self.assertEqual(metadata, {
            "schema": 1, "official_sha256": self.OFFICIAL,
            "stock_kernel_sha256": "5878e39213f37fb3d26b51a5197e09b05e7de18ee275fd7f430ea6eac5e7dc85",
            "bridge_kernel_sha256": "bf9683e4ef1e75e91f3c82280dc36e6ad02ae4137dfa4f4234565f94e2bf668e",
            "compressed_kernel_sha256": "ae0767a2ffbf040f9b73ef74b1fdf0b6490679595ce302299157d846fad062e6",
            "nand_dt_sha256": audit.RETURN_NAND_SHA256, "product": 1,
            "route": "config@mp02.1", "compatible": "qcom,ipq5018-mp02.1",
            "revision_sha256": "40a6acb40913eeb3d112b78399c24f5559a80e0e09bbc3fab86d7f2a70e1ed3f",
        })
        report_name = os.environ.get("BB_RETURN_SOURCE_RESULT_JSON")
        report_path = Path(report_name).expanduser() if report_name else None
        if report_path is not None and report_path.is_file():
            recorded = json.loads(report_path.read_text()).get("return_source_evidence")
            self.assertEqual(recorded, metadata)
        header = audit.return_source_header(metadata)
        self.assertEqual(header.decode("ascii"), build_return_source.c_header(metadata))
        self.assertIn(b'#define BI_RETURN_SOURCE_REVISION_SHA256 "' + metadata["revision_sha256"].encode() + b'"\n', header)
        marker = ("BB_RETURN_SOURCE_REVISION:" + metadata["revision_sha256"]).encode()
        self.assertEqual(audit.require_arm_source_marker(marker, metadata["revision_sha256"]), marker.decode())


class ActualOriginals(unittest.TestCase):
    def test_four_pinned_real_transforms_and_config_tamper(self):
        source_json = os.environ.get("BB_AUDIT_SOURCE_JSON")
        if not source_json:
            self.skipTest("set BB_AUDIT_SOURCE_JSON to the exact caller-supplied input records")
        records = json.loads(source_json)
        checked = 0
        for record in records:
            path = Path(record["source"])
            if not path.exists(): continue
            stock = path.read_bytes(); identity = audit.build.identify_official(stock)
            size = struct.unpack_from(">I",stock,164)[0]; outer = audit.nodes(stock[160:160+size])
            ubi = [v["data"] for p,v in outer.items() if p.startswith("/images/ubi-") and p.count("/")==2]
            self.assertEqual(len(ubi),1)
            with tempfile.TemporaryDirectory() as td:
                p = Path(td)/"stock.ubi"; p.write_bytes(ubi[0])
                _,volumes,_,_ = audit.vendor_ubi.parse(p)
            original = volumes["kernel"]
            candidate,_ = audit.bridge_kernel.transform_kernel(original,identity["sha256"])
            self.assertEqual(len(audit.audit_kernel(candidate,original)),3)
            dt = audit.bridge_kernel._FDT(candidate,4096)
            dt.set("/configurations","default",b"config@unknown\0")
            with self.assertRaisesRegex(ValueError,"default"):
                audit.audit_kernel(dt.bytes(),original)
            checked += 1
        if checked != 4: self.skipTest("four ordinary official input paths unavailable here")

if __name__=='__main__':unittest.main()
