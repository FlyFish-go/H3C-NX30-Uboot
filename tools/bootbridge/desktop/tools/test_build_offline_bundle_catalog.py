#!/usr/bin/env python3
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import sys
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("bundle_catalog_builder_test", HERE / "build_offline_bundle_catalog.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)
PREP_SPEC = importlib.util.spec_from_file_location("bundle_prepare_test", HERE / "prepare_offline_bundles.py")
PREP = importlib.util.module_from_spec(PREP_SPEC)
PREP_SPEC.loader.exec_module(PREP)

def sha(data):
    return hashlib.sha256(data).hexdigest()

class BundleCatalogBuilderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.result_path = self.root / "RESULT.json"

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self, split_rt=False, logical_coverage=False, nx_rx_alias=False):
        bundles = []
        for index, (model, classes) in enumerate((("RC3000", ["A"]), ("RT3000", ["B", "C"]), ("NX30", ["D"]))):
            suffix = "bc" if model == "RT3000" else classes[0][-1].lower()
            original = bytes((i * 13 + index) & 255 for i in range(16384))
            output_array = bytearray(original)
            output_array[31 + index] ^= 0x3D
            output = bytes(output_array)
            official_path = self.root / f"{model}-{suffix}-official.bin"
            official_path.write_bytes(original)
            report_path = self.root / f"{model}-{suffix}-BUILD-REPORT.json"
            audit_path = self.root / f"{model}-{suffix}-AUDIT.json"
            output_path = self.root / f"{model}-{suffix}.bin"
            output_path.write_bytes(output)
            report_classes=( ["A","B","C","D"] if nx_rx_alias and model=="NX30" else classes )
            supported=[{"product":model,"source_sha256":sha(original),"hardware_classes":classes}]
            identity_evidence=None
            receipt_identity_evidence=None
            if nx_rx_alias and model=="NX30":
                identity_evidence_path=self.root/"source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json"
                identity_evidence_path.parent.mkdir(parents=True)
                identity_evidence_path.write_text('{"identity":"exact-shared-source"}\n')
                identity_evidence={"path":"source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json",
                    "bytes":identity_evidence_path.stat().st_size,"sha256":sha(identity_evidence_path.read_bytes())}
                receipt_identity_evidence={"path":str(identity_evidence_path.resolve()),
                    "bytes":identity_evidence_path.stat().st_size,"sha256":sha(identity_evidence_path.read_bytes())}
                supported.append({"product":"RX3000","source_sha256":sha(original),"hardware_classes":["A","B","C","D"],"identity_evidence":identity_evidence})
            catalog_sha=sha(b"nand-catalog-"+model.encode())
            source_variants=["D"]
            profile={"profile_id":"profile-"+model,"oem_model":model,"qpic_model":model,"model_aliases":[model],"jedec_bytes_observed":2,
                     "manufacturer_id":239,"chip_id":186,"capacity_bytes":134217728,"capacity_mib":128,"qpic_capacity_bytes":134217728,"qpic_capacity_mib":128,
                     "page_bytes":2048,"pages_per_block":64,"erase_bytes":131072,
                     "raw_spare_bytes":128,"qpic_raw_spare_bytes":128,"qpic_mtd_oob_ecc4_bytes":64,"qpic_mtd_oob_ecc8_bytes":128,
                     "appsbl_ecc_mode_mask":3,"appsbl_source_mask":1,"appsbl_ecc4_source_mask":1,"appsbl_ecc8_source_mask":1,
                     "kernel_oem_oob_field_mask":7,"kernel_ecc_mode_mask":3,"kernel_ecc_step_bytes":512,"kernel_image_rows":1,
                     "qpic_ecc_mode_mask":3,"runtime_ecc_mode_mask":3,"capabilities":1,"source_table_mask":1}
            runtime_profile={"profile_id":profile["profile_id"],"model_aliases":[model],"jedec_bytes_observed":2,
                     "manufacturer_id":239,"chip_id":186,"capacity_mib":128,"page_bytes":2048,"erase_bytes":131072,
                     "raw_spare_bytes":128,"mtd_oob_bytes":64,"ecc_bits":4,"catalogue_eligible":0}
            runtime_profiles=[runtime_profile,{**runtime_profile,"mtd_oob_bytes":128}]
            profile_set_id="nandset-"+sha(original)[:12]+"-"+sha(json.dumps({"source_sha256":sha(original),"catalog_sha256":catalog_sha,
                "source_variants":source_variants,"profiles":[profile["profile_id"]]},sort_keys=True,separators=(",",":"),ensure_ascii=True).encode())[:16]
            profile_sets={profile_set_id:{"profile_set_id":profile_set_id,"source_sha256":sha(original),"catalog_sha256":catalog_sha,"source_variants":source_variants,
                                           "profile_ids":[profile["profile_id"]],"profiles":[profile],"runtime_profiles":runtime_profiles}}
            candidate_set_id="candset-test-"+model
            archive_sha=sha(b"candidate-archive")
            provider_rows=[]
            for n, board in enumerate(report_classes):
                row_profiles=[dict(x,catalogue_eligible={"A":1,"B":2,"C":4,"D":8}[board]) for x in runtime_profiles]
                provider_rows.append({"record_id":f"{model}-{board}-{n}","hardware_class":board,"candidate_id":f"{model}-{board}-{n}",
                    "profile_set_id":profile_set_id,"candidate_set_id":candidate_set_id,"nand_profiles":row_profiles,
                    "nand_profile_id":profile["profile_id"],"load_ecc_bits":4,"storage_ecc_bits":4,
                    "nand":{"profile_id":profile["profile_id"],"model_aliases":[model],"manufacturer_id":239,"chip_id":186,
                            "capacity_mib":128,"page_bytes":2048,"erase_bytes":131072,"raw_spare_bytes":128,"mtd_oob_bytes":64,"ecc_bits":4},
                    "provider":{"path":f"/usr/sbin/bridge-check-{board}","bytes":4,"sha256":sha(b"provider"+board.encode())},
                    "payload":{"path":f"/etc/bridge/providers/{board}/uboot.bin","bytes":16,"sha256":sha(b"partition"+board.encode()),
                               "storage":{"encoding":"base-patch-v1","path":"/etc/bridge/provider-payload-bases/"+sha(b"base")+".bin",
                                          "sha256":sha(b"base"),"bytes":24,"patch_header_sha256":sha(b"patch-header"),
                                          "patch_count":2,"data_bytes":16},
                               "candidate_id":f"{model}-{board}-{n}",
                               "descriptor":{"candidate_id":f"{model}-{board}-{n}","files":{"appsbl.partition.bin":{"sha256":sha(b"partition"+board.encode()),"bytes":16},
                                              "appsbl.mbn":{"sha256":sha(b"mbn"+board.encode()),"bytes":8}},
                                            "container":{"load":{"sha256":sha(b"load"+board.encode()),"bytes":20}}},
                               "mbn":{"sha256":sha(b"mbn"+board.encode()),"bytes":8},
                               "load":{"sha256":sha(b"load"+board.encode()),"bytes":20}}})
            if model=="RT3000":
                duplicate=dict(provider_rows[0]);duplicate["record_id"]+="-profile2";duplicate["candidate_id"]+="-profile2"
                duplicate["nand_profiles"]=[dict(x,catalogue_eligible=2) for x in runtime_profiles];provider_rows.append(duplicate)
                duplicate["payload"]=dict(duplicate["payload"]);duplicate["payload"]["descriptor"]=dict(duplicate["payload"]["descriptor"])
                duplicate["payload"]["descriptor"]["candidate_id"]=duplicate["candidate_id"]
                duplicate["payload"]["candidate_id"]=duplicate["candidate_id"]
            contracts=[]
            for p in provider_rows:
                d=p["payload"]["descriptor"];part=d["files"]["appsbl.partition.bin"]
                contracts.append({"candidate_id":p["candidate_id"],"board":p["hardware_class"],"nand_profile_id":p["nand_profile_id"],
                    "load_ecc_bits":p["load_ecc_bits"],"storage_ecc_bits":p["storage_ecc_bits"],"partition_sha256":part["sha256"],
                    "mbn_sha256":d["files"]["appsbl.mbn"]["sha256"],"load_sha256":d["container"]["load"]["sha256"],"partition_bytes":part["bytes"]})
            candidate_inventory=list(contracts)
            candidate_inventory.append({"candidate_id":f"{model}-ineligible","board":"D","nand_profile_id":"source-ineligible-profile",
                "load_ecc_bits":4,"storage_ecc_bits":4,"partition_sha256":sha(b"ineligible-partition"),
                "mbn_sha256":sha(b"ineligible-mbn"),"load_sha256":sha(b"ineligible-load"),"partition_bytes":16})
            candidate_set_id="candset-"+sha(json.dumps({"schema":"bootbridge-candidate-set-v1","source_sha256":sha(original),
                "archive_sha256":archive_sha,"candidates":sorted(candidate_inventory,key=lambda x:x["candidate_id"])},sort_keys=True,separators=(",",":"),ensure_ascii=True).encode())[:16]
            for p in provider_rows:p["candidate_set_id"]=candidate_set_id
            report = {"schema":"bootbridge-provider-bundle-v2", "status":"OFFLINE_LAB_CANDIDATE",
                      "official":{"identity":f"{model}-{suffix}","model":model,"version":"R015","sha256":sha(original),"bytes":len(original)},
                      "source_identity":{"header_product":model,"version":"R015","sha256":sha(original),"bytes":len(original)},
                      "supported_product_identities":supported,
                      "candidate_archive_sha256":archive_sha,"candidate_manifest_sha256":sha(b"candidate-manifest"),
                      "candidate_archive_bytes":4096,"candidate_manifest_bytes":300,"candidate_descriptors_sha256":sha(b"candidate-descriptors"),
                      "candidate_descriptors_bytes":512,"candidate_set_id":candidate_set_id,"candidate_inventory":candidate_inventory,
                      "candidate_archive_candidate_count":len(candidate_inventory),
                      "nand_catalog_sha256":catalog_sha,
                      "source_ineligible_candidates":[{"candidate_id":f"{model}-ineligible","board":"D","nand_profile_id":"source-ineligible-profile","reason":"NO_EXACT_SOURCE_KERNEL_PROFILE"}],
                      "profile_sets":profile_sets,
                      "official_path":str(official_path),
                      "candidate":{"path":output_path.name,"bytes":len(output),"sha256":sha(output)},
                      "providers":provider_rows,
                      "dispatcher":{"sha256":sha(b"dispatcher"),"records_count":len(provider_rows)}}
            report_bytes = (json.dumps(report,sort_keys=True)+"\n").encode()
            report_path.write_bytes(report_bytes)
            audit = {"schema":"bootbridge-provider-bundle-audit-v2","status":"PASS","bundle_status":"OFFLINE_LAB_CANDIDATE","build_report_sha256":sha(report_bytes),"official_sha256":sha(original),"supported_product_identities":supported,
                     "candidate_set_id":candidate_set_id,"profile_sets":profile_sets,"providers":provider_rows,
                     "candidate_inventory":candidate_inventory,"candidate_archive_candidate_count":len(candidate_inventory),
                     "source_ineligible_candidates":report["source_ineligible_candidates"],"nand_catalog_sha256":catalog_sha,
                     "provider_records":len(provider_rows),"nand_profile_count":sum(len(x["profile_ids"]) for x in profile_sets.values()),
                     **{k:report[k] for k in ("candidate_archive_sha256","candidate_archive_bytes","candidate_manifest_sha256","candidate_manifest_bytes","candidate_descriptors_sha256","candidate_descriptors_bytes")}}
            audit_path.write_text(json.dumps(audit,sort_keys=True)+"\n")
            official = dict(report["official"],path=str(official_path))
            row={"bundle_id":f"bundle-{model}-{suffix}","official":official,
                            "build_report":{"path":report_path.name,"sha256":sha(report_bytes)},
                            "audit_report":{"path":audit_path.name,"sha256":sha(audit_path.read_bytes())},
                            "candidate":{"path":output_path.name,"bytes":len(output),"sha256":sha(output)}}
            if logical_coverage and model=="RT3000":
                report_bytes=(json.dumps(report,sort_keys=True)+"\n").encode();report_path.write_bytes(report_bytes)
                audit={"schema":"bootbridge-provider-bundle-audit-v2","status":"PASS","bundle_status":"OFFLINE_LAB_CANDIDATE","build_report_sha256":sha(report_bytes),"official_sha256":sha(original),"supported_product_identities":report["supported_product_identities"],
                       "candidate_set_id":candidate_set_id,"profile_sets":profile_sets,"providers":provider_rows,
                       "candidate_inventory":candidate_inventory,"candidate_archive_candidate_count":len(candidate_inventory),
                       "source_ineligible_candidates":report["source_ineligible_candidates"],"nand_catalog_sha256":catalog_sha,
                       "provider_records":len(provider_rows),"nand_profile_count":sum(len(x["profile_ids"]) for x in profile_sets.values()),
                       **{k:report[k] for k in ("candidate_archive_sha256","candidate_archive_bytes","candidate_manifest_sha256","candidate_manifest_bytes","candidate_descriptors_sha256","candidate_descriptors_bytes")}}
                audit_path.write_text(json.dumps(audit,sort_keys=True)+"\n")
                row["build_report"]["sha256"]=sha(report_bytes);row["audit_report"]["sha256"]=sha(audit_path.read_bytes())
                boards=["B","C"]
                coverage_path=self.root/f"{model}-logical-coverage.json"
                candidate_ids=sorted({p["candidate_id"] for p in provider_rows if p["hardware_class"] in boards})
                receipt={"schema":"bootbridge-logical-coverage-receipt-v1","status":"PASS_LOGICAL_COVERAGE_PROVENANCE",
                    "software_contract_status":"SOFTWARE_CONTRACT_ONLY","bundle_id":row["bundle_id"],
                    "source_model":model,"source_version":"R015","source_sha256":sha(original),
                    "candidate_bytes":len(output),"candidate_sha256":sha(output),"model":model,"providers":boards,
                    "candidate_ids":candidate_ids,"provider_records":len(candidate_ids),
                    "build_report_sha256":row["build_report"]["sha256"],"audit_report_sha256":row["audit_report"]["sha256"],
                    "dispatcher_sha256":report["dispatcher"]["sha256"],"hardware_validation":"NOT_EXECUTED",
                    "physical_install_qualification":"NOT_GRANTED"}
                coverage_path.write_text(json.dumps(receipt,sort_keys=True)+"\n")
                row["logical_coverage"]=[{"model":model,"providers":boards,"evidence":{"path":coverage_path.name,
                    "bytes":coverage_path.stat().st_size,"sha256":sha(coverage_path.read_bytes())}}]
            if nx_rx_alias and model=="NX30":
                coverage_rows=[]
                for logical_model,boards in (("NX30",["D"]),("RX3000",["A","B","C","D"])):
                    coverage_path=self.root/f"{logical_model}-logical-coverage.json"
                    candidate_ids=sorted({p["candidate_id"] for p in provider_rows if p["hardware_class"] in boards})
                    receipt={"schema":"bootbridge-logical-coverage-receipt-v1","status":"PASS_LOGICAL_COVERAGE_PROVENANCE",
                        "software_contract_status":"SOFTWARE_CONTRACT_ONLY","bundle_id":row["bundle_id"],
                        "source_model":"NX30","source_version":"R015","source_sha256":sha(original),
                        "candidate_bytes":len(output),"candidate_sha256":sha(output),"model":logical_model,"providers":boards,
                        "candidate_ids":candidate_ids,"provider_records":len(candidate_ids),
                        "build_report_sha256":row["build_report"]["sha256"],"audit_report_sha256":row["audit_report"]["sha256"],
                        "dispatcher_sha256":report["dispatcher"]["sha256"],"hardware_validation":"NOT_EXECUTED",
                        "physical_install_qualification":"NOT_GRANTED"}
                    if logical_model=="RX3000":receipt["identity_evidence"]=receipt_identity_evidence
                    coverage_path.write_text(json.dumps(receipt,sort_keys=True)+"\n")
                    coverage_rows.append({"model":logical_model,"providers":boards,
                        "evidence":{"path":coverage_path.name,"bytes":coverage_path.stat().st_size,"sha256":sha(coverage_path.read_bytes())}})
                row["logical_coverage"]=coverage_rows
            bundles.append(row)
        if split_rt:
            rt = bundles[1]
            duplicate = dict(rt)
            duplicate["bundle_id"] += "-split"
            # Deliberately preserve the exact same source fingerprint with a different output.
            duplicate["candidate"] = dict(rt["candidate"])
            bundles[2] = duplicate
        result = {"schema":"bootbridge-provider-bundle-build-result-v1","status":"PASS","release_set":"test-bundle-v1","engine_revision":2,"bundles":bundles}
        self.result_path.write_text(json.dumps(result,sort_keys=True)+"\n")

    def test_combined_source_bundles_compile_one_output_each(self):
        self.fixture(logical_coverage=True)
        out = self.root / "generated"
        prepared = self.root / "prepared"
        prep_manifest = PREP.prepare(self.result_path,prepared)
        manifest = BUILDER.build(prepared/"prepared-bundles.json",prepared,out)
        self.assertEqual(manifest["bundle_count"],3)
        self.assertEqual([b["providers"] for b in manifest["bundles"]],[["A"],["B","C"],["D"]])
        self.assertEqual(manifest["bundles"][1]["logical_coverage"][0]["model"],"RT3000")
        self.assertEqual(manifest["bundles"][1]["logical_coverage"][0]["providers"],["B","C"])
        self.assertEqual(len(manifest["bundles"][1]["logical_coverage"]),1)
        header = (out / "bb_offline_catalog_generated.h").read_text()
        self.assertIn("bb_offline_bundles[]",header)
        self.assertIn("bb_offline_providers_1[] = {BB_BOARD_HW_B,BB_BOARD_HW_C}",header)
        candidate_size=sum(row["candidate"]["bytes"] for row in prep_manifest["bundles"])
        self.assertLess((out / "BootBridge.rules.dat").stat().st_size,candidate_size)
        self.assertIn("BB_PLAN_COPY",header)
        self.assertEqual(len(manifest["bundles"][1]["provider_rows"]),3)
        self.assertTrue(shutil.which("gcc"))
        helper=self.root/"bundle-export-test"
        subprocess.run(["gcc","-std=c99","-Wall","-Wextra","-Werror","-DBB_OFFLINE_TEST",
                        "-I"+str(out),"-I"+str(HERE.parent),"-I"+str(HERE.parent/"catalog"),
                        str(HERE.parent/"offline/test_bundle_export.c"),str(HERE.parent/"offline/bb_offline_rules.c"),
                        str(HERE.parent/"catalog/bb_catalog.c"),str(HERE.parent/"src/bb_export.c"),"-o",str(helper)],check=True)
        result=json.loads(self.result_path.read_text())
        first=result["bundles"][0]
        input_path=self.root/first["official"]["path"] if not Path(first["official"]["path"]).is_absolute() else Path(first["official"]["path"])
        target_path=self.root/first["candidate"]["path"]
        exported=self.root/"exported.bin"
        env=dict(os.environ,BB_OFFLINE_TEST_DATA=str(out/"BootBridge.rules.dat"))
        subprocess.run([str(helper),str(input_path),str(exported)],env=env,check=True)
        self.assertEqual(exported.read_bytes(),target_path.read_bytes())

    def test_same_source_in_split_rows_is_rejected(self):
        self.fixture(split_rt=True)
        with self.assertRaisesRegex(ValueError,"duplicate exact source"):
            PREP.prepare(self.result_path,self.root / "out")

    def test_legacy_report_schema_is_rejected(self):
        self.fixture()
        result=json.loads(self.result_path.read_text())
        report_path=self.root/result["bundles"][0]["build_report"]["path"]
        report=json.loads(report_path.read_text());report["schema"]="bootbridge-provider-bundle-v1";report["status"]="OFFLINE_LAB_BUNDLE"
        raw=(json.dumps(report,sort_keys=True)+"\n").encode();report_path.write_bytes(raw)
        result["bundles"][0]["build_report"]["sha256"]=sha(raw)
        self.result_path.write_text(json.dumps(result,sort_keys=True)+"\n")
        with self.assertRaisesRegex(ValueError,"ABI-v2"):
            PREP.prepare(self.result_path,self.root/"out")

    def test_provider_storage_descriptor_is_strict_and_separate_from_candidate_identity(self):
        self.fixture()
        result=json.loads(self.result_path.read_text())
        report_path=self.root/result["bundles"][0]["build_report"]["path"]
        report=json.loads(report_path.read_text())
        payload=report["providers"][0]["payload"]
        self.assertEqual(payload["sha256"],payload["descriptor"]["files"]["appsbl.partition.bin"]["sha256"])
        self.assertEqual(payload["bytes"],payload["descriptor"]["files"]["appsbl.partition.bin"]["bytes"])
        payload["storage"]["patch_count"]=0
        payload["storage"]["data_bytes"]=0
        PREP.validate_v2_metadata(report,"test-storage-zero-patch")
        payload["storage"]["data_bytes"]=1
        with self.assertRaisesRegex(ValueError,"base-patch storage descriptor"):
            PREP.validate_v2_metadata(report,"test-storage-inconsistent-count")
        payload["storage"]["data_bytes"]=0
        payload["storage"]["patch_count"]=1
        with self.assertRaisesRegex(ValueError,"base-patch storage descriptor"):
            PREP.validate_v2_metadata(report,"test-storage-inconsistent-data")
        payload["storage"]["patch_count"]=True
        payload["storage"]["data_bytes"]=1
        with self.assertRaisesRegex(ValueError,"base-patch storage descriptor"):
            PREP.validate_v2_metadata(report,"test-storage-bool-count")
        payload["storage"]["patch_count"]=0
        payload["storage"]["data_bytes"]=0
        payload["storage"]["path"]="/etc/bridge/provider-payload-bases/../bad.bin"
        with self.assertRaisesRegex(ValueError,"base-patch storage descriptor"):
            PREP.validate_v2_metadata(report,"test-storage")

    def test_live_bridge_builder_and_nand_schema_contract(self):
        bridge_tools=HERE.parent.parent/"nx30-oem-bridge"/"tools"
        if not bridge_tools.is_dir():
            self.skipTest("sibling OEM bridge source is not present in this standalone checkout")
        sys.path.insert(0,str(bridge_tools))
        try:
            import build_bundle as live_builder
            import nand_schema as live_nand_schema
            source_sha="69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44"
            profile_set=live_builder.source_nand_profile_set({"sha256":source_sha,"model":"RC3000"})
            profile_id=profile_set["profile_ids"][0]
            base_runtime=next(row for row in profile_set["runtime_profiles"] if row["profile_id"]==profile_id)
            storage_ecc=base_runtime["ecc_bits"]
            matching_runtime=[dict(row,catalogue_eligible=1) for row in profile_set["runtime_profiles"]
                if row["profile_id"]==profile_id and row["ecc_bits"]==storage_ecc]
            row_runtime=matching_runtime[0]
            archive_sha=sha(b"integration-candidate-archive")
            candidate={"candidate_id":"cross-board-a-profile","board":"A","nand_profile_id":profile_id,
                "load_ecc_bits":storage_ecc,"storage_ecc_bits":storage_ecc,
                "partition_sha256":sha(b"partition"),"mbn_sha256":sha(b"mbn"),
                "load_sha256":sha(b"load"),"partition_bytes":16}
            omitted={"candidate_id":"cross-ineligible","board":"A","nand_profile_id":"profile-not-in-source",
                "load_ecc_bits":4,"storage_ecc_bits":4,"partition_sha256":sha(b"other-partition"),
                "mbn_sha256":sha(b"other-mbn"),"load_sha256":sha(b"other-load"),"partition_bytes":16}
            inventory=[candidate,omitted]
            candidate_set_id=live_nand_schema.candidate_set_id(source_sha,archive_sha,inventory)
            descriptor={"candidate_id":candidate["candidate_id"],"files":{"appsbl.partition.bin":{"sha256":candidate["partition_sha256"],"bytes":16},
                "appsbl.mbn":{"sha256":candidate["mbn_sha256"],"bytes":8}},"container":{"load":{"sha256":candidate["load_sha256"],"bytes":20}}}
            provider={"record_id":"cross-record","candidate_id":candidate["candidate_id"],"hardware_class":"A",
                "profile_set_id":profile_set["profile_set_id"],"candidate_set_id":candidate_set_id,"nand_profile_id":profile_id,
                "nand_profiles":matching_runtime,"nand":{"profile_id":profile_id,"model_aliases":row_runtime["model_aliases"],
                    "manufacturer_id":row_runtime["manufacturer_id"],"chip_id":row_runtime["chip_id"],"capacity_mib":row_runtime["capacity_mib"],
                    "page_bytes":row_runtime["page_bytes"],"erase_bytes":row_runtime["erase_bytes"],"raw_spare_bytes":row_runtime["raw_spare_bytes"],
                    "mtd_oob_bytes":row_runtime["mtd_oob_bytes"],"ecc_bits":row_runtime["ecc_bits"]},
                "load_ecc_bits":candidate["load_ecc_bits"],"storage_ecc_bits":storage_ecc,
                "provider":{"path":"/usr/sbin/bridge-check-HW-A","sha256":sha(b"provider"),"bytes":8},
                "payload":{"path":"/etc/bridge/providers/HW-A/uboot.bin","sha256":candidate["partition_sha256"],"bytes":16,
                    "storage":{"encoding":"base-patch-v1","path":"/etc/bridge/provider-payload-bases/"+sha(b"base")+".bin",
                        "sha256":sha(b"base"),"bytes":24,"patch_header_sha256":sha(b"patch-header"),"patch_count":2,"data_bytes":16},
                    "candidate_id":candidate["candidate_id"],"descriptor":descriptor,
                    "mbn":{"sha256":candidate["mbn_sha256"],"bytes":8},"load":{"sha256":candidate["load_sha256"],"bytes":20}}}
            report={"schema":"bootbridge-provider-bundle-v2","status":"OFFLINE_LAB_CANDIDATE",
                "official":{"sha256":source_sha},"candidate_archive_sha256":archive_sha,
                "nand_catalog_sha256":profile_set["catalog_sha256"],
                "candidate_archive_bytes":4096,"candidate_manifest_sha256":sha(b"manifest"),"candidate_manifest_bytes":100,
                "candidate_descriptors_sha256":sha(b"descriptors"),"candidate_descriptors_bytes":200,
                "candidate_set_id":candidate_set_id,"candidate_inventory":inventory,
                "candidate_archive_candidate_count":len(inventory),
                "source_ineligible_candidates":[{"candidate_id":omitted["candidate_id"],"board":omitted["board"],
                    "nand_profile_id":omitted["nand_profile_id"],"reason":"NO_EXACT_SOURCE_KERNEL_PROFILE"}],
                "profile_sets":{profile_set["profile_set_id"]:profile_set},"providers":[provider]}
            validated_profiles,validated_rows=PREP.validate_v2_metadata(report,"live-bridge-source")
            self.assertEqual(validated_profiles,report["profile_sets"])
            self.assertEqual(validated_rows,[provider])
        finally:
            sys.path.remove(str(bridge_tools))

    def test_logical_coverage_requires_a_receipt_bound_to_exact_candidate(self):
        self.fixture(logical_coverage=True)
        result=json.loads(self.result_path.read_text())
        spec=result["bundles"][1]["logical_coverage"][0]["evidence"]
        receipt_path=self.root/spec["path"]
        receipt=json.loads(receipt_path.read_text());receipt["build_report_sha256"]="0"*64
        raw=(json.dumps(receipt,sort_keys=True)+"\n").encode();receipt_path.write_bytes(raw)
        spec["bytes"]=len(raw);spec["sha256"]=sha(raw)
        self.result_path.write_text(json.dumps(result,sort_keys=True)+"\n")
        with self.assertRaisesRegex(ValueError,"logical coverage receipt"):
            PREP.prepare(self.result_path,self.root/"out")

    def test_nx_source_can_carry_separately_pinned_rx_logical_coverage(self):
        self.fixture(nx_rx_alias=True)
        prepared=self.root/"prepared"
        PREP.prepare(self.result_path,prepared)
        manifest=BUILDER.build(prepared/"prepared-bundles.json",prepared,self.root/"generated")
        nx=next(row for row in manifest["bundles"] if row["official"]["model"]=="NX30")
        self.assertEqual(nx["providers"],["A","B","C","D"])
        self.assertEqual([(r["model"],r["providers"]) for r in nx["logical_coverage"]],
                         [("NX30",["D"]),("RX3000",["A","B","C","D"])])
        self.assertEqual(nx["display_models"],"NX30 / RX3000")

    def test_rx_relative_identity_path_requires_unique_absolute_receipt_binding(self):
        evidence_path=self.root/"source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json"
        evidence_path.parent.mkdir(parents=True)
        evidence=evidence_path.write_bytes(b"pinned RX source identity\n")
        pin={"path":str(evidence_path.resolve()),"bytes":evidence,"sha256":sha(evidence_path.read_bytes())}
        receipt_path=self.root/"RX-logical.json"
        receipt_path.write_text(json.dumps({"identity_evidence":pin}),encoding="utf-8")
        receipt_raw=receipt_path.read_bytes()
        row={"official":{"model":"NX30"},"logical_coverage":[{"model":"RX3000","providers":["A","B","C","D"],
            "evidence":{"path":str(receipt_path),"bytes":len(receipt_raw),"sha256":sha(receipt_raw)}}]}
        identity={"product":"RX3000","identity_evidence":{"path":PREP.RX_IDENTITY_RELATIVE_PATH,
            "bytes":pin["bytes"],"sha256":pin["sha256"]}}
        resolved=PREP.resolve_supported_identity_pin(self.root,row,"bundle-NX30",identity)
        self.assertEqual(resolved,pin)
        identity["identity_evidence"]["path"]="source/unknown/RX-OTA-SOURCE-EVIDENCE.json"
        with self.assertRaisesRegex(ValueError,"unrecognized relative"):
            PREP.resolve_supported_identity_pin(self.root,row,"bundle-NX30",identity)
        identity["identity_evidence"]["path"]=PREP.RX_IDENTITY_RELATIVE_PATH
        row["logical_coverage"].append(dict(row["logical_coverage"][0]))
        with self.assertRaisesRegex(ValueError,"exactly one RX logical receipt"):
            PREP.resolve_supported_identity_pin(self.root,row,"bundle-NX30",identity)

if __name__ == "__main__":
    unittest.main(verbosity=2)
