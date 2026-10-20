#!/usr/bin/env python3
"""Pinned four-source offline bridge builder. No target-device operations.

Run in the authoritative WSL tree. Inputs are ordinary OEM package files;
outputs are private, laboratory candidates, never an installation approval.
"""
# This builder uses assertions for package/DT preservation invariants. Python
# optimization removes them, so refuse that interpreter mode at import time as
# well as for the CLI, before any input reads or candidate/report output.
if not __debug__:
    raise RuntimeError("NX30 bridge builder requires assertions; Python optimization is unsupported")

import argparse
import ctypes
import ctypes.util
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import tarfile
import zlib

import vendor_fit
import vendor_ubi
import bridge_kernel as kernel_transform
import build_oem_manifest as oem_manifest
import build_return_source as return_source
import candidate_profile_codegen
import nand_schema
import candidate_manifest
import payload_patch_codegen
import root_binding_contract

SOURCE_SHA = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"
EPOCH = 1791907200  # Fixed build epoch; this is a prototype version, not OEM R013.
PROJECT = Path(__file__).resolve().parents[1]
PROFILE = kernel_transform.PROFILE
_OFFICIAL_BYTES = {
    SOURCE_SHA: 31981568,
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": 33554432,
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": 34078720,
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": 33554432,
}
OFFICIAL_SOURCES = {
    source_sha: {"sha256": source_sha, "identity": record[0], "bytes": _OFFICIAL_BYTES[source_sha],
                 "model": record[0].split("V100", 1)[0], "kernel_fit_sha256": record[1]}
    for source_sha, record in kernel_transform.SOURCES.items()
}

# Reviewed logical-content namespaces. Exit policy zero deliberately means no
# approved loader path; a known original package never supplies that evidence.
OEM_POOL_SOURCE_RULES = {
    SOURCE_SHA: (1, 10012, 1, "nx30-r012-archived-whole-pin-v1"),
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44":
        (2, 21301, 0, "rc3000-r013l01-archived-whole-pin-v1"),
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b":
        (3, 30015, 0, "rt3000-r015-archived-whole-pin-v1"),
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5":
        (4, 40012, 0, "rw3000-r012-archived-whole-pin-v1"),
}

# One finite post-attach table observed on the HW-D R012 physical B baseline.
# It is a second exact content rule, not a runtime autoresize formula.
OEM_R012_SOURCE_TABLE_SHA256 = "02475743e130eb615a0cf5a1fc93cc51613ddf201fc96c6ef043687b0b984ed9"
OEM_R012_HW_D_POST_TABLE_SHA256 = "1a581827d678e71ddd313f5824ed493587ca15374e88a3f2ddb62b73edc094f9"
OEM_R012_HW_D_POST_ALIAS = 10013
OEM_R012_HW_D_POST_GROUP = 10013

SBL_SHA = "c8bc5e1d35a3092dc1292d1b8ba13061082e08992a2cb07c05dbf7561a1244b5"
SBL_VALID_BYTES = 178176
APPSBL_BYTES = 0x140000
PUBLIC_ABCD_ARCHIVE_SHA256 = "bcca16ce8060b6f09094c10d0af40f9e48343d62da8e08ffe6cadbca8725c732"
PUBLIC_BOARD_INPUTS = {
    "A": dict(route="config@mp03.1", machid=0x08040001, product_name="RC3000",
        record="hw-a-public-8ed8-controlled-trial-v1", model="W25N01GWZEIG",
        manufacturer=0xef, chip=0xba, raw_spare=64, mtd_oob=64, ecc=4,
        nand_profile_id="h3c-oem-efba-128m-2k-sp64"),
    "B": dict(route="config@mp02.1", machid=0x08040000, product_name="RT3000",
        record="hw-b-public-9121-controlled-trial-v1", model="GD5F1GQ5REYIG",
        manufacturer=0xc8, chip=0x41, raw_spare=128, mtd_oob=128, ecc=8,
        nand_profile_id="h3c-oem-c841-128m-2k-sp128"),
    "C": dict(route="config@mp03.1-c2", machid=0x08040101, product_name="RT3000",
        record="hw-c-public-57d1-controlled-trial-v1", model="W25N01GW",
        manufacturer=0xef, chip=0xba, raw_spare=64, mtd_oob=64, ecc=4,
        nand_profile_id="h3c-oem-efba-128m-2k-sp64"),
    "D": dict(route="config@mp02.1", machid=0x08040000, product_name="NX30",
        record="hw-d-r012-public-91e9-low-ecc4-trial-v1", model="W25N01GWZEIG",
        manufacturer=0xef, chip=0xba, raw_spare=64, mtd_oob=64, ecc=4,
        nand_profile_id="h3c-oem-efba-128m-2k-sp64"),
}
# Reviewed complete OEM R012 APPSBL including exact FF padding. A report cannot
# extend OEM eligibility by calling an arbitrary fingerprint a baseline.
REVIEWED_OEM_APPSBL = frozenset({
    "7f1f7bf0938aae5d6c5816d6816524d5877eefb41e9fbdf4427b24bf34fb2b48",
})


def validate_release_identity(payload, report):
    if not isinstance(report, dict):
        raise ValueError("payload report must be an object")
    identity = report.get("release_identity")
    fields = {"scope", "sequence", "candidate_sha256", "candidate_bytes"}
    if not isinstance(identity, dict) or set(identity) != fields:
        raise ValueError("release_identity requires exactly scope/sequence/candidate_sha256/candidate_bytes")
    def scope(value):
        return isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_.:-]{1,127}", value)
    def integer(value, minimum):
        return type(value) is int and minimum <= value <= (1 << 64) - 1
    def digest(value):
        return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) and value != "0" * 64
    if (not scope(identity["scope"]) or not integer(identity["sequence"], 1) or
            not digest(identity["candidate_sha256"]) or
            type(identity["candidate_bytes"]) is not int or
            identity["candidate_bytes"] != APPSBL_BYTES or len(payload) != APPSBL_BYTES or
            identity["candidate_sha256"] != sha(payload)):
        raise ValueError("release_identity must bind exact candidate bytes/digest and explicit scope/sequence")
    known = report.get("known_releases")
    if not isinstance(known, list) or not 1 <= len(known) <= 127:
        raise ValueError("known_releases must be a reviewed nonempty list of at most 127 records")
    records = []
    hashes = {identity["candidate_sha256"]}
    sequences = {identity["sequence"]}
    have_oem = False
    for item in known:
        if not isinstance(item, dict) or set(item) != {"scope", "sequence", "sha256", "bytes", "kind"}:
            raise ValueError("known_releases record has missing or unknown fields")
        if (not scope(item["scope"]) or item["scope"] != identity["scope"] or
                not digest(item["sha256"]) or type(item["bytes"]) is not int or
                item["bytes"] != APPSBL_BYTES or not integer(item["sequence"], 0) or
                item["kind"] not in ("oem_baseline", "custom_release")):
            raise ValueError("known_releases record has invalid scope/size/hash/kind/sequence")
        if item["kind"] == "oem_baseline":
            if item["sequence"] != 0 or item["sha256"] not in REVIEWED_OEM_APPSBL:
                raise ValueError("OEM baseline must match fixed reviewed OEM APPSBL and sequence zero")
            have_oem = True
        elif item["sequence"] == 0 or item["sequence"] in sequences:
            raise ValueError("custom release sequence is zero, duplicate or conflicts with candidate")
        else:
            sequences.add(item["sequence"])
        if item["sha256"] in hashes:
            raise ValueError("release registry contains duplicate/conflicting SHA-256")
        hashes.add(item["sha256"])
        records.append(dict(item))
    if not have_oem:
        raise ValueError("known_releases must include the fixed reviewed OEM baseline")
    records.append({"scope": identity["scope"], "sequence": identity["sequence"],
                    "sha256": identity["candidate_sha256"], "bytes": APPSBL_BYTES,
                    "kind": "custom_release"})
    return records


def sha(data):
    return hashlib.sha256(data).hexdigest()


def identify_official(official):
    """Bind product/length/header/whole SHA before any output or transformation."""
    if not isinstance(official, bytes):
        raise ValueError("official source must be immutable bytes")
    identity = OFFICIAL_SOURCES.get(sha(official))
    if identity is None or len(official) != identity["bytes"]:
        raise ValueError("unknown or unadapted official package; RX aliases are not supported")
    if official[:6] != b"H3C_+k" or official[99:135].split(b"\0", 1)[0] != identity["identity"].encode("ascii"):
        raise ValueError("official product/version header mismatch")
    if (struct.unpack_from(">I", official, 64)[0] != 96 or
            struct.unpack_from(">I", official, 68)[0] != len(official) - 64 or
            struct.unpack_from(">I", official, 80)[0] != len(official) - 160 or
            struct.unpack_from(">I", official, 84)[0] != 0 or official[135] != 1 or
            sum(value[0] for value in struct.iter_unpack(">I", official[64:])) & 0xffffffff != 0xffffffff):
        raise ValueError("official wrapper length/checksum/version gate mismatch")
    total = struct.unpack_from(">I", official, 164)[0]
    if struct.unpack_from(">I", official, 160)[0] != 0xd00dfeed or total < 40 or total > len(official) - 160:
        raise ValueError("official outer FIT bounds invalid")
    return dict(identity)


def official_bootchain_components(official, identity):
    """Extract only the exact SBL1/APPSBL nodes from the pinned outer FIT."""
    outer_bytes = struct.unpack_from(">I", official, 164)[0]
    nodes = verify_fit(official[160:160 + outer_bytes])
    found = {}
    for path, props in nodes.items():
        desc = props.get("description")
        data = props.get("data")
        if desc == b"sbl1_nand.mbn.padded\0":
            key = "sbl"
        elif desc == b"openwrt-ipq5018-u-boot.mbn.padded\0":
            key = "appsbl"
        else:
            continue
        if key in found or not isinstance(data, bytes) or not data:
            raise ValueError("official outer FIT has duplicate/invalid boot-chain component")
        found[key] = data
    if set(found) != {"sbl", "appsbl"}:
        raise ValueError("official outer FIT lacks exact SBL1/APPSBL components")
    sbl, appsbl = found["sbl"], found["appsbl"]
    if len(sbl) != SBL_VALID_BYTES or len(appsbl) > APPSBL_BYTES:
        raise ValueError("official SBL/APPSBL component length is outside the reviewed layout")
    appsbl_pad = appsbl + b"\xff" * (APPSBL_BYTES - len(appsbl))
    sbl_pad = sbl + b"\xff" * (0x80000 - len(sbl))
    return {"sbl_sha256": sha(sbl), "sbl_valid_bytes": len(sbl),
            "sbl_extent_sha256": sha(sbl_pad), "appsbl_component_sha256": sha(appsbl),
            "appsbl_padded_sha256": sha(appsbl_pad), "appsbl_valid_bytes": len(appsbl)}


def generated_first_install_profile_header(board_class, official_identity, components, nand_profile=None,
                                           candidate_id=None):
    b = PUBLIC_BOARD_INPUTS[board_class]
    if nand_profile is not None:
        required = {"profile_id", "model_aliases", "jedec_bytes_observed", "manufacturer_id", "chip_id",
                    "capacity_mib", "page_bytes", "erase_bytes", "raw_spare_bytes", "mtd_oob_bytes",
                    "ecc_bits", "catalogue_eligible", "accepted_mtd_oob_mask"}
        if not isinstance(nand_profile, dict) or set(nand_profile) != required:
            raise ValueError("bound NAND profile tuple schema mismatch")
        if not (nand_profile["catalogue_eligible"] & {"A":1,"B":2,"C":4,"D":8}[board_class]):
            raise ValueError("bound NAND profile is not enabled for this compiled board class")
        if nand_profile["jedec_bytes_observed"] != 2:
            raise ValueError("bound NAND profile requires the exact two-byte OEM ID prefix")
        b = dict(b, model=nand_profile["model_aliases"][0], manufacturer=nand_profile["manufacturer_id"],
                 chip=nand_profile["chip_id"], raw_spare=nand_profile["raw_spare_bytes"],
                 mtd_oob=nand_profile["mtd_oob_bytes"], ecc=nand_profile["ecc_bits"],
                 capacity=nand_profile["capacity_mib"], nand_profile_id=nand_profile["profile_id"],
                 candidate_id=candidate_id)
    product = official_identity.get("model")
    if product is None:
        raise ValueError("unrecognized exact official product identity")
    route = b["route"]
    route_data = kernel_transform.ROUTES.get(route)
    if route_data is None:
        raise ValueError("selected hardware route is not an exact reviewed kernel config")
    compatible = route_data[1]
    vals = ["#ifndef BOOTBRIDGE_FIRST_INSTALL_PROFILE_H", "#define BOOTBRIDGE_FIRST_INSTALL_PROFILE_H",
        '#define BI_HARDWARE_CLASS "' + board_class + '"',
        f"#define BI_EXPECTED_MACHID 0x{b['machid']:08x}u",
        '#define BI_EXPECTED_PRODUCT "' + product + '"',
        '#define BI_EXPECTED_OEM_APPSBL_SHA256 "' + components["appsbl_padded_sha256"] + '"',
        '#define BI_FIRST_INSTALL_RECORD "' + (candidate_id or b["record"]) + '"',
        '#define BI_FIRST_INSTALL_SOURCE_SHA256 "' + official_identity["sha256"] + '"',
        '#define BI_CANDIDATE_ID "' + (candidate_id or "baseline-frozen") + '"',
        '#define BI_FIRST_INSTALL_ROUTE "' + route + '"',
        '#define BI_FIRST_INSTALL_COMPATIBLE "' + compatible + '"',
        '#define BI_BOUND_NAND_PROFILE_ID "' + b["nand_profile_id"] + '"',
        f"#define BI_BOUND_NAND_MANUFACTURER_ID {b['manufacturer']}u",
        f"#define BI_BOUND_NAND_CHIP_ID {b['chip']}u",
        f"#define BI_BOUND_NAND_CAPACITY_BYTES {b.get('capacity', 128) * 1048576}u",
        f"#define BI_BOUND_NAND_RAW_SPARE_BYTES {b['raw_spare']}u",
        "#define BI_BOUND_NAND_PAGE_BYTES 2048u",
        "#define BI_BOUND_NAND_ERASE_BYTES 131072u",
        f"#define BI_BOUND_NAND_MTD_OOB_BYTES {b['mtd_oob']}u",
        f"#define BI_BOUND_NAND_ACCEPTED_MTD_OOB_MASK {b.get('accepted_mtd_oob_mask', 0)}u",
        f"#define BI_BOUND_NAND_ECC_BITS {b['ecc']}u",
        '#define BI_FIRST_INSTALL_PRODUCT BI_RETURN_SOURCE_PRODUCT',
        "#define BI_FIRST_INSTALL_OEM_EXIT_POLICY 2u",
        "#define BI_FIRST_INSTALL_PROFILE_COUNT 4u",
        '#include "bridge_first_install.h"',
    ]
    # This is a physical-layout template, not a candidate registry. The
    # production qualifier selects the exact payload through the compiled
    # board registry, then binds that single result to a local profile copy.
    # Keep the template fields NULL/0 so it cannot bypass that selection.
    candidate_binding = 'NULL, 0u'
    vals += ["static const bfi_install_profile bootbridge_first_install_profiles[BI_FIRST_INSTALL_PROFILE_COUNT] = {"]
    # Four exact tuples: APPSBL HIGH/LOW active, independently of root A/B.
    # All share the same source-bound OEM digest, SBL, NAND and candidate gates.
    for app in (1, 0):
        active, target = ((0x640000, 0x500000) if app else (0x500000, 0x640000))
        for root in (0, 1):
            bridge, oem = ((0x900000, 0x3100000) if root == 0 else (0x3100000, 0x900000))
            vals += [
                "  {",
                f'    "hw-{board_class.lower()}-public-profile-app{app}-root{root}-v2", "{b["model"]}", {b["manufacturer"]}u, {b["chip"]}u,',
                f'    {b.get("capacity", 128)}u, {b["raw_spare"]}u, {b["mtd_oob"]}u, {b["ecc"]}u, 2048u, 131072u,',
                f'    0u, 0x80000u, "{components["sbl_sha256"]}", {components["sbl_valid_bytes"]}u, 1u,',
                f'    "{components["appsbl_padded_sha256"]}", {app}u, {root}u, 1u,',
                f'    0x{active:x}u, 0x{target:x}u, 0x140000u, 0x900000u,',
                f'    0x{bridge:x}u, 0x2800000u, 0x{oem:x}u, {1-root}u, 0x2800000u,',
                f'    {b["ecc"]}u, {b["ecc"]}u, {candidate_binding}, {b.get("accepted_mtd_oob_mask", 0)}u',
                "  },",
            ]
    vals += ["};", "#endif", ""]
    return "\n".join(vals)


def candidate_build_profile(descriptor, board_class, product, nand_tuples):
    """Bind compile metadata to one exact board/source profile and OOB tuple set."""
    if board_class not in "ABCD" or len(board_class) != 1 or not isinstance(descriptor, dict):
        raise ValueError("candidate source profile identity is invalid")
    source_profile = descriptor.get("build_profile")
    if not isinstance(source_profile, dict) or source_profile.get("board") != board_class:
        raise ValueError("candidate archive lacks exact board build metadata")
    archived_policy = {"A": (None, 0, 0), "B": (None, 0, 0),
                       "C": (None, 1, 0), "D": ("NX30_2i5x", 1, 1)}[board_class]
    if (source_profile.get("env_alias"), source_profile.get("filter_env_root_keys"),
            source_profile.get("allow_missing_env_machid")) != archived_policy:
        raise ValueError("candidate archive ENV policy differs from reviewed source behavior")
    # The D source alias is NX30-specific: preserve it for NX30 and clear it
    # when the byte-identical source is packaged as RX3000.
    policy = (archived_policy[1], archived_policy[2],
              archived_policy[0] if board_class != "D" or product == "NX30" else None)
    result = dict(source_profile)
    if not isinstance(nand_tuples, list) or not nand_tuples:
        raise ValueError("candidate profile requires a nonempty exact runtime tuple list")
    oobs = sorted({row.get("mtd_oob_bytes") for row in nand_tuples})
    result.update({"mtd_oob_bytes": oobs,
        "filter_env_root_keys": policy[0], "allow_missing_env_machid": policy[1],
        "env_alias": policy[2]})
    return result


def generated_provider_bundle_header(source_sha256, product, records):
    """Render the exact compile-time dispatcher table after provider hashes exist."""
    def checked_text(value, label, pattern):
        if not isinstance(value, str) or not re.fullmatch(pattern, value):
            raise ValueError("invalid provider bundle " + label)
        return value
    checked_text(source_sha256, "source SHA", r"[0-9a-f]{64}")
    checked_text(product, "product", r"[A-Z0-9_]{1,32}")
    if not isinstance(records, list) or not 1 <= len(records) <= 160:
        raise ValueError("provider bundle requires one to 160 exact variant records")
    fields = ("record_id", "candidate_id", "hardware_class", "source_sha256", "product", "route",
        "route_compatible", "source_revision_sha256", "stock_kernel_sha256",
        "bridge_kernel_sha256", "nand_dt_sha256", "profile_set_id", "candidate_set_id",
        "nand_profiles", "provider_path", "provider_sha256", "provider_bytes",
        "payload_path", "payload_sha256", "payload_bytes")
    sha_fields = {"source_sha256", "source_revision_sha256", "stock_kernel_sha256",
                  "bridge_kernel_sha256", "nand_dt_sha256", "provider_sha256", "payload_sha256"}
    text_fields = {"record_id": r"[A-Za-z0-9_.:-]{1,127}",
        "candidate_id": r"[A-Za-z0-9_.:-]{1,255}",
        "hardware_class": r"[A-D]", "product": r"[A-Z0-9_]{1,32}",
        "route": r"config@[A-Za-z0-9._-]{1,64}",
        "route_compatible": r"[A-Za-z0-9,._+-]{1,127}",
        "profile_set_id": r"nandset-[a-z0-9-]{1,64}",
        "candidate_set_id": r"candset-[0-9a-f]{16}",
        "provider_path": r"/usr/sbin/bridge-candidates/[a-z0-9-]{1,40}/provider",
        "payload_path": r"(?:/etc/bridge/providers/[a-z0-9-]{1,40}/uboot\.bin|/etc/bridge/provider-payload-bases/[0-9a-f]{64}\.bin)"}
    nums = set(fields) - sha_fields - set(text_fields) - {"nand_profiles"}
    seen_records, seen_provider_paths, seen_payload_paths = set(), set(), {}
    initializers, declarations, set_ids = [], [], set()
    profile_fields = ("profile_id", "model_aliases", "jedec_bytes_observed", "manufacturer_id",
        "chip_id", "capacity_mib", "page_bytes", "erase_bytes", "raw_spare_bytes",
        "mtd_oob_bytes", "ecc_bits", "catalogue_eligible")
    for index, row in enumerate(records):
        if not isinstance(row, dict) or set(row) != set(fields):
            raise ValueError("provider row fields differ from dispatcher ABI v2")
        if row["source_sha256"] != source_sha256 or row["product"] != product:
            raise ValueError("provider row source/product differs from bundle identity")
        if row["record_id"] in seen_records:
            raise ValueError("duplicate provider record identity")
        seen_records.add(row["record_id"])
        set_ids.add((row["profile_set_id"], row["candidate_set_id"]))
        for key in sha_fields:
            checked_text(row[key], key, r"[0-9a-f]{64}")
        for key, pattern in text_fields.items():
            checked_text(row[key], key, pattern)
        if row["provider_path"] in seen_provider_paths or row["provider_path"] in seen_payload_paths:
            raise ValueError("provider path reused or collides with a payload path")
        seen_provider_paths.add(row["provider_path"])
        payload_contract = (row["payload_sha256"], row["payload_bytes"])
        previous_payload = seen_payload_paths.get(row["payload_path"])
        if previous_payload is not None and previous_payload != payload_contract:
            raise ValueError("shared payload path has conflicting exact byte contracts")
        if row["payload_path"] in seen_provider_paths:
            raise ValueError("payload path collides with a provider path")
        seen_payload_paths[row["payload_path"]] = payload_contract
        for key in nums:
            value = row[key]
            if type(value) is not int or value < 0 or value > 0xffffffff:
                raise ValueError("invalid numeric provider ABI field: " + key)
        if (row["provider_bytes"] <= 0 or row["provider_bytes"] > 32 * 1024 * 1024 or
                row["payload_bytes"] != APPSBL_BYTES):
            raise ValueError("provider record has unsupported exact candidate sizes")
        profiles = row["nand_profiles"]
        if not isinstance(profiles, list) or not profiles:
            raise ValueError("provider row requires a nonempty NAND profile subset")
        alias_declarations, profile_initializers = [], []
        for pidx, profile in enumerate(profiles):
            if not isinstance(profile, dict) or set(profile) != set(profile_fields):
                raise ValueError("provider NAND profile tuple has unexpected fields")
            if (not isinstance(profile["profile_id"], str) or
                    not re.fullmatch(r"[A-Za-z0-9._-]{1,95}", profile["profile_id"]) or
                    not isinstance(profile["model_aliases"], list) or not profile["model_aliases"] or
                    type(profile["jedec_bytes_observed"]) is not int or profile["jedec_bytes_observed"] != 2 or
                    type(profile["catalogue_eligible"]) is not int or
                    not (profile["catalogue_eligible"] & {"A":1,"B":2,"C":4,"D":8}[row["hardware_class"]])):
                raise ValueError("provider NAND profile lacks exact identity/board eligibility")
            for key in profile_fields[2:11]:
                if type(profile[key]) is not int or profile[key] < 0 or profile[key] > 0xffffffff:
                    raise ValueError("provider NAND profile has invalid numeric field: " + key)
            if (profile["capacity_mib"] not in (128, 256, 512) or profile["page_bytes"] != 2048 or
                    profile["erase_bytes"] != 131072 or profile["raw_spare_bytes"] < profile["mtd_oob_bytes"] or
                    profile["ecc_bits"] not in (4, 8)):
                raise ValueError("provider NAND tuple has unsupported geometry")
            alias_name = f"bootbridge_aliases_{index}_{pidx}"
            profile_name = f"bootbridge_nand_profiles_{index}"
            alias_values = ", ".join('"' + checked_text(a, "model alias", r"[A-Za-z0-9._()+-]{1,63}") + '"'
                                      for a in profile["model_aliases"])
            alias_declarations.append(f"static const char *const {alias_name}[] = {{{alias_values}}};")
            profile_initializers.append("    { \"%s\", %s, %du, %du, %du, %du, %du, %du, %du, %du, %du, %du, %du }," % (
                profile["profile_id"], alias_name, len(profile["model_aliases"]),
                profile["jedec_bytes_observed"], profile["manufacturer_id"], profile["chip_id"],
                profile["capacity_mib"], profile["page_bytes"], profile["erase_bytes"],
                profile["raw_spare_bytes"], profile["mtd_oob_bytes"], profile["ecc_bits"],
                profile["catalogue_eligible"]))
        declarations.extend(alias_declarations)
        declarations.append(f"static const bdi_nand_profile bootbridge_nand_profiles_{index}[{len(profiles)}] = {{")
        declarations.extend(profile_initializers)
        declarations.append("};")
        nand_pointer = "bootbridge_nand_profiles_%d" % index
        values = []
        for key in fields:
            value = row[key]
            if key == "nand_profiles":
                values.extend((nand_pointer, str(len(profiles)) + "u"))
            elif key in sha_fields or key in text_fields:
                values.append('"' + value + '"')
            elif key in nums:
                values.append(str(value) + "u")
        initializers.append("    { " + ", ".join(values) + " }")
    if len(set_ids) != 1:
        raise ValueError("all provider rows must share exact profile and candidate set IDs")
    return "\n".join([
        "/* Generated only after hashing each compiled provider binary. */",
        "#ifndef BOOTBRIDGE_PROVIDER_BUNDLE_H", "#define BOOTBRIDGE_PROVIDER_BUNDLE_H",
        '#include "bridge_dispatch.h"',
        "#define BI_PROVIDER_BUNDLE_ABI_VERSION 2u",
        "#define BI_PROVIDER_COUNT %du" % len(records),
        '#define BI_PROVIDER_SOURCE_SHA256 "%s"' % source_sha256,
        '#define BI_PROVIDER_PRODUCT "%s"' % product,
        *declarations,
        "static const bdi_provider_record bootbridge_provider_records[BI_PROVIDER_COUNT] = {",
        ",\n".join(initializers), "};", "#endif", ""])


def payload_report_json(data):
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate payload report field: " + key)
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique_object)


def _catalog_format_checks(header_path, supported, load, storage, hostcc):
    ids = supported + load + storage
    if any(not re.fullmatch(r"[A-Za-z0-9._-]{1,95}", profile_id) for profile_id in ids):
        raise ValueError("payload report has malformed NAND profile IDs")
    probe_source = '''#include <stdio.h>
#include <string.h>
#include "h3c_nand_catalog.h"
int main(int argc, char **argv) {
  if (argc < 3) return 2;
  for (int i = 2; i < argc; ++i) {
    const struct h3c_nand_catalog_profile *p = h3c_nand_catalog_by_profile_id(argv[i]);
    int ok = p && (!strcmp(argv[1], "runtime") ? (h3c_nand_catalog_runtime_read_qualified(p, 4) ||
                                                   h3c_nand_catalog_runtime_read_qualified(p, 8))
                                              : ((p->capacity_bytes == 0x08000000U ||
                                                  p->capacity_bytes == 0x10000000U) &&
                                                 p->page_bytes == 2048U && p->erase_bytes == 131072U));
    if (!ok) { fprintf(stderr, "unsupported candidate format profile: %s\\n", argv[i]); return 1; }
  }
  return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="nx30-catalog-qual-") as td:
        temp = Path(td)
        include = temp / "include"
        include.mkdir()
        shutil.copyfile(header_path, include / "h3c_nand_catalog.h")
        source = temp / "catalog_qualification.c"
        exe = temp / "catalog_qualification"
        source.write_text(probe_source)
        subprocess.run([hostcc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(include),
                        str(source), "-o", str(exe)], check=True, capture_output=True, text=True, timeout=30)
        subprocess.run([str(exe), "runtime", *supported], check=True, capture_output=True, text=True, timeout=10)
        subprocess.run([str(exe), "stage", *load], check=True, capture_output=True, text=True, timeout=10)
        subprocess.run([str(exe), "stage", *storage], check=True, capture_output=True, text=True, timeout=10)


D_FIRST_INSTALL_RECORD = "hw-d-r012-5f486f5f-low-ecc4-v1"
D_FIRST_INSTALL_SHA = "61b10ffdec0af3101657bccd5f4425ca4e8a8e6dc62dd2e8d5f8d08e73bc3595"
D_FIRST_INSTALL_COMMIT = "5f486f5f9852c26d7d16eb5980a06cccea2a5217"
D_FIRST_INSTALL_SOURCE_SHA = "b1144b5a8b20ca543acd7278083b6e7246e21c5b66a8f7d8e6fa1645c7acd0c5"
D_PUBLIC_FIRST_INSTALL_RECORD = "hw-d-r012-public-91e9-low-ecc4-trial-v1"
D_PUBLIC_FIRST_INSTALL_SHA = "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362"
D_HW_R012_POST_ATTACH_CANDIDATE_SHA256S = frozenset((
    D_FIRST_INSTALL_SHA,
    D_PUBLIC_FIRST_INSTALL_SHA,
))
D_PUBLIC_FIRST_INSTALL_MBN_SHA = "ca3e059c9de2209c793948fbb5f155cc8b5cb2011b5c1ca9a903321157dce713"
D_PUBLIC_FIRST_INSTALL_LOAD_SHA = "e524e0f1429323ee3d612284e5ec0d66059996999e0a03dc891334ec66225572"
D_PUBLIC_SOURCE_SHA = "a4557d97bc569fb6b75d389a09bc5235c7948951cb98feff92652e72e82dba55"
D_PUBLIC_SOURCE_ARCHIVE_SHA = "bcca16ce8060b6f09094c10d0af40f9e48343d62da8e08ffe6cadbca8725c732"
D_PUBLIC_SOURCE_PROVENANCE_SHA = "5281a9894be5ba36018c194321d3cc505360c1d9436b36c0a5d22c7224dd5542"
D_PUBLIC_CANONICAL_SHA = D_FIRST_INSTALL_SHA
D_PUBLIC_OEM_SHA = "7f1f7bf0938aae5d6c5816d6816524d5877eefb41e9fbdf4427b24bf34fb2b48"
D_FIRST_INSTALL_SCOPE = "hw-d-r012-efba-sp64-ecc4-first-install"
PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256 = "bcca16ce8060b6f09094c10d0af40f9e48343d62da8e08ffe6cadbca8725c732"


def public_candidate_from_archive(archive_path, hardware_class):
    """Select one exact public neutral-path payload from the pinned ABCD archive.

    The existing candidate_manifest parser validates the full archive and all
    board descriptors before the selected partition is returned. The board is
    an explicit build input; neither OEM product identity nor FIT default is
    used to choose it.
    """
    if hardware_class not in ("A", "B", "C", "D"):
        raise ValueError("hardware class must be explicitly A, B, C or D")
    import candidate_manifest
    try:
        parsed = candidate_manifest.read_public_candidates(
            Path(archive_path), PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256)
        descriptor = parsed["boards"][hardware_class]
        member_name = "payloads/HW-%s/appsbl.partition.bin" % hardware_class
        archived_path = candidate_manifest.ROOT + "/" + member_name
        archive_bytes = regular(archive_path)
        if sha(archive_bytes) != PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256:
            raise ValueError("public candidate archive changed after descriptor validation")
        with tarfile.open(fileobj=io.BytesIO(archive_bytes), mode="r:gz") as archive:
            member = archive.getmember(archived_path)
            if not member.isfile():
                raise ValueError("selected public candidate is not a regular archive member")
            stream = archive.extractfile(member)
            if stream is None:
                raise ValueError("cannot read selected public candidate")
            payload = stream.read()
        expected = descriptor["files"]["appsbl.partition.bin"]
        if len(payload) != expected["bytes"] or sha(payload) != expected["sha256"]:
            raise ValueError("selected public candidate bytes differ from parsed descriptor")
        return payload, descriptor
    except (candidate_manifest.CandidateError, OSError, KeyError, tarfile.TarError) as exc:
        raise ValueError("public ABCD candidate selection failed: " + str(exc)) from exc


def fresh_candidate_from_archive(archive_path, descriptor):
    """Extract only a candidate descriptor already checked under three fresh pins."""
    import candidate_manifest
    try:
        payload = candidate_manifest.extract_candidate_partition_v2(archive_path, descriptor)
    except (candidate_manifest.CandidateError, OSError) as exc:
        raise ValueError("fresh candidate member read failed: " + str(exc)) from exc
    expected = descriptor["files"]["appsbl.partition.bin"]
    if len(payload) != expected["bytes"] or sha(payload) != expected["sha256"]:
        raise ValueError("fresh candidate descriptor no longer binds the exact partition bytes")
    return payload


def fresh_payload_patch(archive_path, descriptor, candidate_bytes):
    """Bind one same-board EF:BA 4/4 archive base to the exact full candidate."""
    try:
        parsed = candidate_manifest.read_candidate_archive_v2(
            archive_path, descriptor["archive_sha256"], descriptor["manifest_sha256"],
            descriptor["candidates_sha256"])
        eligible = [row for row in parsed["candidates"]
            if row["board"] == descriptor["board"] and
               row["nand_profile_id"] == "h3c-oem-efba-128m-2k-sp64" and
               row["load_ecc_bits"] == 4 and row["storage_ecc_bits"] == 4]
        if len(eligible) != 1:
            raise ValueError("pinned archive must contain exactly one per-board EF:BA 4/4 base")
        base_descriptor = eligible[0]
        base_bytes = candidate_manifest.extract_candidate_partition_v2(archive_path, base_descriptor)
        header, metadata = payload_patch_codegen.generate_patch_header(
            base_descriptor, base_bytes, descriptor, candidate_bytes)
    except (candidate_manifest.CandidateError, payload_patch_codegen.PayloadPatchError, KeyError) as exc:
        raise ValueError("cannot derive exact fresh candidate base/patch: " + str(exc)) from exc
    storage_path = "/etc/bridge/provider-payload-bases/" + metadata["base_sha256"] + ".bin"
    storage = {"encoding": "base-patch-v1", "path": storage_path,
        "sha256": metadata["base_sha256"], "bytes": metadata["base_bytes"],
        "patch_header_sha256": sha(header.encode("ascii")),
        "patch_count": metadata["patch_count"], "data_bytes": metadata["patch_bytes"]}
    return base_bytes, header, metadata, storage


def finite_d_loader_record(payload, report):
    """Bind the reviewed empirical record; a report's PASS flags are ignored.

    This is build eligibility only. The Linux collector must match its live
    facts to the finite rule before an install capability can be offered.
    Source fields are reviewed manifest metadata, not a checkout measurement.
    The exact computed payload digest binds the already-reviewed build bytes.
    """
    profile = ["h3c-oem-efba-128m-2k-sp64"]
    return (len(payload) == APPSBL_BYTES and sha(payload) == D_FIRST_INSTALL_SHA and
            report.get("derivative_commit") == D_FIRST_INSTALL_COMMIT and
            report.get("source_sha256") == D_FIRST_INSTALL_SOURCE_SHA and
            report.get("build_profile") == "release" and
            report.get("appsbl_load_profiles") == profile and
            report.get("appsbl_storage_profiles") == profile and
            report.get("supported_nand_profile_ids") == profile)


def public_d_loader_record(payload, report):
    """Recognize only the independently recorded public D trial candidate.

    The source digest here is the public source SHA256SUMS file, not the
    canonical checkout digest. Hardware evidence is cited separately; this
    record only binds the exact offline payload and reviewed source identity.
    """
    profile = ["h3c-oem-efba-128m-2k-sp64"]
    identity = report.get("release_identity")
    if not isinstance(identity, dict):
        return False
    expected_identity = {
        "scope": D_FIRST_INSTALL_SCOPE,
        "sequence": 2,
        "candidate_sha256": D_PUBLIC_FIRST_INSTALL_SHA,
        "candidate_bytes": APPSBL_BYTES,
    }
    known = report.get("known_releases")
    if not isinstance(known, list) or len(known) != 2:
        return False
    expected_known = [
        {"scope": D_FIRST_INSTALL_SCOPE, "sequence": 0,
         "sha256": D_PUBLIC_OEM_SHA, "bytes": APPSBL_BYTES, "kind": "oem_baseline"},
        {"scope": D_FIRST_INSTALL_SCOPE, "sequence": 1,
         "sha256": D_PUBLIC_CANONICAL_SHA, "bytes": APPSBL_BYTES, "kind": "custom_release"},
    ]
    return (
        len(payload) == APPSBL_BYTES and sha(payload) == D_PUBLIC_FIRST_INSTALL_SHA and
        report.get("partition_sha256") == D_PUBLIC_FIRST_INSTALL_SHA and
        report.get("mbn_sha256") == D_PUBLIC_FIRST_INSTALL_MBN_SHA and
        report.get("load_sha256") == D_PUBLIC_FIRST_INSTALL_LOAD_SHA and
        report.get("base_commit") == D_FIRST_INSTALL_COMMIT and
        report.get("derivative_commit") == D_FIRST_INSTALL_COMMIT and
        report.get("source_digest_kind") == "public-source-checksum-list-v1" and
        report.get("source_sha256") == D_PUBLIC_SOURCE_SHA and
        report.get("public_source_archive_sha256") == D_PUBLIC_SOURCE_ARCHIVE_SHA and
        report.get("public_source_provenance_sha256") == D_PUBLIC_SOURCE_PROVENANCE_SHA and
        report.get("build_profile") == "release" and
        report.get("appsbl_load_profiles") == profile and
        report.get("appsbl_storage_profiles") == profile and
        report.get("supported_nand_profile_ids") == profile and
        report.get("ecc_bits") == 4 and report.get("appsbl_load_ecc_bits") == 4 and
        report.get("appsbl_storage_ecc_bits") == 4 and
        report.get("sbl_sha256") == SBL_SHA and report.get("sbl_valid_bytes") == SBL_VALID_BYTES and
        identity == expected_identity and known == expected_known
    )


def resolve_d_loader_record(payload, report):
    """Return the exact finite record name for these bytes, else UNAVAILABLE."""
    if finite_d_loader_record(payload, report):
        return D_FIRST_INSTALL_RECORD
    if public_d_loader_record(payload, report):
        return D_PUBLIC_FIRST_INSTALL_RECORD
    return "UNAVAILABLE"


def validate_payload_report(payload, report, hostcc="cc"):
    if not isinstance(report, dict):
        raise ValueError("payload report must be an object")
    validate_release_identity(payload, report)
    if report.get("partition_sha256") != sha(payload):
        raise ValueError("payload report does not bind the exact embedded APPSBL bytes")
    required_strings = ["payload_version", "base_commit", "derivative_commit", "nand_catalog_header_path", "nand_catalog_sha256"]
    if any(not isinstance(report.get(key), str) or not report[key] for key in required_strings):
        raise ValueError("payload report lacks version or shared catalog identity")
    if report.get("sbl_sha256") != SBL_SHA or report.get("sbl_valid_bytes") != SBL_VALID_BYTES:
        raise ValueError("payload report SBL1 is not the audited R012 frontstage")
    if any(not re.fullmatch(r"[0-9a-f]{40}", report[key]) for key in ["base_commit", "derivative_commit"]):
        raise ValueError("payload report base/derivative commit is not a full source commit")
    if not re.fullmatch(r"[A-Za-z0-9._+-]{1,128}", report["payload_version"]):
        raise ValueError("payload report payload_version is malformed")
    if report.get("ecc_bits") != 4 or report.get("appsbl_load_ecc_bits") != 4 or report.get("appsbl_storage_ecc_bits") != 4:
        raise ValueError("bridge installation package supports only explicitly verified ECC4 stages")
    if (report.get("ip") != "192.168.10.1" or report.get("candidate_service_ram_role") != 0 or
            report.get("result") != "PASS_OFFLINE_MBN_ROLE0" or
            report.get("target", {}).get("size") != "0x140000" or
            report.get("network_changes_persistent_env") is not False or
            report.get("hardware_boot_or_write") != "NOT_EXECUTED"):
        raise ValueError("payload report address/role/target or offline execution boundary mismatch")
    catalog_path = Path(report["nand_catalog_header_path"])
    if not catalog_path.is_absolute() or not catalog_path.is_file():
        raise ValueError("shared catalog header path must identify the exact source file")
    catalog_bytes = regular(catalog_path)
    if sha(catalog_bytes) != report["nand_catalog_sha256"]:
        raise ValueError("shared catalog header digest mismatch")
    def profile_ids(field):
        values = report.get(field)
        if not isinstance(values, list) or not values or any(not isinstance(v, str) or not v for v in values):
            raise ValueError(f"payload report {field} must be a nonempty profile ID list")
        if len(set(values)) != len(values):
            raise ValueError(f"payload report {field} has duplicate profile IDs")
        return values

    supported = profile_ids("supported_nand_profile_ids")
    load = profile_ids("appsbl_load_profiles")
    storage = profile_ids("appsbl_storage_profiles")
    if not set(load).issubset(supported) or not set(storage).issubset(supported):
        raise ValueError("APPSBL stage qualifications must be subsets of runtime-supported profiles")
    _catalog_format_checks(catalog_path, supported, load, storage, hostcc)
    record_name = resolve_d_loader_record(payload, report)
    return {"catalog_path": catalog_path, "catalog_bytes": catalog_bytes,
            "supported": supported, "load": load, "storage": storage,
            "catalog_sha256": sha(catalog_bytes),
            "boot_chain_qualification": record_name,
            "install_qualification_available": record_name != "UNAVAILABLE"}


def generated_payload_header(template, payload, report, qualification):
    records = validate_release_identity(payload, report)
    text = template.read_text()
    replacements = {
        r'(#define BI_PAYLOAD_BYTES )\d+U': lambda m: m.group(1) + str(len(payload)) + "U",
        r'(#define BI_PAYLOAD_SHA256 )"[^"]*"': lambda m: m.group(1) + '"' + sha(payload) + '"',
        r'(#define BI_PAYLOAD_VERSION )"[^"]*"': lambda m: m.group(1) + '"' + report["payload_version"] + '"',
        r'(#define BI_SBL_VALID_BYTES )\d+U': lambda m: m.group(1) + str(report["sbl_valid_bytes"]) + "U",
        r'(#define BI_SBL_SHA256 )"[^"]*"': lambda m: m.group(1) + '"' + report["sbl_sha256"] + '"',
    }
    for pattern, replacement in replacements.items():
        text, count = re.subn(pattern, replacement, text)
        if count != 1:
            raise ValueError("static payload header template is missing an expected binding")
    registry = [f'#define BI_VERSION_SCOPE "{report["release_identity"]["scope"]}"',
                'static const bv_record BI_VERSION_RELEASES[] = {']
    for record in records:
        kind = "BV_OEM_BASELINE" if record["kind"] == "oem_baseline" else "BV_CUSTOM_RELEASE"
        registry.append('    {"%s", "%s", UINT64_C(%d), UINT64_C(%d), %s},' %
                        (record["sha256"], record["scope"], record["bytes"], record["sequence"], kind))
    registry += ['};', '#define BI_VERSION_REGISTRY BI_VERSION_RELEASES',
                 '#define BI_VERSION_REGISTRY_COUNT (sizeof(BI_VERSION_RELEASES)/sizeof(BI_VERSION_RELEASES[0]))']
    text, count = re.subn(r'/\* BEGIN TRUSTED VERSION REGISTRY \*/.*?/\* END TRUSTED VERSION REGISTRY \*/',
                         '/* BEGIN TRUSTED VERSION REGISTRY */\n' + '\n'.join(registry) +
                         '\n/* END TRUSTED VERSION REGISTRY */', text, flags=re.S)
    if count != 1:
        raise ValueError("payload header missing trusted version registry marker")
    lines = [f'#define BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE {int(qualification["install_qualification_available"])}',
             f'#define BI_FIRST_INSTALL_RECORD "{qualification["boot_chain_qualification"] if qualification["install_qualification_available"] else ""}"',
             f'#define BI_NAND_CATALOG_SHA256 "{qualification["catalog_sha256"]}"',
             f'#define BI_SUPPORTED_NAND_PROFILE_IDS "{";".join(qualification["supported"])}"',
             f'#define BI_APPSBL_LOAD_PROFILE_IDS "{";".join(qualification["load"])}"',
             f'#define BI_APPSBL_STORAGE_PROFILE_IDS "{";".join(qualification["storage"])}"',
             f'#define BI_APPSBL_LOAD_ECC_BITS {report["appsbl_load_ecc_bits"]}',
             f'#define BI_APPSBL_STORAGE_ECC_BITS {report["appsbl_storage_ecc_bits"]}']
    return text.replace("#endif", "\n".join(lines) + "\n#endif")


def public_catalog_binding(report, profile, hostcc):
    """Validate the shared NAND catalog as code input without D payload claims."""
    if not isinstance(report, dict):
        raise ValueError("public candidate still requires a pinned NAND catalog report")
    path = Path(report.get("nand_catalog_header_path", ""))
    if not path.is_absolute() or not path.is_file():
        raise ValueError("public candidate catalog header path is missing")
    raw = regular(path)
    if sha(raw) != report.get("nand_catalog_sha256"):
        raise ValueError("public candidate catalog header digest mismatch")
    supported = load = storage = [profile["nand_profile_id"]]
    _catalog_format_checks(path, supported, load, storage, hostcc)
    return {"catalog_path": path, "catalog_bytes": raw, "catalog_sha256": sha(raw),
            "supported": supported, "load": load, "storage": storage}


def generated_public_payload_header(template, payload, descriptor, catalog, components, profile,
                                   payload_path="/etc/bridge/uboot.bin"):
    board_class = profile.get("class")
    if board_class not in PUBLIC_BOARD_INPUTS:
        raise ValueError("public payload header requires a canonical board class")
    profile_candidate_id = profile.get("candidate_id")
    if "candidate_id" in profile:
        if (not isinstance(profile_candidate_id, str) or not profile_candidate_id or
                profile_candidate_id != descriptor.get("candidate_id")):
            raise ValueError("profile candidate ID must exactly match the payload descriptor")
        first_install_record = profile_candidate_id
    else:
        first_install_record = PUBLIC_BOARD_INPUTS[board_class]["record"]
    text = template.read_text()
    replacements = {
        r'(#define BI_PAYLOAD_PATH )"[^"]*"': lambda m: m.group(1) + '"' + payload_path + '"',
        r'(#define BI_PAYLOAD_BYTES )\d+U': lambda m: m.group(1) + str(len(payload)) + "U",
        r'(#define BI_PAYLOAD_SHA256 )"[^"]*"': lambda m: m.group(1) + '"' + sha(payload) + '"',
        r'(#define BI_PAYLOAD_VERSION )"[^"]*"': lambda m: m.group(1) + '"' + descriptor["candidate_id"] + '"',
        r'(#define BI_SBL_VALID_BYTES )\d+U': lambda m: m.group(1) + str(components["sbl_valid_bytes"]) + "U",
        r'(#define BI_SBL_SHA256 )"[^"]*"': lambda m: m.group(1) + '"' + components["sbl_sha256"] + '"',
    }
    for pattern, replacement in replacements.items():
        text, count = re.subn(pattern, replacement, text)
        if count != 1:
            raise ValueError("payload header lacks a required public-candidate binding")
    scope = "bb-hw-" + profile["class"].lower() + "-" + profile["source_sha256"][:12] + "-trial-v1"
    registry = [f'#define BI_VERSION_SCOPE "{scope}"',
        'static const bv_record BI_VERSION_RELEASES[] = {',
        f'    {{"{components["appsbl_padded_sha256"]}", "{scope}", UINT64_C({APPSBL_BYTES}), UINT64_C(0), BV_OEM_BASELINE}},',
        f'    {{"{sha(payload)}", "{scope}", UINT64_C({APPSBL_BYTES}), UINT64_C(1), BV_CUSTOM_RELEASE}},',
        '};',
        '#define BI_VERSION_REGISTRY BI_VERSION_RELEASES',
        '#define BI_VERSION_REGISTRY_COUNT 2u']
    text, count = re.subn(r'/\* BEGIN TRUSTED VERSION REGISTRY \*/.*?/\* END TRUSTED VERSION REGISTRY \*/',
        '/* BEGIN TRUSTED VERSION REGISTRY */\n' + '\n'.join(registry) +
        '\n/* END TRUSTED VERSION REGISTRY */', text, flags=re.S)
    if count != 1:
        raise ValueError("payload header trusted registry marker missing")
    lines = [f'#define BI_BOOT_CHAIN_QUALIFICATION_AVAILABLE 1',
        f'#define BI_FIRST_INSTALL_RECORD "{first_install_record}"',
        f'#define BI_NAND_CATALOG_SHA256 "{catalog["catalog_sha256"]}"',
        f'#define BI_SUPPORTED_NAND_PROFILE_IDS "{";".join(catalog["supported"])}"',
        f'#define BI_APPSBL_LOAD_PROFILE_IDS "{";".join(catalog["load"])}"',
        f'#define BI_APPSBL_STORAGE_PROFILE_IDS "{";".join(catalog["storage"])}"',
        f'#define BI_APPSBL_LOAD_ECC_BITS {profile.get("load_ecc", profile["ecc"])}',
        f'#define BI_APPSBL_STORAGE_ECC_BITS {profile["ecc"]}']
    return text.replace("#endif", "\n".join(lines) + "\n#endif")


def public_runtime_payload_metadata(payload, descriptor, profile, official_identity, route_metadata, catalog):
    scope = "bb-hw-" + profile["class"].lower() + "-" + profile["source_sha256"][:12] + "-trial-v1"
    metadata = {"schema": "bootbridge-runtime-payload-v1", "payload_version": descriptor["candidate_id"],
        "partition_bytes": len(payload), "partition_sha256": sha(payload),
        "release_identity": {"scope": scope, "sequence": 1,
                             "candidate_sha256": sha(payload), "candidate_bytes": len(payload)},
        "catalog_sha256": catalog["catalog_sha256"], "requested_payload_ecc_bits": profile["ecc"],
        "hardware_class": profile["class"], "official_product": profile["product_name"],
        "official_sha256": official_identity["sha256"], "return_route": route_metadata["route"],
        "return_source_revision_sha256": route_metadata["revision_sha256"],
        "claim": "offline candidate only; hardware validation NOT_EXECUTED"}
    snapshot = descriptor.get("source_snapshot_sha256")
    if snapshot:
        metadata["source_snapshot_sha256"] = snapshot
    else:
        metadata["source_commits"] = {"base_commit": descriptor["source_revision"],
                                      "derivative_commit": descriptor["source_revision"]}
    return metadata


def runtime_payload_metadata(payload, report):
    """Public code-profile metadata only; never embed the build report verbatim.

    Catalog paths, host evidence paths, sample/SN/MAC and arbitrary diagnostic
    fields stay in private build evidence. These fields do not qualify hardware.
    """
    validate_release_identity(payload, report)
    for key in ("base_commit", "derivative_commit"):
        if not isinstance(report.get(key), str) or not re.fullmatch(r"[0-9a-f]{40}", report[key]):
            raise ValueError("runtime metadata requires exact source commit identities")
    version = report.get("payload_version")
    if not isinstance(version, str) or not re.fullmatch(r"[A-Za-z0-9._+-]{1,128}", version):
        raise ValueError("runtime metadata requires a bounded code version")
    catalog_sha = report.get("nand_catalog_sha256")
    if not isinstance(catalog_sha, str) or not re.fullmatch(r"[0-9a-f]{64}", catalog_sha) or catalog_sha == "0" * 64:
        raise ValueError("runtime metadata requires exact catalog content identity")
    if report.get("ip") != "192.168.10.1" or any(type(report.get(k)) is not int or report[k] != 4 for k in
            ("ecc_bits", "appsbl_load_ecc_bits", "appsbl_storage_ecc_bits")):
        raise ValueError("unreviewed runtime address or requested payload format")
    return {"schema": "bootbridge-runtime-payload-v1", "payload_version": version,
            "partition_bytes": len(payload), "partition_sha256": sha(payload),
            "source_commits": {k: report[k] for k in ("base_commit", "derivative_commit")},
            "release_identity": dict(report["release_identity"]),
            "catalog_sha256": catalog_sha, "requested_payload_ecc_bits": 4,
            "maintenance_ip": "192.168.10.1", "hardware_qualification": "UNAVAILABLE",
            "install_available": False}


def regular(path):
    path = Path(path)
    if not stat.S_ISREG(path.stat().st_mode) or str(path.resolve()).startswith("/dev/"):
        raise ValueError("input must be an ordinary offline file")
    return path.read_bytes()


def command(args, log):
    environment = dict(os.environ)
    # mksquashfs rejects SOURCE_DATE_EPOCH combined with explicit timestamps.
    environment.pop("SOURCE_DATE_EPOCH", None)
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                            timeout=180, env=environment)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"command failed; see {log}")


def digest(data, algo):
    return struct.pack(">I", zlib.crc32(data) & 0xffffffff) if algo == "crc32" else hashlib.new(algo, data).digest()


def fit_nodes(raw):
    total = struct.unpack_from(">I", raw, 4)[0]
    return vendor_fit.fdt_props(raw[:total])


def verify_fit(raw):
    nodes = fit_nodes(raw)
    checked = []
    for path, props in nodes.items():
        if "algo" not in props:
            continue
        parent = path.rsplit("/", 1)[0]
        data = nodes[parent]["data"]
        algo = props["algo"].rstrip(b"\0").decode()
        if props["value"] != digest(data, algo):
            raise ValueError(f"FIT hash mismatch: {path}")
        checked.append(path)
    for path, props in nodes.items():
        if path.startswith("/images/") and path.count("/") == 2 and "data" in props:
            if not any(p.startswith(path + "/") for p in checked):
                raise ValueError(f"unhashed image: {path}")
    if not checked:
        raise ValueError("no FIT hashes")
    return nodes


class FDT:
    def __init__(self, raw, extra=65536):
        self.lib = ctypes.CDLL(ctypes.util.find_library("fdt") or "libfdt.so.1")
        for name, types in {
            "fdt_open_into": [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int],
            "fdt_path_offset": [ctypes.c_void_p, ctypes.c_char_p],
            "fdt_setprop": [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int],
            "fdt_del_node": [ctypes.c_void_p, ctypes.c_int],
            "fdt_pack": [ctypes.c_void_p],
        }.items():
            getattr(self.lib, name).argtypes = types
            getattr(self.lib, name).restype = ctypes.c_int
        self.buf = ctypes.create_string_buffer(len(raw) + extra)
        source = ctypes.create_string_buffer(raw)
        self.require(self.lib.fdt_open_into(source, self.buf, len(self.buf)))

    @staticmethod
    def require(rc):
        if rc < 0:
            raise ValueError(f"libfdt returned {rc}")

    def node(self, path):
        n = self.lib.fdt_path_offset(self.buf, path.encode())
        self.require(n)
        return n

    def set(self, path, key, value):
        data = ctypes.create_string_buffer(value)
        self.require(self.lib.fdt_setprop(self.buf, self.node(path), key.encode(), data, len(value)))

    def remove(self, path):
        self.require(self.lib.fdt_del_node(self.buf, self.node(path)))

    def bytes(self):
        self.require(self.lib.fdt_pack(self.buf))
        return self.buf.raw[:struct.unpack_from(">I", self.buf.raw, 4)[0]]


def bridge_kernel(stock, source_official_sha256):
    return kernel_transform.transform_kernel(stock, source_official_sha256)


def copy_stock_bound(original, output, stock_squashfs, rel):
    """Copy one curated file/link only after comparison to this source rootfs."""
    source, dest = original / rel, output / rel
    if not source.resolve(strict=True).is_relative_to(original.resolve()):
        raise ValueError("curated input escapes its official rootfs: " + rel)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        if not stat.S_ISLNK(source.lstat().st_mode):
            raise ValueError("curated source is not a symlink: " + rel)
        target = os.readlink(source)
        if (os.path.isabs(target) or not (dest.parent / target).resolve().is_relative_to(output.resolve())):
            raise ValueError("curated library link escapes staged rootfs: " + rel)
        listing = subprocess.run(["unsquashfs", "-lls", str(stock_squashfs), rel],
                                 capture_output=True, text=True, timeout=30, check=True).stdout
        entry = squashfs_listing_entry(listing, rel)
        if entry["type"] != "l" or entry["symlink_target"] != target:
            raise ValueError("curated library link differs from official rootfs: " + rel)
        dest.symlink_to(target)
        return {"symlink": target, "official_symlink_verified": True}
    if not stat.S_ISREG(source.lstat().st_mode):
        raise ValueError("curated input must be a regular file: " + rel)
    listing = subprocess.run(["unsquashfs", "-lls", str(stock_squashfs), rel],
                             capture_output=True, text=True, timeout=30, check=True).stdout
    entry = squashfs_listing_entry(listing, rel)
    if entry["type"] != "-":
        raise ValueError("official SquashFS inode is not a regular file: " + rel)
    data = regular(source)
    extracted = subprocess.run(["unsquashfs", "-cat", str(stock_squashfs), rel],
                               capture_output=True, timeout=30)
    if extracted.returncode or extracted.stdout != data:
        raise ValueError("rootfs input does not match the official volume: " + rel)
    dest.write_bytes(data)
    dest.chmod(entry["mode"])
    return {"bytes": len(data), "sha256": sha(data), "mode_octal": f"{entry['mode']:04o}",
            "official_regular_inode_verified": True}


def squashfs_listing_entry(listing, rel):
    """Parse the exact mode/type/link metadata emitted by unsquashfs -lls."""
    pattern = re.compile(
        r"^(?P<mode>[bcdlps-][rwxStTs-]{9})\s+\S+\s+\d+\s+"
        r"\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}\s+squashfs-root/" +
        re.escape(rel) + r"(?: -> (?P<link>.*))?$"
    )
    matches = [pattern.match(line) for line in listing.splitlines()]
    matches = [match for match in matches if match]
    if len(matches) != 1:
        raise ValueError("official SquashFS inode metadata missing or ambiguous: " + rel)
    match = matches[0]
    text = match.group("mode")
    permissions = text[1:]
    mode = 0
    for index, shift in enumerate((6, 3, 0)):
        triplet = permissions[index * 3:index * 3 + 3]
        if triplet[0] == "r": mode |= 4 << shift
        if triplet[1] == "w": mode |= 2 << shift
        if triplet[2] in "xst": mode |= 1 << shift
    if permissions[2] in "sS": mode |= stat.S_ISUID
    if permissions[5] in "sS": mode |= stat.S_ISGID
    if permissions[8] in "tT": mode |= stat.S_ISVTX
    return {"type": text[0], "mode": mode, "symlink_target": match.group("link")}


def resolve_rootfs_regular(root, rel):
    root = Path(root).resolve(strict=True)
    candidate = root / rel
    resolved = candidate.resolve(strict=True)
    if not resolved.is_relative_to(root) or not stat.S_ISREG(resolved.stat().st_mode):
        raise ValueError("ELF dependency is not a regular in-root file: " + str(rel))
    return resolved


def audit_elf_runtime(root, rel, require_interpreter=False):
    """Require an executable in-root PT_INTERP and a resolvable in-root DT_NEEDED closure."""
    root = Path(root).resolve(strict=True)
    pending = [str(rel)]
    seen = set()
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        elf = resolve_rootfs_regular(root, current)
        phdr = subprocess.run(["readelf", "-l", str(elf)], capture_output=True,
                              text=True, check=True)
        interpreters = re.findall(r"\[Requesting program interpreter:\s*([^]]+)\]", phdr.stdout)
        if len(interpreters) > 1:
            raise ValueError("ELF has ambiguous PT_INTERP: " + current)
        if require_interpreter and current == str(rel) and not interpreters:
            raise ValueError("ELF executable lacks PT_INTERP: " + current)
        if interpreters:
            interpreter = interpreters[0].strip()
            if not interpreter.startswith("/") or "\0" in interpreter:
                raise ValueError("ELF PT_INTERP is not an absolute in-root path: " + current)
            interpreter_rel = interpreter.lstrip("/")
            interpreter_file = resolve_rootfs_regular(root, interpreter_rel)
            if interpreter_file.stat().st_mode & 0o111 == 0:
                raise ValueError("ELF PT_INTERP is not executable: " + interpreter)
            pending.append(interpreter_rel)
        dynamic = subprocess.run(["readelf", "-d", str(elf)], capture_output=True,
                                 text=True, check=True)
        for soname in re.findall(r"\(NEEDED\).*?\[([^]]+)\]", dynamic.stdout):
            if not soname or "/" in soname or soname in (".", ".."):
                raise ValueError("ELF DT_NEEDED is not a library basename: " + soname)
            dependency = "lib/" + soname
            resolve_rootfs_regular(root, dependency)
            pending.append(dependency)


def oem_pool_entries(manifests, official_identity, exit_policy_override=None):
    """Fixed build-reviewed content rules, separate from loader approval."""
    if not isinstance(official_identity, dict):
        raise ValueError("reviewed official source identity required")
    source = official_identity.get("sha256")
    if source not in OEM_POOL_SOURCE_RULES or official_identity != OFFICIAL_SOURCES[source]:
        raise ValueError("unknown/mismatched OEM content source")
    product, alias, exit_policy, review = OEM_POOL_SOURCE_RULES[source]
    if exit_policy_override is not None:
        if exit_policy_override not in (1, 2):
            raise ValueError("only exact reviewed bootipq return/install policies are supported")
        exit_policy = exit_policy_override
    return [dict(manifest=m, source_package_sha256=source, source_review_id=review,
                 content_group=alias, exit_policy=exit_policy, product=product,
                 alias=alias, table_policy=0) for m in manifests]


def observed_hw_d_r012_post_attach_table(raw_table):
    """Build the one pinned HW-D R012 post-attach table from the official table."""
    if not isinstance(raw_table, bytes) or len(raw_table) != 128 * 172:
        raise ValueError("exact UBI volume-table bytes required")
    if sha(raw_table) != OEM_R012_SOURCE_TABLE_SHA256:
        raise ValueError("unexpected official R012 source volume table")
    names = ("kernel", "wifi_fw", "bt_fw", "ubi_rootfs", "rootfs_data")
    reserves = (50, 50, 3, 137, 1)
    kinds = (2, 2, 2, 1, 1)
    for index in range(128):
        row = raw_table[index * 172:(index + 1) * 172]
        if vendor_ubi.crc(row[:168]) != struct.unpack_from(">I", row, 168)[0]:
            raise ValueError("invalid source volume-table CRC")
        if index >= 5:
            if row[:168] != bytes(168):
                raise ValueError("unexpected source volume-table entry")
            continue
        name_len = struct.unpack_from(">H", row, 14)[0]
        if (struct.unpack_from(">I", row, 0)[0] != reserves[index] or
                struct.unpack_from(">I", row, 4)[0] != 1 or
                struct.unpack_from(">I", row, 8)[0] != 0 or row[12] != kinds[index] or
                row[13] != 0 or name_len != len(names[index]) or
                row[16:16 + name_len] != names[index].encode("ascii") or
                row[144] != (1 if index == 4 else 0) or row[145:168] != bytes(23)):
            raise ValueError("official R012 volume-table row differs from pinned source")

    changed = bytearray(raw_table)
    row_offset = 4 * 172
    struct.pack_into(">I", changed, row_offset, 56)
    changed[row_offset + 144] = 0
    struct.pack_into(">I", changed, row_offset + 168,
                     vendor_ubi.crc(changed[row_offset:row_offset + 168]))
    changed = bytes(changed)
    allowed = set(range(row_offset, row_offset + 4)) | {row_offset + 144} | set(range(row_offset + 168, row_offset + 172))
    if any(a != b and index not in allowed for index, (a, b) in enumerate(zip(raw_table, changed))):
        raise ValueError("post-attach table changed bytes outside rootfs_data row/CRC")
    if sha(changed) != OEM_R012_HW_D_POST_TABLE_SHA256:
        raise ValueError("reconstructed HW-D R012 post-attach table hash mismatch")
    return changed


def public_autoresize_source_table(stock_slot):
    """Require the official source's exact CRC-checked autoresize row shape."""
    table_meta, _, _, tables = vendor_ubi.parse(stock_slot)
    if (len(tables) != 2 or tables[0]["raw"] != tables[1]["raw"] or
            table_meta.get("volume_table_raw_sha256") != sha(tables[0]["raw"])):
        raise ValueError("public autoresize source must have two exact, CRC-checked table copies")
    raw = tables[0]["raw"]
    if len(raw) != 128 * 172:
        raise ValueError("public autoresize source has unexpected volume-table length")
    row = raw[4 * 172:5 * 172]
    name = b"rootfs_data"
    # The policy-2 normalizer may only reverse the exact source row created by
    # UBI autoresize. Check the full defined row fields and its CRC here before
    # emitting any policy-2 rule; matching later still validates every row.
    if (struct.unpack_from(">I", row, 0)[0] != 1 or
            struct.unpack_from(">I", row, 4)[0] != 1 or
            struct.unpack_from(">I", row, 8)[0] != 0 or row[12] != 1 or
            row[13] != 0 or struct.unpack_from(">H", row, 14)[0] != len(name) or
            row[16:16 + len(name)] != name or row[16 + len(name):144] != bytes(144 - 16 - len(name)) or
            row[144] != 1 or row[145:168] != bytes(23) or
            vendor_ubi.crc(row[:168]) != struct.unpack_from(">I", row, 168)[0]):
        raise ValueError("public policy2 source is not the exact rootfs_data reserve=1/autoresize=1 row")
    return sha(raw)


def generate_oem_header(out, build_include, stock_ubi, candidate_sha256, official_identity,
                        exit_policy_override=None, selected_geometry=None):
    """Derive logical same-source rules; never assert physical qualification."""
    slot_bytes = 40 * 1024 * 1024
    if not 0 < len(stock_ubi) <= slot_bytes:
        raise ValueError("official UBI exceeds exact physical slot")
    padded = stock_ubi + b"\xff" * (slot_bytes - len(stock_ubi))
    path = out / "stock-slot.ubi"
    path.write_bytes(padded)
    path.chmod(0o600)
    pinned = sha(padded)
    source_table_sha256 = public_autoresize_source_table(path) if selected_geometry is not None else None
    manifests = []
    geometries = ((selected_geometry["mtd_oob"], selected_geometry["ecc"]),) if selected_geometry else ((64, 4), (128, 4), (128, 8))
    for offset in (0x900000, 0x3100000):
        for oob, ecc in geometries:
            manifests.append(oem_manifest.build_manifest(path,
                expected_ubi_sha256=pinned, candidate_sha256=candidate_sha256,
                observed_geometry={"physical_offset": offset, "size_bytes": slot_bytes,
                    "erase_bytes": 131072, "page_bytes": 2048,
                    "oob_bytes": oob, "ecc_bits": ecc}))
    entries = oem_pool_entries(manifests, official_identity, exit_policy_override)
    if selected_geometry is not None:
        # Policy 2 is a narrowly normalized UBI autoresize table: the C matcher
        # verifies raw CRCs, table capacity, all non-rootfs_data rows and the
        # four immutable volume streams, then canonicalizes only row 4's
        # reserved_pebs/flags back to the exact source 1/1 row before hashing.
        # Keep the exact raw rule as well; this does not accept arbitrary tables.
        entries.extend(dict(entry, table_policy=2, alias=entry["alias"]+10000) for entry in tuple(entries))
    post_attach = None
    if (selected_geometry is None and official_identity == OFFICIAL_SOURCES[SOURCE_SHA] and
            candidate_sha256 in D_HW_R012_POST_ATTACH_CANDIDATE_SHA256S):
        source_meta, _, _, source_tables = vendor_ubi.parse(path)
        if (source_meta.get("volume_table_raw_sha256") != OEM_R012_SOURCE_TABLE_SHA256 or
                len(source_tables) != 2 or source_tables[0]["raw"] != source_tables[1]["raw"]):
            raise ValueError("official R012 source table copies do not match pinned baseline")
        post_table = observed_hw_d_r012_post_attach_table(source_tables[0]["raw"])
        base = next(m for m in manifests if m["geometry"] == {
            "physical_offset": 0x3100000, "size_bytes": slot_bytes,
            "erase_bytes": 131072, "page_bytes": 2048, "oob_bytes": 64, "ecc_bits": 4})
        post_manifest = dict(base)
        post_manifest["volume_table_sha256"] = sha(post_table)
        if post_manifest["immutable"] != base["immutable"]:
            raise ValueError("post-attach variant changed immutable volume bindings")
        product, _, exit_policy, review = OEM_POOL_SOURCE_RULES[SOURCE_SHA]
        post_attach = dict(manifest=post_manifest, source_package_sha256=SOURCE_SHA,
            source_review_id=review, content_group=OEM_R012_HW_D_POST_GROUP,
            exit_policy=exit_policy_override if exit_policy_override is not None else exit_policy,
            product=product, alias=OEM_R012_HW_D_POST_ALIAS,
            table_policy=0)
        entries.append(post_attach)
    pool_revision = sha(json.dumps({"schema": "bootbridge-oem-pool-v2", "rules": entries},
                         sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii"))
    (build_include / "bridge_oem_manifest.h").write_text(oem_manifest.c_pool_header(entries))
    return {"count": len(entries), "base_count": len(manifests), "source_padded_ubi_sha256": pinned,
            "pool_revision_sha256": pool_revision,
            "source_package_sha256": official_identity["sha256"],
            "source_volume_table_sha256": source_table_sha256,
            "exit_policy": entries[0]["exit_policy"],
            "table_policy": "RAW_EXACT+OEM_AUTORESIZE_NORMALIZED" if selected_geometry else "RAW_EXACT",
            "hw_d_r012_post_attach": ({"geometry": "B/0x3100000/spare64/ECC4",
                "table_policy": "RAW_EXACT", "source_table_sha256": OEM_R012_SOURCE_TABLE_SHA256,
                "table_sha256": OEM_R012_HW_D_POST_TABLE_SHA256,
            "candidate_sha256": candidate_sha256,
                "rootfs_data_reserved_pebs": 56, "rootfs_data_flags": 0,
                "content_group": OEM_R012_HW_D_POST_GROUP,
                "alias": OEM_R012_HW_D_POST_ALIAS} if post_attach else None),
            "scope": "logical same-input OEM version only; no physical qualification",
            "older_oem_versions": "UNAVAILABLE; trusted multi-version pool required before release"}


def stage_rootfs(original, output, cc, logs, stock_squashfs, payload, payload_report, build_include,
                 public_profile=None, runtime_metadata=None,
                 provider_install_path="/usr/sbin/bridge-check",
                 payload_install_path="/etc/bridge/uboot.bin",
                 include_payload_metadata=True, provider_executable_mode=0o755):
    if provider_executable_mode not in (0o555, 0o755):
        raise ValueError("provider executable mode is not allowed")
    output.mkdir()
    provenance = {}

    def copy(rel):
        provenance[rel] = copy_stock_bound(original, output, stock_squashfs, rel)

    for rel in ["bin/busybox", "sbin/kmodloader",
                "lib/firmware/qca-nss0-retail.bin"]:
        copy(rel)
    for p in sorted((original / "lib").glob("*.so*")):
        # Keep only the dependency closure for BusyBox and the explicit insmod.
        if p.name in ["libc.so", "ld-musl-arm.so.1", "libgcc_s.so.1", "libubox.so", "librpc.so"]:
            copy(str(p.relative_to(original)))
    for name in ["qca-ssdk", "qca-nss-dp", "qca-nss-drv"]:
        copy("lib/modules/4.4.60/" + name + ".ko")
    for d in ["dev", "proc", "sys", "tmp", "run", "etc/bridge", "usr/sbin", "usr/bin", "www", "root"]:
        (output / d).mkdir(parents=True, exist_ok=True)
    for rel in (provider_install_path, payload_install_path):
        path = Path(rel)
        if not path.is_absolute() or ".." in path.parts:
            raise ValueError("bundle install path must be a normalized absolute path")
        (output / str(path).lstrip("/")).parent.mkdir(parents=True, exist_ok=True)
    (output / "var").symlink_to("/tmp")
    for app in ["sh", "ash", "cat", "mkdir", "mknod", "mount", "ifconfig", "brctl", "ip", "sleep", "readlink", "grep", "awk", "route"]:
        if app != "busybox":
            (output / "bin" / app).symlink_to("busybox")
    (output / "usr/sbin/insmod").symlink_to("../../sbin/kmodloader")
    (output / "lib/firmware/qca-nss0.bin").symlink_to("qca-nss0-retail.bin")
    # Audit the real OEM ELF dependencies before producing an image. BusyBox
    # carries the OEM RPC dependency even when only its shell applet is used.
    runtime_elfs = ["bin/busybox", "sbin/kmodloader",
                    *[p for p in provenance if p.startswith("lib/") and ".so" in p and "symlink" not in provenance[p]]]
    for rel in runtime_elfs:
        audit_elf_runtime(output, rel, require_interpreter=rel in ("bin/busybox", "sbin/kmodloader"))
    runtime = PROJECT / "runtime"
    for source, target in [("boot.sh", "etc/bridge/boot.sh"), ("profile", "etc/bridge/profile"),
                           ("rootfs-id", "etc/bridge/rootfs-id"), ("preinit", "etc/preinit"),
                           ("init", "init")]:
        shutil.copyfile(runtime / source, output / target)
        (output / target).chmod(0o755 if source in ["boot.sh", "preinit", "init"] else 0o644)
    provider_rel = str(Path(provider_install_path).as_posix()).lstrip("/")
    for source, target in [(PROJECT / "src/bridge_check.c", provider_rel),
                           (runtime / "bridge_init.c", "sbin/init"),
                           (PROJECT / "src/bridge_dhcp.c", "usr/sbin/bridge-dhcp")]:
        is_provider = target == provider_rel
        extra = [PROJECT / "src/bridge_install.c", PROJECT / "src/bridge_version.c", PROJECT / "src/bridge_oem_slot.c", PROJECT / "src/bridge_backup.c", PROJECT / "src/bridge_mtd_observer.c", PROJECT / "src/bridge_first_install.c", PROJECT / "src/bridge_root_binding.c", PROJECT / "src/bridge_install_identity.c"] if is_provider else []
        return_source_flag = ['-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"'] if is_provider else []
        return_registry_flags = (['-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
                                  '-DBI_RETURN_SOURCE_REGISTRY_V2=1']
                                 if is_provider and public_profile is not None else [])
        first_install_flag = []
        if is_provider and public_profile is not None:
            first_install_flag = ['-DBI_FIRST_INSTALL_PROFILE_HEADER="bridge_first_install_profile.h"']
        payload_path_flag = ['-DBI_PAYLOAD_HEADER="bridge_payload.h"'] if is_provider else []
        candidate_profile_flags = []
        if is_provider and public_profile is not None and public_profile.get("candidate_profile_codegen"):
            candidate_profile_flags = [
                '-DBFI_PROFILE_RECORD_HEADER="bfi_candidate_profile.generated.h"',
                '-DBII_CANDIDATE_POLICY_HEADER="bii_candidate_policy.generated.h"']
        payload_patch_flags = (['-DBI_PAYLOAD_PATCH_HEADER="bridge_payload_patch.h"']
                               if is_provider and public_profile is not None and
                                  public_profile.get("candidate_profile_codegen") else [])
        root_binding_flags = ([root_binding_contract.COMPILE_FLAG] if is_provider else [])
        command([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static", "-s",
                 "-fno-ident", "-Wl,--build-id=none",
                 '-DBI_OEM_MANIFEST_HEADER="bridge_oem_manifest.h"', *return_source_flag,
                 *return_registry_flags,
                 *first_install_flag, *payload_path_flag, *candidate_profile_flags,
                 *payload_patch_flags, *root_binding_flags,
                 "-I", build_include, "-I", PROJECT / "src",
                 source, *extra, "-o", output / target],
                logs / (Path(target).name + "-compile.log"))
        if is_provider:
            (output / target).chmod(provider_executable_mode)
    payload_target = output / str(Path(payload_install_path).as_posix()).lstrip("/")
    payload_target.write_bytes(payload)
    payload_target.chmod(0o400)
    metadata = runtime_metadata if runtime_metadata is not None else runtime_payload_metadata(payload, payload_report)
    if include_payload_metadata:
        metadata_target = payload_target.with_suffix(".json")
        metadata_target.write_text(json.dumps(metadata, sort_keys=True, indent=2) + "\n")
        metadata_target.chmod(0o400)
    for p in (PROJECT / "web").iterdir():
        if p.is_file():
            shutil.copyfile(p, output / "www" / p.name)
            (output / "www" / p.name).chmod(0o644)
    # The bridge never inherits stock boot scripts, credentials, writable overlays,
    # shell consoles, Web updater, ENV/ART, or firmware-writing utilities.
    assert not any((output / p).exists() for p in ["etc/rc.d", "sbin/mount_root", "bin/webs", "bin/dnsmasq", "sbin/mtd"])
    return provenance


def build(args):
    os.environ.setdefault("STAGING_DIR", str(args.cc.resolve().parents[2]))
    official = regular(args.official)
    official_identity = identify_official(official)
    public_profile = None
    public_descriptor = None
    candidate_bfi_header = None
    candidate_bii_header = None
    candidate_patch_header = None
    candidate_patch_metadata = None
    payload_storage = None
    payload_storage_bytes = None
    candidate_descriptor = getattr(args, "candidate_descriptor", None)
    if candidate_descriptor is not None:
        if args.hardware_class not in PUBLIC_BOARD_INPUTS or args.uboot is not None:
            raise ValueError("fresh variant build requires one explicit board class and no --uboot")
        if candidate_descriptor.get("board") != args.hardware_class:
            raise ValueError("fresh candidate descriptor board differs from explicit provider class")
        payload = fresh_candidate_from_archive(args.candidate_archive, candidate_descriptor)
        profile_id = candidate_descriptor.get("nand_profile_id")
        board_mask = {"A":1,"B":2,"C":4,"D":8}[args.hardware_class]
        variants = getattr(args, "source_table_variants", ("A0", "C"))
        runtime_profiles = nand_schema.runtime_profiles(variants, board_mask)
        bound = [p for p in runtime_profiles if p["profile_id"] == profile_id and
                 p["ecc_bits"] == candidate_descriptor["storage_ecc_bits"]]
        if not bound:
            raise ValueError("fresh candidate NAND profile is not source/catalog compatible")
        if candidate_descriptor["load_ecc_bits"] != candidate_descriptor["storage_ecc_bits"]:
            raise ValueError("fresh candidate load ECC must equal storage ECC")
        nand_bound = bound[0]
        oob_mask = sum({64:1, 128:2, 256:4}[p["mtd_oob_bytes"]] for p in bound)
        if len({p["mtd_oob_bytes"] for p in bound}) != len(bound):
            raise ValueError("fresh source profile repeats an MTD OOB tuple")
        # ENV policy is source behavior. D alias is only compiled for the
        # exact NX30 source/product identity, not the shared RX3000 package.
        candidate_descriptor["build_profile"] = candidate_build_profile(
            candidate_descriptor, args.hardware_class, official_identity["model"], bound)
        payload_storage_bytes, candidate_patch_header, candidate_patch_metadata, payload_storage = \
            fresh_payload_patch(args.candidate_archive, candidate_descriptor, payload)
        if candidate_descriptor["load_ecc_bits"] not in (4, 8):
            raise ValueError("fresh candidate storage ECC differs from its exact NAND profile")
        public_profile = dict(PUBLIC_BOARD_INPUTS[args.hardware_class],
            **{"class": args.hardware_class, "product_name": official_identity.get("model"),
               "source_sha256": official_identity["sha256"], "nand_profile_id": profile_id,
               "nand_profile": dict(nand_bound, accepted_mtd_oob_mask=oob_mask),
               "nand_profiles": bound, "accepted_mtd_oob_mask": oob_mask,
               "ecc": candidate_descriptor["storage_ecc_bits"],
               "load_ecc": candidate_descriptor["load_ecc_bits"],
               "candidate_id": candidate_descriptor["candidate_id"],
               "record": "hw-%s-%s-%s-%s" % (args.hardware_class.lower(), profile_id,
                   candidate_descriptor["load_ecc_bits"], candidate_descriptor["storage_ecc_bits"])})
        if public_profile["product_name"] is None:
            raise ValueError("unrecognized exact official product identity")
        public_profile["model"] = nand_bound["model_aliases"][0]
        public_profile["manufacturer"] = nand_bound["manufacturer_id"]
        public_profile["chip"] = nand_bound["chip_id"]
        public_profile["raw_spare"] = nand_bound["raw_spare_bytes"]
        public_profile["mtd_oob"] = nand_bound["mtd_oob_bytes"]
        public_profile["accepted_mtd_oob_mask"] = oob_mask
        public_profile["capacity"] = nand_bound["capacity_mib"]
        public_profile["product_id"] = return_source.PRODUCTS[official_identity["sha256"]]
        components = official_bootchain_components(official, official_identity)
        public_profile["appsbl_sha256"] = components["appsbl_padded_sha256"]
        elf_bytes = candidate_manifest.extract_candidate_member_v2(args.candidate_archive,
            candidate_descriptor, "appsbl.elf")
        candidate_bfi_header, candidate_bii_header = candidate_profile_codegen.render_headers(candidate_descriptor,
            elf_bytes, bound, candidate_descriptor["build_profile"],
            candidate_descriptor["bss_metadata"])
        public_profile["candidate_profile_codegen"] = True
        payload_report = payload_report_json(regular(args.payload_report))
        nand_qualification = public_catalog_binding(payload_report, public_profile, "cc")
        public_descriptor = candidate_descriptor
    elif args.candidate_archive is not None:
        if args.hardware_class not in PUBLIC_BOARD_INPUTS or args.uboot is not None:
            raise ValueError("neutral archive build requires one explicit hardware class and no --uboot")
        payload, public_descriptor = public_candidate_from_archive(args.candidate_archive, args.hardware_class)
        public_profile = dict(PUBLIC_BOARD_INPUTS[args.hardware_class], **{
            "class": args.hardware_class, "product_name": official_identity.get("model"),
            "source_sha256": official_identity["sha256"]})
        if public_profile["product_name"] is None:
            raise ValueError("unrecognized exact official product identity")
        public_profile["product_id"] = return_source.PRODUCTS[official_identity["sha256"]]
        components = official_bootchain_components(official, official_identity)
        public_profile["appsbl_sha256"] = components["appsbl_padded_sha256"]
        if public_descriptor["files"]["appsbl.partition.bin"]["sha256"] != sha(payload):
            raise ValueError("candidate archive descriptor hash mismatch")
        payload_report = payload_report_json(regular(args.payload_report))
        nand_qualification = public_catalog_binding(payload_report, public_profile, "cc")
    else:
        if args.uboot is None or args.payload_report is None or args.hardware_class is not None:
            raise ValueError("legacy source build requires --uboot/--payload-report and no hardware class")
        payload = regular(args.uboot)
        payload_report = payload_report_json(regular(args.payload_report))
        nand_qualification = validate_payload_report(payload, payload_report)
    if len(payload) != 0x140000:
        raise ValueError("APPSBL candidate must have the fixed 0x140000-byte partition length")
    outer_size = struct.unpack_from(">I", official, 164)[0]
    outer = verify_fit(official[160:160 + outer_size])
    ubi_images = [p["data"] for n, p in outer.items() if n.startswith("/images/ubi-") and "data" in p]
    assert len(ubi_images) == 1
    root_contract = root_binding_contract.derive(official_identity["sha256"], ubi_images[0])
    out = args.output.resolve()
    if out.exists():
        raise ValueError("new output directory required; preserve existing evidence")
    out.mkdir(parents=True, mode=0o700)
    logs = out / "logs"
    logs.mkdir()
    build_include = out / ".generated-include"
    build_include.mkdir()
    root_contract_header = root_binding_contract.render_header(root_contract)
    (build_include / "bridge_root_binding_contract.generated.h").write_text(
        root_contract_header, encoding="ascii")
    (out / "bridge_root_binding_contract.generated.h").write_text(
        root_contract_header, encoding="ascii")
    (out / "bridge_root_binding_contract.generated.h").chmod(0o600)
    if candidate_bfi_header is not None:
        for name, content in (("bfi_candidate_profile.generated.h", candidate_bfi_header),
                              ("bii_candidate_policy.generated.h", candidate_bii_header)):
            (build_include / name).write_text(content, encoding="ascii")
            (out / name).write_text(content, encoding="ascii")
            (out / name).chmod(0o600)
    if candidate_patch_header is not None:
        (build_include / "bridge_payload_patch.h").write_text(candidate_patch_header, encoding="ascii")
        (out / "bridge_payload_patch.h").write_text(candidate_patch_header, encoding="ascii")
        (out / "bridge_payload_patch.h").chmod(0o600)
    (build_include / "h3c_nand_catalog.h").write_bytes(nand_qualification["catalog_bytes"])
    if public_profile is not None:
        (build_include / "bridge_payload.h").write_text(generated_public_payload_header(
            PROJECT / "src/bridge_payload.h", payload, public_descriptor, nand_qualification, components, public_profile))
    else:
        (build_include / "bridge_payload.h").write_text(generated_payload_header(
            PROJECT / "src/bridge_payload.h", payload, payload_report, nand_qualification))
    (out / "stock.ubi").write_bytes(ubi_images[0])
    oem_rules = generate_oem_header(out, build_include, ubi_images[0], sha(payload), official_identity,
                                    2 if public_profile is not None else None,
                                    public_profile if public_profile is not None else None)
    table, volumes, _, _ = vendor_ubi.parse(out / "stock.ubi")
    kernel, kernel_meta = bridge_kernel(volumes["kernel"], official_identity["sha256"])
    selected_route = public_profile["route"] if public_profile is not None else return_source.ROUTE
    return_source_metadata = return_source.metadata(volumes["kernel"], kernel,
        official_identity["sha256"], route=selected_route)
    return_source_header = return_source.c_header(return_source_metadata)
    (build_include / "bridge_oem_return_source.h").write_text(return_source_header)
    return_registry_header = None
    return_registry_table_mask = None
    return_registry_source_variants = None
    if public_profile is not None:
        source_variants = getattr(args, "source_table_variants", ("A0", "C"))
        return_registry_source_variants = list(source_variants)
        return_registry_table_mask = sum(nand_schema.TABLE_MASKS[v] for v in source_variants)
        return_registry_header = return_source.c_return_contract_header(
            [return_source_metadata], {official_identity["sha256"]: components},
            nand_schema.profiles()["profiles"], source_table_mask=return_registry_table_mask,
            exit_policy=2)
        (build_include / "bridge_oem_return_registry.h").write_text(return_registry_header)
    runtime_metadata = None
    if public_profile is not None:
        public_profile["compatible"] = return_source_metadata["compatible"]
        profile_header = generated_first_install_profile_header(args.hardware_class, official_identity, components,
            nand_profile=public_profile.get("nand_profile"), candidate_id=public_profile.get("candidate_id"))
        (build_include / "bridge_first_install_profile.h").write_text(profile_header)
        (out / "bridge_first_install_profile.h").write_text(profile_header)
        (out / "bridge_first_install_profile.h").chmod(0o600)
        runtime_metadata = public_runtime_payload_metadata(payload, public_descriptor, public_profile,
                                                            official_identity, return_source_metadata, nand_qualification)
    (out / "kernel.fit").write_bytes(kernel)
    for name in ["wifi_fw", "bt_fw"]:
        (out / (name + ".bin")).write_bytes(volumes[name])
    rootfs = out / "rootfs"
    (out / "stock.squashfs").write_bytes(volumes["ubi_rootfs"])
    provider_install_path = getattr(args, "provider_install_path", "/usr/sbin/bridge-check")
    payload_install_path = (payload_storage["path"] if payload_storage is not None else
        getattr(args, "payload_install_path", "/etc/bridge/uboot.bin"))
    provider_executable_mode = 0o555 if hasattr(args, "provider_install_path") else 0o755
    if public_profile is not None:
        generated_header = build_include / "bridge_payload.h"
        generated_header.write_text(generated_public_payload_header(
            PROJECT / "src/bridge_payload.h", payload, public_descriptor, nand_qualification,
            components, public_profile, payload_install_path))
    staged_payload = payload_storage_bytes if payload_storage_bytes is not None else payload
    provenance = stage_rootfs(args.rootfs, rootfs, args.cc, logs, out / "stock.squashfs", staged_payload,
        payload_report, build_include, public_profile=public_profile, runtime_metadata=runtime_metadata,
        provider_install_path=provider_install_path, payload_install_path=payload_install_path,
        include_payload_metadata=not hasattr(args, "provider_install_path"),
        provider_executable_mode=provider_executable_mode)
    preserved_return_header = out / "bridge_oem_return_source.h"
    preserved_return_header.write_text(return_source_header)
    preserved_return_header.chmod(0o600)
    if return_registry_header is not None:
        preserved_registry_header = out / "bridge_oem_return_registry.h"
        preserved_registry_header.write_text(return_registry_header)
        preserved_registry_header.chmod(0o600)
    shutil.rmtree(build_include)
    squash = out / "rootfs.squashfs"
    # CONFIG_DEVTMPFS is unset. Linux opens this node before starting PID 1;
    # create it inside SquashFS via metadata, never mknod on the build host.
    (out / "device-nodes.pseudo").write_text("dev/console c 0600 0 0 5 1\n")
    command([args.mksquashfs, rootfs, squash, "-comp", "xz", "-b", "131072",
             "-noappend", "-all-root", "-no-xattrs", "-no-progress", "-processors", "1",
             "-mkfs-time", str(EPOCH), "-all-time", str(EPOCH),
             "-pf", out / "device-nodes.pseudo"], logs / "mksquashfs.log")
    images = {"kernel": out / "kernel.fit", "wifi_fw": out / "wifi_fw.bin",
              "bt_fw": out / "bt_fw.bin", "ubi_rootfs": squash}
    config = []
    for vol in table["volume_table"]:
        lines = ["[" + vol["name"] + "]", "mode=ubi", "vol_id=" + str(vol["id"]),
                 "vol_name=" + vol["name"], "vol_type=" + vol["type"],
                 "vol_size=" + str(vol["capacity"])]
        if vol["name"] in images:
            assert images[vol["name"]].stat().st_size <= vol["capacity"]
            lines += ["image=" + str(images[vol["name"]])]
        if vol["autoresize"]:
            lines += ["vol_flags=autoresize"]
        config += lines + [""]
    (out / "ubinize.cfg").write_text("\n".join(config))
    command([args.ubinize, "-o", out / "bridge.ubi", "-m", "2048", "-p", "131072",
             "-s", "2048", "-O", "2048", "-Q", "1791907200", out / "ubinize.cfg"], logs / "ubinize.log")
    ubi_meta, extracted, _, _ = vendor_ubi.parse(out / "bridge.ubi")
    built_rootfs_geometry = root_binding_contract.verify_same_source_and_built(
        root_contract, regular(out / "bridge.ubi"))
    assert ubi_meta["volume_table"] == table["volume_table"]
    assert all(extracted[n] == volumes[n] for n in ["wifi_fw", "bt_fw"])
    assert extracted["kernel"] == kernel and extracted["rootfs_data"] == b""
    squash_bytes = regular(squash)
    assert extracted["ubi_rootfs"][:len(squash_bytes)] == squash_bytes
    assert extracted["ubi_rootfs"][len(squash_bytes):] == b"\xff" * (len(extracted["ubi_rootfs"]) - len(squash_bytes))
    ubi = regular(out / "bridge.ubi")
    node = "ubi-bridge-" + hashlib.sha1(ubi).hexdigest()
    its = '''/dts-v1/; / { description = "BootBridge read-only reference-route laboratory image";
        #address-cells = <1>; timestamp = <1791907200>;
        images { %s { description = "BootBridge diagnostic UBI"; data = /incbin/("bridge.ubi");
        type = "firmware"; arch = "arm"; compression = "none";
        hash@1 { algo = "crc32"; value = [00 00 00 00]; };
        hash@2 { algo = "sha1"; value = [%s]; }; }; };
        configurations { default = "conf-bridge"; conf-bridge { firmware = "%s"; }; }; };''' % (node, "00 " * 20, node)
    (out / "bridge.its").write_text(its)
    command([args.dtc, "-I", "dts", "-O", "dtb", "-o", out / "bridge-unhashed.itb", out / "bridge.its"], logs / "dtc.log")
    fit = FDT(regular(out / "bridge-unhashed.itb"))
    for n, algo in [("hash@1", "crc32"), ("hash@2", "sha1")]:
        fit.set("/images/" + node + "/" + n, "value", digest(ubi, algo))
    fit_data = fit.bytes()
    assert set(p for p in verify_fit(fit_data) if p.startswith("/images/") and p.count("/") == 2) == {"/images/" + node}
    (out / "bridge.itb").write_bytes(fit_data)
    # Preserve the OEM product/version gate and opaque identity bytes. Only lengths
    # and checksum change; do not claim another official firmware revision.
    outer_payload = fit_data + b"\0" * (-len(fit_data) % 4)
    header = bytearray(official[:160])
    struct.pack_into(">I", header, 68, 96 + len(outer_payload))
    struct.pack_into(">I", header, 80, len(outer_payload))
    struct.pack_into(">I", header, 72, 0)
    packaged = header + outer_payload
    word_sum = sum(v[0] for v in struct.iter_unpack(">I", packaged[64:])) & 0xffffffff
    struct.pack_into(">I", packaged, 72, (0xffffffff - word_sum) & 0xffffffff)
    assert sum(v[0] for v in struct.iter_unpack(">I", packaged[64:])) & 0xffffffff == 0xffffffff
    assert packaged[99:160] == official[99:160] and packaged[135] == 1
    candidate = out / (official_identity["identity"] + "-BOOTBRIDGE-DIAG-NOT-QUALIFIED.bin")
    candidate.write_bytes(packaged)
    candidate.chmod(0o600)
    is_public = public_profile is not None
    report = {"schema": 1, "profile": PROFILE, "status": "OFFLINE_LAB_CANDIDATE",
              "device_operations": False, "tester_release_ready": False,
              "install_available": False, "backup_download_required": False,
              "maintenance_ip": "192.168.10.1",
              "embedded_uboot": ({"candidate_id": public_descriptor["candidate_id"],
                  "archive_sha256": public_descriptor.get("archive_sha256",
                      PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256),
                  "board": public_profile["class"], "partition_bytes": len(payload),
                  "partition_sha256": sha(payload), "hardware_validation": "NOT_EXECUTED"}
                  if is_public else payload_report),
              "nand_qualification": {"catalog_sha256": nand_qualification["catalog_sha256"],
                "supported_nand_profile_ids": nand_qualification["supported"],
                "appsbl_load_profiles": nand_qualification["load"],
                "appsbl_storage_profiles": nand_qualification["storage"],
                "ecc_bits": public_profile["ecc"] if is_public else 4,
                "sbl_sha256": components["sbl_sha256"] if is_public else SBL_SHA,
                "sbl_valid_bytes": components["sbl_valid_bytes"] if is_public else SBL_VALID_BYTES,
                "catalog_scope": "runtime-read-and-candidate-geometry-only",
                "boot_chain_qualification": public_profile["record"] if is_public else nand_qualification["boot_chain_qualification"],
                "install_qualification_available": True if is_public else nand_qualification["install_qualification_available"]},
              "source_official_sha256": official_identity["sha256"],
              "source_official_identity": official_identity,
              "hardware_class": public_profile["class"] if is_public else "D",
              "hardware_validation": "NOT_EXECUTED" if is_public else "D_SCOPE_ONLY",
              "public_candidate_descriptor": public_descriptor if is_public else None,
              "payload_storage": payload_storage,
              "payload_patch": candidate_patch_metadata,
              "official_bootchain_components": components if is_public else None,
              "return_source": return_source_metadata,
              "return_source_registry": ({"path": "bridge_oem_return_registry.h",
                  "sha256": sha(return_registry_header.encode("ascii")),
                  "exit_policy": 2, "source_table_mask": return_registry_table_mask,
                  "source_table_variants": return_registry_source_variants}
                  if return_registry_header is not None else None),
              "runtime_mode": "CONTROLLED_TRIAL_RUNTIME_GATED" if is_public else ("D_FIRST_INSTALL_RUNTIME_GATED" if nand_qualification["install_qualification_available"] else "READONLY_DIAGNOSTIC"), "hardware_classification": "EXPLICIT_COMPILED_CLASS" if is_public else "UNKNOWN",
              "candidate_hardware_scope": ("one neutral public candidate selected by explicit hardware class; runtime exact profile required; hardware validation NOT_EXECUTED" if is_public else "single D payload experimental material; not a runtime hardware match"),
              "multi_payload_provider": "explicit single-candidate build per selected class" if is_public else "UNAVAILABLE",
              "oem_return_rules": oem_rules,
              "reference_routes_enabled": sorted(kernel_transform.ROUTES),
              "unmodified_reference_routes": 21,
              "candidate": {"name": candidate.name,
              "bytes": len(packaged), "sha256": sha(packaged)}, "kernel": kernel_meta,
              "outer_fit_components": [node], "outer_u_boot_components": [],
              "outer_sbl_mibib_env_art_components": [], "oem_version_gate_preserved": True,
              "outer_checksum_valid": True, "ubi": {k: v for k, v in ubi_meta.items() if k not in ["rows", "path"]},
              "root_binding_contract": {**root_binding_contract.report_record(root_contract),
                  "built_ubi": built_rootfs_geometry},
              "rootfs": {"bytes": len(squash_bytes), "sha256": sha(squash_bytes),
                         "oem_input_files": provenance},
              "not_executed": ["target OEM Web acceptance", "target authentication/security state",
                               "hardware NAND/ECC reads", "bridge cold boot", "LAN DHCP/HTTP on device"]}
    (out / "BUILD-REPORT.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "candidate": report["candidate"], "kernel": kernel_meta}, ensure_ascii=False))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ["official", "rootfs", "output", "cc", "ubinize"]:
        p.add_argument("--" + key, required=True, type=Path)
    p.add_argument("--uboot", type=Path, help="legacy pinned D candidate input")
    p.add_argument("--payload-report", type=Path, required=True,
                   help="catalog/report source; for archive candidates only catalog fields are used")
    p.add_argument("--candidate-archive", type=Path,
                   help="single pinned neutral ABCD candidate archive")
    p.add_argument("--hardware-class", choices=tuple(PUBLIC_BOARD_INPUTS),
                   help="explicit compiled board class for a neutral archive candidate")
    p.add_argument("--mksquashfs", default="mksquashfs")
    p.add_argument("--dtc", default="dtc")
    args = p.parse_args()
    if bool(args.candidate_archive) != bool(args.hardware_class):
        p.error("--candidate-archive and --hardware-class must be supplied together")
    build(args)


if __name__ == "__main__":
    main()
