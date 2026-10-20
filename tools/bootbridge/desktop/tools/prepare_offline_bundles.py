#!/usr/bin/env python3
"""Verify audited provider bundles and compile source-to-target delta recipes.

Full official and target images are read only from the private build evidence;
only deterministic BBDP delta programs are written to the prepared output.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("offline_bundle_assembler", HERE / "assemble_release.py")
ASM = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ASM
SPEC.loader.exec_module(ASM)
MODEL_CLASSES = {"RC3000":set(("A","B","C","D")),
                 "RW3000":set(("A","B","C","D")),
                 "RT3000":set(("A","B","C","D")),
                 "RX3000":set(("A","B","C","D")),
                 "NX30":set(("D",))}
BOARD_MASK={"A":1,"B":2,"C":4,"D":8}
NAND_RUNTIME_KEYS = {"profile_id", "model_aliases", "jedec_bytes_observed", "manufacturer_id",
                     "chip_id", "capacity_mib", "page_bytes", "erase_bytes", "raw_spare_bytes",
                     "mtd_oob_bytes", "ecc_bits", "catalogue_eligible"}
NAND_RAW_KEYS = {"profile_id","oem_model","qpic_model","model_aliases","jedec_bytes_observed","manufacturer_id","chip_id",
                 "capacity_bytes","capacity_mib","qpic_capacity_bytes","qpic_capacity_mib","page_bytes","pages_per_block","erase_bytes","raw_spare_bytes",
                 "qpic_raw_spare_bytes","qpic_mtd_oob_ecc4_bytes","qpic_mtd_oob_ecc8_bytes","appsbl_ecc_mode_mask",
                 "appsbl_source_mask","appsbl_ecc4_source_mask","appsbl_ecc8_source_mask","kernel_oem_oob_field_mask",
                 "kernel_ecc_mode_mask","kernel_ecc_step_bytes","kernel_image_rows","qpic_ecc_mode_mask",
                 "runtime_ecc_mode_mask","capabilities","source_table_mask"}

def validate_v2_metadata(report: dict, bid: str) -> tuple[list, list]:
    """Require ABI-v2 NAND/candidate inventories before accepting a bundle."""
    if report.get("schema") != "bootbridge-provider-bundle-v2":
        raise ValueError(f"{bid}: ABI-v2 provider bundle report required")
    candidate_set_id=report.get("candidate_set_id")
    if not isinstance(candidate_set_id,str) or not candidate_set_id or len(candidate_set_id)>128:
        raise ValueError(f"{bid}: missing ABI-v2 candidate_set_id")
    for key in ("candidate_archive_sha256","candidate_manifest_sha256","candidate_descriptors_sha256"):
        if not isinstance(report.get(key),str) or not re.fullmatch(r"[0-9a-f]{64}",report[key]):
            raise ValueError(f"{bid}: missing exact {key}")
    for key in ("candidate_archive_bytes","candidate_manifest_bytes","candidate_descriptors_bytes"):
        if type(report.get(key)) is not int or report[key]<1:
            raise ValueError(f"{bid}: missing exact {key}")
    source_sha=report.get("official",{}).get("sha256")
    nand_catalog_sha=report.get("nand_catalog_sha256")
    if not isinstance(nand_catalog_sha,str) or not re.fullmatch(r"[0-9a-f]{64}",nand_catalog_sha):
        raise ValueError(f"{bid}: missing exact NAND catalog digest")
    profile_sets=report.get("profile_sets")
    providers=report.get("providers")
    if not isinstance(profile_sets,dict) or not profile_sets or not isinstance(providers,list) or not providers:
        raise ValueError(f"{bid}: ABI-v2 profile sets/provider multiset are required")
    profiles_by_set={};profile_ids_by_set={}
    for set_id,profile_set in profile_sets.items():
        if not isinstance(profile_set,dict) or profile_set.get("profile_set_id")!=set_id or profile_set.get("source_sha256")!=source_sha:
            raise ValueError(f"{bid}: profile set does not bind exact source and NAND catalog")
        catalog_sha=profile_set.get("catalog_sha256");source_variants=profile_set.get("source_variants")
        if catalog_sha!=nand_catalog_sha or not isinstance(catalog_sha,str) or not re.fullmatch(r"[0-9a-f]{64}",catalog_sha) or not isinstance(source_variants,list) or not source_variants or len(set(source_variants))!=len(source_variants) or any(x not in {"A0","A1","B","C","D"} for x in source_variants):
            raise ValueError(f"{bid}: malformed exact NAND catalog/source variant metadata")
        profiles=profile_set.get("profiles")
        runtime=profile_set.get("runtime_profiles")
        ids=profile_set.get("profile_ids")
        if not isinstance(profiles,list) or not profiles or not isinstance(runtime,list) or not runtime or not isinstance(ids,list) or not ids:
            raise ValueError(f"{bid}: malformed ABI-v2 NAND profile inventory")
        seen=set()
        for profile in profiles:
            if not isinstance(profile,dict) or set(profile)!=NAND_RAW_KEYS:
                raise ValueError(f"{bid}: malformed raw NAND catalog profile row")
            pid=profile.get("profile_id")
            if not isinstance(pid,str) or not pid or pid in seen:raise ValueError(f"{bid}: invalid or duplicate NAND profile id")
            seen.add(pid)
            aliases=profile.get("model_aliases")
            if (not isinstance(aliases,list) or not aliases or any(not isinstance(x,str) or not x for x in aliases)
                    or len(set(aliases))!=len(aliases)):
                raise ValueError(f"{bid}: invalid NAND profile model aliases")
            for key in ("manufacturer_id", "chip_id", "capacity_mib", "page_bytes","erase_bytes","raw_spare_bytes"):
                if type(profile.get(key)) is not int or profile[key]<0:
                    raise ValueError(f"{bid}: invalid NAND profile numeric field {key}")
        if len(set(ids))!=len(ids) or any(x not in seen for x in ids):raise ValueError(f"{bid}: invalid eligible profile ID list")
        runtime_ids=[]
        raw_by_id={p["profile_id"]:p for p in profiles}
        runtime_seen=set()
        for profile in runtime:
            if not isinstance(profile,dict) or set(profile)!=NAND_RUNTIME_KEYS:
                raise ValueError(f"{bid}: malformed runtime NAND profile tuple")
            pid=profile.get("profile_id");raw_profile=raw_by_id.get(pid)
            if raw_profile is None:raise ValueError(f"{bid}: runtime profile is absent from raw catalog")
            for key in ("model_aliases","jedec_bytes_observed","manufacturer_id","chip_id","capacity_mib","page_bytes","erase_bytes","raw_spare_bytes"):
                if profile.get(key)!=raw_profile.get(key):raise ValueError(f"{bid}: runtime profile {key} differs from raw catalog")
            if profile.get("ecc_bits") not in (4,8) or profile.get("mtd_oob_bytes") not in (64,128,256) or type(profile.get("catalogue_eligible")) is not int or not 0<=profile["catalogue_eligible"]<=15:
                raise ValueError(f"{bid}: malformed runtime NAND ECC/OOB/board mask")
            if pid not in runtime_seen:runtime_seen.add(pid);runtime_ids.append(pid)
        if ids!=runtime_ids:raise ValueError(f"{bid}: eligible NAND profile IDs differ from ordered runtime tuples")
        canonical=json.dumps({"source_sha256":source_sha,"catalog_sha256":catalog_sha,
                              "source_variants":source_variants,
                              "profiles":ids},
                             sort_keys=True,separators=(",",":"),ensure_ascii=True).encode("utf-8")
        expected_id=f"nandset-{source_sha[:12]}-{hashlib.sha256(canonical).hexdigest()[:16]}"
        if set_id!=expected_id:raise ValueError(f"{bid}: profile_set_id does not bind exact source/catalog/profile set")
        profiles_by_set[set_id]=runtime;profile_ids_by_set[set_id]=seen
    candidate_rows=[];row_ids=set();candidate_ids=set();candidate_contracts=[]
    for provider in providers:
        if not isinstance(provider,dict):raise ValueError(f"{bid}: malformed ABI-v2 provider row")
        rid=provider.get("record_id");cid=provider.get("candidate_id");set_id=provider.get("profile_set_id")
        if not isinstance(rid,str) or not rid or rid in row_ids or not isinstance(cid,str) or not cid or cid in candidate_ids:
            raise ValueError(f"{bid}: duplicate or missing ABI-v2 provider/candidate id")
        row_ids.add(rid);candidate_ids.add(cid)
        if set_id not in profiles_by_set or provider.get("candidate_set_id")!=report.get("candidate_set_id"):
            raise ValueError(f"{bid}: provider row references an unknown profile/candidate set")
        row_profiles=provider.get("nand_profiles")
        if not isinstance(row_profiles,list) or not row_profiles or any(not isinstance(x,dict) for x in row_profiles):
            raise ValueError(f"{bid}: provider row has invalid NAND profile subset")
        profile_id=provider.get("nand_profile_id")
        board_mask=BOARD_MASK.get(provider.get("hardware_class"))
        expected_profiles=[]
        for profile in profiles_by_set[set_id]:
            if profile.get("profile_id")==profile_id and profile.get("ecc_bits")==provider.get("storage_ecc_bits"):
                exact=dict(profile);exact["catalogue_eligible"]=board_mask;expected_profiles.append(exact)
        expected_profiles.sort(key=lambda p:(p["mtd_oob_bytes"],p["ecc_bits"]))
        actual_profiles=sorted(row_profiles,key=lambda p:(p.get("mtd_oob_bytes",-1),p.get("ecc_bits",-1)))
        if (not expected_profiles or actual_profiles!=expected_profiles or
                provider.get("hardware_class") not in BOARD_MASK):
            raise ValueError(f"{bid}: provider row does not bind its exact NAND profile tuple")
        nand=provider.get("nand")
        if (not isinstance(nand,dict) or nand.get("profile_id")!=profile_id or
                nand.get("ecc_bits")!=provider.get("storage_ecc_bits") or
                not any(nand.get("mtd_oob_bytes")==p.get("mtd_oob_bytes") and
                        all(nand.get(k)==p.get(k) for k in ("model_aliases","manufacturer_id","chip_id","capacity_mib","page_bytes","erase_bytes","raw_spare_bytes"))
                        for p in row_profiles)):
            raise ValueError(f"{bid}: canonical NAND summary does not reference one exact tuple in its allowed list")
        if provider.get("hardware_class") not in {"A","B","C","D"}:
            raise ValueError(f"{bid}: provider row has invalid board class")
        payload=provider.get("payload");desc=payload.get("descriptor") if isinstance(payload,dict) else None
        try:
            partition=desc["files"]["appsbl.partition.bin"];mbn=desc["files"]["appsbl.mbn"];load=desc["container"]["load"]
            cid=provider["candidate_id"];board=provider["hardware_class"]
            load_ecc=provider["load_ecc_bits"];storage_ecc=provider["storage_ecc_bits"]
            contract={"candidate_id":cid,"board":board,"nand_profile_id":profile_id,
                      "load_ecc_bits":load_ecc,"storage_ecc_bits":storage_ecc,
                      "partition_sha256":partition["sha256"],"mbn_sha256":mbn["sha256"],
                      "load_sha256":load["sha256"],"partition_bytes":partition["bytes"]}
        except (KeyError,TypeError) as exc:
            raise ValueError(f"{bid}: candidate row lacks exact descriptor-bound payload contract") from exc
        if (type(load_ecc) is not int or type(storage_ecc) is not int or
                any(not isinstance(contract[k],str) or not re.fullmatch(r"[0-9a-f]{64}",contract[k])
                    for k in ("partition_sha256","mbn_sha256","load_sha256")) or
                type(contract["partition_bytes"]) is not int or contract["partition_bytes"]<1):
            raise ValueError(f"{bid}: malformed exact candidate payload contract")
        if (desc.get("candidate_id")!=cid or payload.get("candidate_id")!=cid or
                payload.get("sha256")!=partition["sha256"] or payload.get("bytes")!=partition["bytes"] or
                payload.get("mbn")!=mbn or payload.get("load")!=load):
            raise ValueError(f"{bid}: provider payload does not repeat exact archive candidate descriptor")
        storage=payload.get("storage")
        storage_keys={"encoding","path","sha256","bytes","patch_header_sha256","patch_count","data_bytes"}
        if not isinstance(storage,dict) or set(storage)!=storage_keys:
            raise ValueError(f"{bid}: provider payload must pin its exact rootfs storage encoding")
        storage_sha=storage.get("sha256");header_sha=storage.get("patch_header_sha256")
        storage_path=storage.get("path")
        if (storage.get("encoding")!="base-patch-v1" or
                not isinstance(storage_path,str) or
                storage_path!=f"/etc/bridge/provider-payload-bases/{storage_sha}.bin" or
                not isinstance(storage_sha,str) or not re.fullmatch(r"[0-9a-f]{64}",storage_sha) or
                not isinstance(header_sha,str) or not re.fullmatch(r"[0-9a-f]{64}",header_sha) or
                type(storage.get("bytes")) is not int or storage["bytes"]<1 or
                type(storage.get("patch_count")) is not int or storage["patch_count"]<0 or
                type(storage.get("data_bytes")) is not int or storage["data_bytes"]<0 or
                ((storage["patch_count"]==0)!=(storage["data_bytes"]==0)) or
                storage["data_bytes"]>storage["bytes"]):
            raise ValueError(f"{bid}: malformed exact base-patch storage descriptor")
        candidate_contracts.append(contract)
        candidate_rows.append(provider)
    inventory=report.get("candidate_inventory")
    candidate_keys={"candidate_id","board","nand_profile_id","load_ecc_bits","storage_ecc_bits",
                    "partition_sha256","mbn_sha256","load_sha256","partition_bytes"}
    if not isinstance(inventory,list) or not inventory or any(not isinstance(x,dict) or set(x)!=candidate_keys for x in inventory):
        raise ValueError(f"{bid}: exact full candidate_inventory is required")
    if len({x["candidate_id"] for x in inventory})!=len(inventory):
        raise ValueError(f"{bid}: duplicate candidate identity in full archive inventory")
    for candidate in inventory:
        if (not isinstance(candidate["candidate_id"],str) or not candidate["candidate_id"] or
                candidate["board"] not in {"A","B","C","D"} or not isinstance(candidate["nand_profile_id"],str) or
                candidate["load_ecc_bits"] not in (4,8) or candidate["storage_ecc_bits"] not in (4,8) or
                any(not isinstance(candidate[k],str) or not re.fullmatch(r"[0-9a-f]{64}",candidate[k]) for k in ("partition_sha256","mbn_sha256","load_sha256")) or
                type(candidate["partition_bytes"]) is not int or candidate["partition_bytes"]<1):
            raise ValueError(f"{bid}: malformed full archive candidate inventory row")
    inventory_by_id={x["candidate_id"]:x for x in inventory}
    if any(inventory_by_id.get(row["candidate_id"])!=contract for row,contract in zip(candidate_rows,candidate_contracts)):
        raise ValueError(f"{bid}: provider candidates differ from full archive candidate inventory")
    omitted=[x for x in inventory if x["candidate_id"] not in candidate_ids]
    expected_omitted=[{"candidate_id":x["candidate_id"],"board":x["board"],"nand_profile_id":x["nand_profile_id"],
                       "reason":"NO_EXACT_SOURCE_KERNEL_PROFILE"} for x in omitted]
    if report.get("source_ineligible_candidates")!=expected_omitted or report.get("candidate_archive_candidate_count")!=len(inventory):
        raise ValueError(f"{bid}: source-ineligible candidate inventory differs from full archive inventory")
    canonical_candidates={"schema":"bootbridge-candidate-set-v1","source_sha256":source_sha,
                          "archive_sha256":report["candidate_archive_sha256"],
                          "candidates":sorted(inventory,key=lambda x:x["candidate_id"])}
    candidate_id="candset-"+hashlib.sha256(json.dumps(canonical_candidates,sort_keys=True,separators=(",",":"),ensure_ascii=True).encode("utf-8")).hexdigest()[:16]
    if candidate_id!=candidate_set_id:
        raise ValueError(f"{bid}: candidate_set_id does not bind the full archive inventory")
    candidates=candidate_rows
    profiles=report["profile_sets"]
    if any(row.get("candidate_set_id")!=candidate_set_id for row in candidates):
        raise ValueError(f"{bid}: provider rows disagree on candidate_set_id")
    return profiles,candidates

def file_pinned(base: pathlib.Path, spec: dict, label: str) -> tuple[pathlib.Path, bytes, str]:
    if not isinstance(spec, dict) or not isinstance(spec.get("path"), str):
        raise ValueError(f"{label}: missing exact path")
    p=pathlib.Path(spec["path"])
    if not p.is_absolute(): p=base/p
    p=p.resolve(strict=True)
    data=p.read_bytes(); sha=hashlib.sha256(data).hexdigest()
    if spec.get("sha256")!=sha or ("bytes" in spec and spec["bytes"]!=len(data)):
        raise ValueError(f"{label}: size or SHA-256 mismatch")
    return p,data,sha

RX_IDENTITY_RELATIVE_PATH = "source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json"

def resolve_supported_identity_pin(base: pathlib.Path, aggregate_row: dict, bundle_id: str,
                                   identity_row: dict) -> dict:
    """Resolve the one approved RX report-relative pin through its aggregate receipt.

    Bundle reports are rooted at the source workspace, while aggregate paths are
    rooted at RESULT.parent. Only this exact RX evidence relative path may be
    bridged, and only by one fully pinned RX logical-coverage receipt.
    """
    spec=identity_row.get("identity_evidence")
    if not isinstance(spec,dict) or not isinstance(spec.get("path"),str):
        raise ValueError(f"{bundle_id}: missing RX source identity pin")
    raw_path=pathlib.Path(spec["path"])
    is_rx=identity_row.get("product")=="RX3000"
    is_shared_rx=(is_rx and aggregate_row.get("official",{}).get("model")=="NX30")
    if not is_rx:
        return spec
    if raw_path.is_absolute() and not is_shared_rx:
        return spec
    if not is_shared_rx:
        raise ValueError(f"{bundle_id}: relative RX identity evidence is not supported outside the shared NX source")

    entries=[entry for entry in aggregate_row.get("logical_coverage",[])
             if isinstance(entry,dict) and entry.get("model")=="RX3000"]
    if len(entries)!=1:
        raise ValueError(f"{bundle_id}: shared NX/RX identity requires exactly one RX logical receipt")
    receipt_path,receipt_raw,_=file_pinned(base,entries[0].get("evidence"),
                                          f"{bundle_id} RX logical coverage receipt")
    try:
        receipt=json.loads(receipt_raw)
    except (UnicodeDecodeError,json.JSONDecodeError) as exc:
        raise ValueError(f"{bundle_id}: RX logical coverage receipt is not valid JSON") from exc
    receipt_pin=receipt.get("identity_evidence")
    if (not isinstance(receipt_pin,dict) or not isinstance(receipt_pin.get("path"),str) or
            not pathlib.Path(receipt_pin["path"]).is_absolute()):
        raise ValueError(f"{bundle_id}: RX logical receipt must contain an absolute identity-evidence pin")
    receipt_evidence_path,receipt_evidence_raw,receipt_evidence_sha=file_pinned(
        base,receipt_pin,f"{bundle_id} RX source identity receipt evidence")

    if raw_path.is_absolute():
        report_path,report_raw,report_sha=file_pinned(base,spec,f"{bundle_id} RX report identity evidence")
        if report_path!=receipt_evidence_path:
            raise ValueError(f"{bundle_id}: report and logical receipt resolve RX identity evidence differently")
    else:
        if spec["path"]!=RX_IDENTITY_RELATIVE_PATH:
            raise ValueError(f"{bundle_id}: unrecognized relative RX identity-evidence path")
        expected_parts=pathlib.PurePosixPath(RX_IDENTITY_RELATIVE_PATH).parts
        if receipt_evidence_path.parts[-len(expected_parts):]!=expected_parts:
            raise ValueError(f"{bundle_id}: pinned RX evidence path does not match the report's exact relative path")
        report_raw=receipt_evidence_raw
        report_sha=hashlib.sha256(report_raw).hexdigest()
    if (spec.get("sha256")!=report_sha or spec.get("bytes")!=len(report_raw) or
            receipt_pin.get("sha256")!=report_sha or receipt_pin.get("bytes")!=len(report_raw)):
        raise ValueError(f"{bundle_id}: RX report and aggregate identity evidence pins conflict")
    # Return only the absolute, fully checked form. Existing downstream receipt
    # contract checks still compare this pin against the RX logical receipt.
    return {"path":str(receipt_evidence_path),"bytes":len(report_raw),"sha256":report_sha}

def prepare(result_path: pathlib.Path, output: pathlib.Path) -> dict:
    result_path=result_path.resolve(strict=True); base=result_path.parent
    result_raw=result_path.read_bytes(); result=json.loads(result_raw)
    result_sha=hashlib.sha256(result_raw).hexdigest()
    if result.get("schema")!="bootbridge-provider-bundle-build-result-v1" or result.get("status")!="PASS":
        raise ValueError("provider bundle aggregate is not PASS")
    release_set=result.get("release_set"); engine=result.get("engine_revision")
    rows=result.get("bundles")
    if not isinstance(release_set,str) or not release_set or type(engine) is not int or engine<1 or not isinstance(rows,list) or not 1<=len(rows)<=5:
        raise ValueError("invalid release identity or source bundle rows")
    output.mkdir(parents=True,exist_ok=True); programs=output/"programs"; programs.mkdir()
    seen_ids=set(); seen_sources=set(); prepared=[]
    for row in rows:
        bid=row.get("bundle_id")
        if not isinstance(bid,str) or not re.fullmatch(r"[A-Za-z0-9._-]{1,96}",bid) or bid in seen_ids:
            raise ValueError("invalid or duplicate bundle_id")
        seen_ids.add(bid)
        official=row.get("official")
        if not isinstance(official,dict) or not isinstance(official.get("path"),str):
            raise ValueError(f"{bid}: aggregate official.path is required for exact delta compilation")
        src_path,source,src_sha=file_pinned(base,official,f"{bid} official source")
        if official.get("sha256")!=src_sha or official.get("bytes")!=len(source):
            raise ValueError(f"{bid}: official path/hash/bytes do not pin source bytes")
        if src_sha in seen_sources: raise ValueError("duplicate exact source fingerprint; bundle selection would be ambiguous")
        seen_sources.add(src_sha)
        report_path,report,report_sha=file_pinned(base,row.get("build_report"),f"{bid} build report")
        audit_path,audit_raw,audit_sha=file_pinned(base,row.get("audit_report"),f"{bid} audit report")
        report_obj=json.loads(report); audit=json.loads(audit_raw)
        if report_obj.get("schema")!="bootbridge-provider-bundle-v2" or report_obj.get("status")!="OFFLINE_LAB_CANDIDATE":
            raise ValueError(f"{bid}: ABI-v2 bundle report/status required")
        profiles,candidates=validate_v2_metadata(report_obj,bid)
        if (audit.get("schema")!="bootbridge-provider-bundle-audit-v2" or audit.get("status")!="PASS" or
                audit.get("bundle_status")!="OFFLINE_LAB_CANDIDATE" or audit.get("build_report_sha256")!=report_sha or
                audit.get("official_sha256")!=src_sha or audit.get("candidate_archive_sha256")!=report_obj.get("candidate_archive_sha256") or
                audit.get("candidate_set_id")!=report_obj.get("candidate_set_id") or
                audit.get("profile_sets")!=report_obj.get("profile_sets") or audit.get("providers")!=report_obj.get("providers") or
                audit.get("candidate_inventory")!=report_obj.get("candidate_inventory") or
                audit.get("source_ineligible_candidates")!=report_obj.get("source_ineligible_candidates") or
                audit.get("candidate_archive_candidate_count")!=report_obj.get("candidate_archive_candidate_count") or
                audit.get("provider_records")!=len(report_obj.get("providers",[])) or
                audit.get("nand_profile_count")!=sum(len(x.get("profile_ids",[])) for x in report_obj.get("profile_sets",{}).values())):
            raise ValueError(f"{bid}: independent audit does not pin build report")
        report_official=report_obj.get("official",{})
        model=report_official.get("model")
        if model not in MODEL_CLASSES or report_official.get("sha256")!=src_sha or report_official.get("bytes")!=len(source):
            raise ValueError(f"{bid}: report official identity does not pin exact source")
        for field in ("identity","model","version","sha256","bytes"):
            if field in official and official.get(field)!=report_official.get(field):
                raise ValueError(f"{bid}: report/aggregate official {field} mismatch")
        source_identity=report_obj.get("source_identity")
        if not isinstance(source_identity,dict) or source_identity.get("header_product")!=model or source_identity.get("version")!=report_official.get("version") or source_identity.get("sha256")!=src_sha or source_identity.get("bytes")!=len(source):
            raise ValueError(f"{bid}: report source_identity does not repeat official fingerprint")
        supported=report_obj.get("supported_product_identities")
        if not isinstance(supported,list) or not supported:raise ValueError(f"{bid}: missing supported source identities")
        identity_products=set();identity_classes=set();supported_normalized=[]
        for identity_row in supported:
            if not isinstance(identity_row,dict) or identity_row.get("product") not in MODEL_CLASSES or identity_row.get("source_sha256")!=src_sha:
                raise ValueError(f"{bid}: invalid supported source identity")
            product=identity_row["product"]; hw=identity_row.get("hardware_classes")
            if (product in identity_products or not isinstance(hw,list) or not hw or len(set(hw))!=len(hw) or
                any(c not in MODEL_CLASSES[product] for c in hw)):
                raise ValueError(f"{bid}: invalid supported identity hardware class set")
            identity_products.add(product);identity_classes.update(hw)
            normalized=dict(identity_row)
            if "identity_evidence" in identity_row:
                pin=resolve_supported_identity_pin(base,row,bid,identity_row)
                evidence_path,evidence_raw,evidence_sha=file_pinned(base,pin,f"{bid} {product} identity evidence")
                normalized["identity_evidence"]={"path":str(evidence_path),"bytes":len(evidence_raw),"sha256":evidence_sha}
            elif product=="RX3000" and model=="NX30":
                raise ValueError(f"{bid}: shared NX/RX source requires an independently pinned RX identity receipt")
            supported_normalized.append(normalized)
        if model not in identity_products:raise ValueError(f"{bid}: canonical source product identity is absent")
        report_source=report_obj.get("official_path")
        if not isinstance(report_source,str) or pathlib.Path(report_source).resolve(strict=True)!=src_path:
            raise ValueError(f"{bid}: aggregate official.path differs from audited exact source path")
        classes=[p.get("hardware_class") for p in report_obj.get("providers",[])]
        if not classes or len(classes)>256 or any(c not in {"A","B","C","D"} for c in classes) or set(classes)!=identity_classes:
            raise ValueError(f"{bid}: bundled provider classes disagree with explicit supported identity mappings")
        dispatcher=report_obj.get("dispatcher",{})
        if not dispatcher.get("sha256") or dispatcher.get("records_count")!=len(classes):
            raise ValueError(f"{bid}: bundle dispatcher evidence incomplete")
        target_path,target,target_sha=file_pinned(base,row.get("candidate"),f"{bid} combined candidate")
        if report_obj.get("candidate",{}).get("sha256")!=target_sha or report_obj.get("candidate",{}).get("bytes")!=len(target):
            raise ValueError(f"{bid}: candidate differs from audited build report")
        program=ASM.compile_plan_program(source,target)
        rebuilt,ops,data=ASM._reconstruct(source,program)
        if rebuilt!=target or hashlib.sha256(rebuilt).hexdigest()!=target_sha:
            raise ValueError(f"{bid}: delta does not reconstruct audited combined target")
        if not data or len(data)>=len(target):
            raise ValueError(f"{bid}: delta data is not smaller than the target; refusing to embed a complete output image")
        ppath=programs/(bid+".bbprog"); ppath.write_bytes(program)
        coverage=[]; seen_coverage=set(); raw_coverage=row.get("logical_coverage",[])
        if not isinstance(raw_coverage,list):raise ValueError(f"{bid}: logical_coverage must be an explicit list")
        for entry in raw_coverage:
            logical_model=entry.get("model") if isinstance(entry,dict) else None
            logical_providers=entry.get("providers") if isinstance(entry,dict) else None
            if (logical_model not in MODEL_CLASSES or not isinstance(logical_providers,list) or
                not logical_providers or len(set(logical_providers))!=len(logical_providers) or
                any(provider not in classes or provider not in MODEL_CLASSES.get(logical_model,set()) for provider in logical_providers)):
                raise ValueError(f"{bid}: invalid or unsupported logical coverage mapping")
            identity_match=next((x for x in supported_normalized if x["product"]==logical_model),None)
            pairs={(logical_model,provider) for provider in logical_providers}
            if (len(pairs & seen_coverage) or identity_match is None or
                    not set(logical_providers).issubset(identity_match.get("hardware_classes",[]))):
                raise ValueError(f"{bid}: logical coverage providers disagree with audited source identity mapping")
            seen_coverage.update(pairs)
            evidence_path,evidence_raw,evidence_sha=file_pinned(base,entry.get("evidence"),f"{bid} {logical_model} coverage evidence")
            receipt=json.loads(evidence_raw)
            expected_candidate_ids=sorted({p["candidate_id"] for p in report_obj["providers"]
                                           if p.get("hardware_class") in logical_providers})
            expected={"schema":"bootbridge-logical-coverage-receipt-v1","status":"PASS_LOGICAL_COVERAGE_PROVENANCE",
                      "software_contract_status":"SOFTWARE_CONTRACT_ONLY","bundle_id":bid,
                      "source_model":model,"source_version":report_official["version"],"source_sha256":src_sha,
                      "candidate_bytes":len(target),"candidate_sha256":target_sha,
                      "model":logical_model,"providers":logical_providers,
                      "candidate_ids":expected_candidate_ids,"provider_records":len(expected_candidate_ids),
                      "build_report_sha256":report_sha,"audit_report_sha256":audit_sha,
                      "dispatcher_sha256":dispatcher["sha256"],"hardware_validation":"NOT_EXECUTED",
                      "physical_install_qualification":"NOT_GRANTED"}
            if any(receipt.get(key)!=value for key,value in expected.items()):
                raise ValueError(f"{bid}: logical coverage receipt does not bind the exact source/provider/report/audit contract")
            normalized_evidence={"path":str(evidence_path),"bytes":len(evidence_raw),"sha256":evidence_sha}
            if "identity_evidence" in identity_match:
                receipt_pin=receipt.get("identity_evidence")
                receipt_path,receipt_raw,receipt_sha=file_pinned(base,receipt_pin,f"{bid} {logical_model} source identity evidence")
                if {"path":str(receipt_path),"bytes":len(receipt_raw),"sha256":receipt_sha}!=identity_match["identity_evidence"]:
                    raise ValueError(f"{bid}: logical coverage receipt omits its source identity evidence pin")
                normalized_evidence["identity_evidence"]=identity_match["identity_evidence"]
            elif receipt.get("identity_evidence") is not None:
                raise ValueError(f"{bid}: logical receipt has unpinned identity evidence")
            coverage.append({"model":logical_model,"providers":logical_providers,
                             "evidence":normalized_evidence})
        prepared.append({"bundle_id":bid,"official":{"identity":report_official["identity"],"model":model,"version":report_official["version"],"sha256":src_sha,"bytes":len(source)},
                         "source_identity":source_identity,"supported_product_identities":supported_normalized,
                         "profile_sets":report_obj["profile_sets"],"candidate_set_id":report_obj["candidate_set_id"],
                         "candidate_archive_sha256":report_obj["candidate_archive_sha256"],
                         "candidate_archive_bytes":report_obj["candidate_archive_bytes"],
                         "candidate_manifest_sha256":report_obj["candidate_manifest_sha256"],
                         "candidate_manifest_bytes":report_obj["candidate_manifest_bytes"],
                         "candidate_descriptors_sha256":report_obj["candidate_descriptors_sha256"],
                         "candidate_descriptors_bytes":report_obj["candidate_descriptors_bytes"],
                         "candidate_inventory":report_obj["candidate_inventory"],
                         "source_ineligible_candidates":report_obj["source_ineligible_candidates"],
                         "candidate_archive_candidate_count":report_obj["candidate_archive_candidate_count"],
                         "nand_catalog_sha256":report_obj["nand_catalog_sha256"],
                         "providers":report_obj["providers"],
                         "program":{"path":"programs/"+ppath.name,"bytes":len(program),"sha256":hashlib.sha256(program).hexdigest()},
                         "candidate":{"bytes":len(target),"sha256":target_sha},"provider_classes":sorted(set(classes)),
                         "provider_rows":report_obj["providers"],
                         "logical_coverage":coverage,
                         "build_report":{"path":str(report_path),"sha256":report_sha},"audit_report":{"path":str(audit_path),"sha256":audit_sha},
                         "dispatcher":dispatcher,"delta":{"operations":len(ops),"data_bytes":len(data)}})
    manifest={"schema":"bootbridge-offline-prepared-bundles-v1","status":"PASS","release_set":release_set,"engine_revision":engine,
              "aggregate_result":{"path":str(result_path),"sha256":result_sha},"bundle_count":len(prepared),"bundles":prepared}
    (output/"prepared-bundles.json").write_text(json.dumps(manifest,sort_keys=True,indent=2)+"\n",encoding="utf-8")
    return manifest

def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument("result_json",type=pathlib.Path); ap.add_argument("output_dir",type=pathlib.Path); args=ap.parse_args()
    try: m=prepare(args.result_json,args.output_dir)
    except Exception as e: print(f"offline bundle preparation refused: {e}",file=sys.stderr); return 2
    print(f"prepared={m['bundle_count']} delta programs")
    return 0
if __name__=="__main__": raise SystemExit(main())
