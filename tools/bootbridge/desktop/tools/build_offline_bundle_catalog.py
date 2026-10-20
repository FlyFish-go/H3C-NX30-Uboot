#!/usr/bin/env python3
"""Compile exact-source one-output bundle metadata and delta data sidecar.

Input programs are audited BBDP recipes. The sidecar contains only XOR and
LITERAL delta bytes, never complete official or combined firmware images.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import sys

HERE=pathlib.Path(__file__).resolve().parent
SPEC=importlib.util.spec_from_file_location("bundle_catalog_assembler",HERE/"assemble_release.py")
ASM=importlib.util.module_from_spec(SPEC);sys.modules[SPEC.name]=ASM;SPEC.loader.exec_module(ASM)
PREP_SPEC=importlib.util.spec_from_file_location("bundle_catalog_preparation",HERE/"prepare_offline_bundles.py")
PREP=importlib.util.module_from_spec(PREP_SPEC);sys.modules[PREP_SPEC.name]=PREP;PREP_SPEC.loader.exec_module(PREP)
MODEL={"RT3000":"BB_MODEL_RT3000","RW3000":"BB_MODEL_RW3000","RC3000":"BB_MODEL_RC3000","RX3000":"BB_MODEL_RX3000","NX30":"BB_MODEL_NX30"}
BOARD={"A":"BB_BOARD_HW_A","B":"BB_BOARD_HW_B","C":"BB_BOARD_HW_C","D":"BB_BOARD_HW_D"}
ALLOWED={"RT3000":set(BOARD),"RW3000":set(BOARD),"RC3000":set(BOARD),"RX3000":set(BOARD),"NX30":{"D"}}
SCHEMA="bootbridge-offline-prepared-bundles-v1"

def cstr(s:str)->str:return '"'+''.join("\\%03o"%b for b in s.encode("utf-8"))+'"'
def cbytes(b:bytes)->str:return "{"+",".join("0x%02x"%x for x in b)+"}"
def sha(data:bytes)->str:return hashlib.sha256(data).hexdigest()

def same_supported_identities(audit_rows,prepared_rows):
    if not isinstance(audit_rows,list) or not isinstance(prepared_rows,list) or len(audit_rows)!=len(prepared_rows):
        return False
    for audited,prepared in zip(audit_rows,prepared_rows):
        if not isinstance(audited,dict) or not isinstance(prepared,dict):return False
        for key in ("product","source_sha256","hardware_classes"):
            if audited.get(key)!=prepared.get(key):return False
        audit_pin=audited.get("identity_evidence");prepared_pin=prepared.get("identity_evidence")
        if (audit_pin is None)!=(prepared_pin is None):return False
        if audit_pin is not None:
            # Relative evidence paths are resolved against the RESULT directory
            # during preparation, so path spelling can change while the exact
            # receipt remains bound by byte count and SHA-256.
            if (audit_pin.get("bytes")!=prepared_pin.get("bytes") or
                    audit_pin.get("sha256")!=prepared_pin.get("sha256")):
                return False
    return True

def build(prepared_path:pathlib.Path,program_root:pathlib.Path,output:pathlib.Path)->dict:
    prepared_path=prepared_path.resolve(strict=True);program_root=program_root.resolve(strict=True)
    raw=prepared_path.read_bytes();manifest=json.loads(raw)
    if manifest.get("schema")!=SCHEMA or manifest.get("status")!="PASS":raise ValueError("prepared bundle manifest is not PASS")
    release=manifest.get("release_set");engine=manifest.get("engine_revision");bundles=manifest.get("bundles")
    if not isinstance(release,str) or not release or type(engine)is not int or engine<1 or not isinstance(bundles,list) or not 1<=len(bundles)<=5:
        raise ValueError("invalid prepared bundle catalog identity")
    seen_ids=set();seen_sources=set();compiled=[];blob=bytearray();evidence=[]
    for row in bundles:
        bid=row.get("bundle_id")
        if not isinstance(bid,str) or not re.fullmatch(r"[A-Za-z0-9._-]{1,96}",bid) or bid in seen_ids:raise ValueError("invalid/duplicate bundle id")
        seen_ids.add(bid)
        official=row.get("official",{});model=official.get("model");version=official.get("version");source_sha=official.get("sha256");source_size=official.get("bytes")
        if model not in MODEL or not isinstance(version,str) or not version or not isinstance(source_sha,str) or not re.fullmatch(r"[0-9a-f]{64}",source_sha) or type(source_size)is not int or source_size<1:
            raise ValueError(f"{bid}: invalid exact source fingerprint")
        if source_sha in seen_sources:raise ValueError("duplicate source SHA would make bundle selection ambiguous")
        seen_sources.add(source_sha)
        source_identity=row.get("source_identity")
        if (not isinstance(source_identity,dict) or source_identity.get("header_product")!=model or
            source_identity.get("version")!=version or source_identity.get("sha256")!=source_sha or source_identity.get("bytes")!=source_size):
            raise ValueError(f"{bid}: source_identity does not match canonical source metadata")
        candidate_set_id=row.get("candidate_set_id")
        profiles=row.get("profile_sets");provider_rows=row.get("provider_rows")
        if not isinstance(candidate_set_id,str) or not candidate_set_id or not isinstance(profiles,dict) or not profiles or not isinstance(provider_rows,list) or not provider_rows:
            raise ValueError(f"{bid}: ABI-v2 profile/provider inventories are required")
        if any(not isinstance(p,dict) or p.get("candidate_set_id")!=candidate_set_id for p in provider_rows):
            raise ValueError(f"{bid}: provider row candidate-set binding mismatch")
        classes=row.get("provider_classes")
        identities=row.get("supported_product_identities")
        if not isinstance(identities,list) or not identities:raise ValueError(f"{bid}: missing supported source identities")
        identity_map={}
        for identity in identities:
            if not isinstance(identity,dict) or identity.get("product") not in MODEL or identity.get("source_sha256")!=source_sha:
                raise ValueError(f"{bid}: invalid supported source identity")
            product=identity["product"];hw=identity.get("hardware_classes")
            if product in identity_map or not isinstance(hw,list) or not hw or len(set(hw))!=len(hw) or any(c not in ALLOWED[product] for c in hw):
                raise ValueError(f"{bid}: invalid source identity class set")
            identity_pin=identity.get("identity_evidence")
            if identity_pin is not None and not isinstance(identity_pin,dict):raise ValueError(f"{bid}: malformed identity evidence pin")
            if product=="RX3000" and model=="NX30" and not isinstance(identity_pin,dict):
                raise ValueError(f"{bid}: RX logical source alias lacks exact identity evidence")
            if identity_pin is not None:
                ipath=pathlib.Path(identity_pin.get("path","")).resolve(strict=True);idata=ipath.read_bytes()
                if identity_pin.get("bytes")!=len(idata) or identity_pin.get("sha256")!=sha(idata):
                    raise ValueError(f"{bid}: identity evidence pin mismatch")
            identity_map[product]=hw
        class_union={c for values in identity_map.values() for c in values}
        if model not in identity_map or not isinstance(classes,list) or not classes or len(classes)>256 or any(c not in BOARD for c in classes) or set(classes)!=class_union:
            raise ValueError(f"{bid}: invalid audited provider set")
        if not isinstance(row.get("dispatcher"),dict) or not re.fullmatch(r"[0-9a-f]{64}",row["dispatcher"].get("sha256","")) or row["dispatcher"].get("records_count")!=len(provider_rows):
            raise ValueError(f"{bid}: dispatcher evidence missing or inconsistent")
        ps=row.get("program",{});rel=ps.get("path")
        if not isinstance(rel,str):raise ValueError(f"{bid}: missing BBDP program")
        pure=pathlib.PurePosixPath(rel)
        if pure.is_absolute() or ".." in pure.parts or "\\" in rel:raise ValueError(f"{bid}: unsafe program path")
        ppath=(program_root/pathlib.Path(*pure.parts)).resolve(strict=True)
        if program_root not in ppath.parents:raise ValueError(f"{bid}: program escaped prepared directory")
        program=ppath.read_bytes()
        if ps.get("bytes")!=len(program) or ps.get("sha256")!=sha(program):raise ValueError(f"{bid}: BBDP hash mismatch")
        ops,data,out_size=ASM._parse_plan(program,source_size)
        target=row.get("candidate",{})
        if out_size!=target.get("bytes") or not isinstance(target.get("sha256"),str) or not re.fullmatch(r"[0-9a-f]{64}",target["sha256"]):
            raise ValueError(f"{bid}: BBDP output metadata mismatch")
        if not data or len(data)>=out_size:raise ValueError(f"{bid}: delta data is empty or would embed a complete target image")
        report_spec=row.get("build_report",{});audit_spec=row.get("audit_report",{})
        for spec,label in ((report_spec,"build report"),(audit_spec,"audit report")):
            if not isinstance(spec,dict) or not isinstance(spec.get("path"),str):raise ValueError(f"{bid}: missing pinned {label}")
            p=pathlib.Path(spec["path"]).resolve(strict=True);raw_report=p.read_bytes()
            if spec.get("sha256")!=sha(raw_report):raise ValueError(f"{bid}: {label} SHA mismatch")
            if label=="build report":report_obj=json.loads(raw_report)
            else:audit_obj=json.loads(raw_report)
        if (report_obj.get("schema")!="bootbridge-provider-bundle-v2" or report_obj.get("status")!="OFFLINE_LAB_CANDIDATE" or
            report_obj.get("official",{}).get("model")!=model or report_obj.get("official",{}).get("version")!=version or
            report_obj.get("official",{}).get("sha256")!=source_sha or report_obj.get("official",{}).get("bytes")!=source_size or
            report_obj.get("candidate",{}).get("sha256")!=target["sha256"] or report_obj.get("candidate",{}).get("bytes")!=out_size or
            sorted({p.get("hardware_class") for p in report_obj.get("providers",[])})!=classes or report_obj.get("dispatcher")!=row.get("dispatcher") or
            report_obj.get("profile_sets")!=profiles or report_obj.get("providers")!=provider_rows or
            report_obj.get("candidate_set_id")!=candidate_set_id or
            report_obj.get("candidate_archive_sha256")!=row.get("candidate_archive_sha256") or
            report_obj.get("candidate_archive_bytes")!=row.get("candidate_archive_bytes") or
            report_obj.get("candidate_manifest_sha256")!=row.get("candidate_manifest_sha256") or
            report_obj.get("candidate_manifest_bytes")!=row.get("candidate_manifest_bytes") or
            report_obj.get("candidate_descriptors_sha256")!=row.get("candidate_descriptors_sha256") or
            report_obj.get("candidate_descriptors_bytes")!=row.get("candidate_descriptors_bytes") or
            report_obj.get("candidate_inventory")!=row.get("candidate_inventory") or
            report_obj.get("source_ineligible_candidates")!=row.get("source_ineligible_candidates") or
            report_obj.get("candidate_archive_candidate_count")!=row.get("candidate_archive_candidate_count") or
            report_obj.get("nand_catalog_sha256")!=row.get("nand_catalog_sha256")):
            raise ValueError(f"{bid}: prepared catalog metadata differs from pinned bundle report")
        PREP.validate_v2_metadata(report_obj,bid)
        if (audit_obj.get("schema")!="bootbridge-provider-bundle-audit-v2" or audit_obj.get("status")!="PASS" or
            audit_obj.get("bundle_status")!="OFFLINE_LAB_CANDIDATE" or
            audit_obj.get("build_report_sha256")!=report_spec.get("sha256") or
            audit_obj.get("official_sha256")!=source_sha or
            audit_obj.get("profile_sets")!=profiles or audit_obj.get("providers")!=provider_rows or
            audit_obj.get("candidate_set_id")!=candidate_set_id or
            audit_obj.get("candidate_inventory")!=row.get("candidate_inventory") or
            audit_obj.get("source_ineligible_candidates")!=row.get("source_ineligible_candidates") or
            audit_obj.get("candidate_archive_candidate_count")!=row.get("candidate_archive_candidate_count") or
            audit_obj.get("candidate_set_id")!=candidate_set_id or
            audit_obj.get("provider_records")!=len(provider_rows) or
            audit_obj.get("nand_profile_count")!=sum(len(x.get("profile_ids",[])) for x in profiles.values()) or
            any(audit_obj.get(k)!=row.get(k) for k in ("candidate_archive_sha256","candidate_archive_bytes",
                "candidate_manifest_sha256","candidate_manifest_bytes","candidate_descriptors_sha256","candidate_descriptors_bytes"))):
            raise ValueError(f"{bid}: pinned independent audit does not validate bundle report")
        coverage=row.get("logical_coverage",[])
        if not isinstance(coverage,list):raise ValueError(f"{bid}: invalid logical coverage list")
        seen_coverage=set()
        for entry in coverage:
            product=entry.get("model") if isinstance(entry,dict) else None
            providers=entry.get("providers") if isinstance(entry,dict) else None
            evidence_spec=entry.get("evidence") if isinstance(entry,dict) else None
            if (product not in identity_map or not isinstance(providers,list) or not providers or
                    len(set(providers))!=len(providers) or not set(providers).issubset(identity_map.get(product,[]))):
                raise ValueError(f"{bid}: logical coverage mapping disagrees with source identity pins")
            pairs={(product,provider) for provider in providers}
            if pairs & seen_coverage:raise ValueError(f"{bid}: duplicate logical coverage row")
            seen_coverage.update(pairs)
            if not isinstance(evidence_spec,dict) or not isinstance(evidence_spec.get("path"),str):raise ValueError(f"{bid}: coverage receipt path missing")
            epath=pathlib.Path(evidence_spec["path"]).resolve(strict=True);eraw=epath.read_bytes()
            if evidence_spec.get("bytes")!=len(eraw) or evidence_spec.get("sha256")!=sha(eraw):raise ValueError(f"{bid}: coverage receipt hash mismatch")
            receipt=json.loads(eraw)
            expected_candidate_ids=sorted(p["candidate_id"] for p in provider_rows if p.get("hardware_class") in providers)
            expected={"schema":"bootbridge-logical-coverage-receipt-v1","status":"PASS_LOGICAL_COVERAGE_PROVENANCE",
                      "software_contract_status":"SOFTWARE_CONTRACT_ONLY","bundle_id":bid,
                      "source_model":model,"source_version":version,"source_sha256":source_sha,
                      "candidate_bytes":out_size,"candidate_sha256":target["sha256"],
                      "model":product,"providers":providers,
                      "candidate_ids":expected_candidate_ids,"provider_records":len(expected_candidate_ids),
                      "build_report_sha256":report_spec["sha256"],"audit_report_sha256":audit_spec["sha256"],
                      "dispatcher_sha256":row["dispatcher"]["sha256"],"hardware_validation":"NOT_EXECUTED",
                      "physical_install_qualification":"NOT_GRANTED"}
            if any(receipt.get(k)!=v for k,v in expected.items()):raise ValueError(f"{bid}: coverage receipt does not bind catalog source and output")
            identity_evidence=next(x.get("identity_evidence") for x in identities if x["product"]==product)
            if identity_evidence is not None:
                receipt_identity=receipt.get("identity_evidence")
                if not isinstance(receipt_identity,dict) or not isinstance(receipt_identity.get("path"),str):
                    raise ValueError(f"{bid}: logical coverage receipt omits identity evidence pin")
                if (receipt_identity.get("bytes")!=identity_evidence.get("bytes") or
                    receipt_identity.get("sha256")!=identity_evidence.get("sha256")):
                    raise ValueError(f"{bid}: logical coverage identity evidence pin mismatch")
        data_offset=len(blob);blob.extend(data);op_specs=[]
        for kind,offset,size,dataoff in ops:
            kind_name={1:"BB_PLAN_COPY",2:"BB_PLAN_LITERAL",3:"BB_PLAN_XOR"}[kind]
            ptr="NULL" if kind==1 else "bb_offline_data+%dULL"%(data_offset+dataoff)
            op_specs.append("{%s,%dULL,%dULL,%s}"%(kind_name,offset,size,ptr))
        display_labels=[model]
        for coverage_row in row.get("logical_coverage",[]):
            if coverage_row["model"] not in display_labels:display_labels.append(coverage_row["model"])
        display_models=" / ".join(display_labels)
        compiled.append({"id":bid,"model":model,"display_models":display_models,"version":version,"source_sha":source_sha,"source_size":source_size,
                         "target_sha":target["sha256"],"target_size":out_size,"ops":op_specs,"data_bytes":len(data),"providers":classes})
        evidence.append({"bundle_id":bid,"official":official,"display_models":display_models,"source_identity":row.get("source_identity"),
                         "supported_product_identities":row.get("supported_product_identities"),
                         "profile_sets":profiles,"candidate_set_id":candidate_set_id,
                         "candidate_archive_sha256":row.get("candidate_archive_sha256"),
                         "candidate_archive_bytes":row.get("candidate_archive_bytes"),
                         "candidate_manifest_sha256":row.get("candidate_manifest_sha256"),
                         "candidate_manifest_bytes":row.get("candidate_manifest_bytes"),
                         "candidate_descriptors_sha256":row.get("candidate_descriptors_sha256"),
                         "candidate_descriptors_bytes":row.get("candidate_descriptors_bytes"),
                         "candidate_inventory":row.get("candidate_inventory"),
                         "source_ineligible_candidates":row.get("source_ineligible_candidates"),
                         "candidate_archive_candidate_count":row.get("candidate_archive_candidate_count"),
                         "nand_catalog_sha256":row.get("nand_catalog_sha256"),
                         "provider_rows":provider_rows,
                         "candidate":target,"providers":classes,"dispatcher":row["dispatcher"],
                         "program":{"path":str(ppath),"bytes":len(program),"sha256":sha(program),"data_bytes":len(data),"operations":len(ops)},
                         "build_report":report_spec,"audit_report":audit_spec,
                         "logical_coverage":row.get("logical_coverage",[])})
    if len(blob)>64*1024*1024:raise ValueError("delta sidecar exceeds loader limit")
    lines=["/* Generated by tools/build_offline_bundle_catalog.py; do not edit. */","#ifndef BB_OFFLINE_CATALOG_GENERATED_H","#define BB_OFFLINE_CATALOG_GENERATED_H",
           "#include <stddef.h>","#include <stdint.h>","#define BB_OFFLINE_DATA_SIZE %dULL"%len(blob),
           "static const unsigned char bb_offline_expected_sha256[32] = %s;"%cbytes(hashlib.sha256(blob).digest()),
           "#ifndef BB_RELEASE_SET_ID","#define BB_RELEASE_SET_ID %s"%cstr(release),"#endif","#ifndef BB_ENGINE_REVISION","#define BB_ENGINE_REVISION %dU"%engine,"#endif"]
    for i,b in enumerate(compiled):
        lines.append("static bb_plan_op bb_offline_ops_%d[] = {"%i)
        lines.extend(op+"," for op in b["ops"]);lines.append("};")
        lines.append("static bb_plan_recipe bb_offline_plan_%d = {%s,%dULL,%s,%dULL,%s,bb_offline_ops_%d,%dU};"%
                     (i,cstr(b["id"]),b["source_size"],cbytes(bytes.fromhex(b["source_sha"])),b["target_size"],cbytes(bytes.fromhex(b["target_sha"])),i,len(b["ops"])))
        lines.append("static const bb_board bb_offline_providers_%d[] = {%s};"%(i,",".join(BOARD[c] for c in b["providers"])))
    lines.append("static bb_bundle_rule bb_offline_bundles[] = {")
    for i,b in enumerate(compiled):lines.append("{%s,%s,%s,%s,BB_RELEASE_SET_ID,%dU,&bb_offline_plan_%d,bb_offline_providers_%d,%dU},"%(cstr(b["id"]),MODEL[b["model"]],cstr(b["display_models"]),cstr(b["version"]),engine,i,i,len(b["providers"])))
    lines.extend(["};","#define BB_OFFLINE_BUNDLE_COUNT (sizeof(bb_offline_bundles)/sizeof(bb_offline_bundles[0]))","#endif",""])
    output.mkdir(parents=True,exist_ok=True);header=output/"bb_offline_catalog_generated.h";sidecar=output/"BootBridge.rules.dat"
    header.write_text("\n".join(lines),encoding="ascii",newline="\n");sidecar.write_bytes(blob)
    result={"schema":"bootbridge-offline-bundle-catalog-build-v1","status":"PASS","catalog_schema":"bootbridge-offline-bundle-catalog-v1",
            "release_set":release,"engine_revision":engine,"prepared_manifest":{"path":str(prepared_path),"sha256":sha(raw)},
            "bundle_count":len(compiled),"bundles":evidence,
            "generated":{"header":{"path":str(header),"bytes":header.stat().st_size,"sha256":sha(header.read_bytes())},
                         "sidecar":{"path":str(sidecar),"bytes":sidecar.stat().st_size,"sha256":sha(sidecar.read_bytes()),"data_bytes_are":"BBDP XOR/LITERAL deltas only"}}}
    (output/"bundle-catalog-manifest.json").write_text(json.dumps(result,sort_keys=True,indent=2)+"\n",encoding="utf-8")
    return result

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument("prepared_manifest",type=pathlib.Path);ap.add_argument("program_root",type=pathlib.Path);ap.add_argument("output_dir",type=pathlib.Path);a=ap.parse_args()
    try:m=build(a.prepared_manifest,a.program_root,a.output_dir)
    except Exception as e:print(f"offline bundle catalog refused: {e}",file=sys.stderr);return 2
    print(f"bundles={m['bundle_count']} delta_sidecar_sha256={m['generated']['sidecar']['sha256']}");return 0
if __name__=="__main__":raise SystemExit(main())
