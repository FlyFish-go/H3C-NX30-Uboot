#!/usr/bin/env python3
"""Independently decode a finished bridge package; ordinary host files only."""
if not __debug__:
    raise RuntimeError("bridge package audit requires assertions; Python optimization is unsupported")

import argparse
import hashlib
import json
import re
from pathlib import Path
import stat
import struct
import subprocess
import tempfile
import zlib

import vendor_fit
import vendor_ubi
import build
import bridge_kernel
import build_oem_manifest
import build_return_source as return_source
import candidate_manifest
import nand_schema
import candidate_profile_codegen
import payload_patch_codegen
import root_binding_contract

SOURCE_SHA = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"
CANONICAL_CANDIDATE_SHA = "61b10ffdec0af3101657bccd5f4425ca4e8a8e6dc62dd2e8d5f8d08e73bc3595"
PUBLIC_CANDIDATE_SHA = "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362"
HW_D_POST_ATTACH_CANDIDATES = frozenset((CANONICAL_CANDIDATE_SHA, PUBLIC_CANDIDATE_SHA))
R012_SOURCE_TABLE_SHA = "02475743e130eb615a0cf5a1fc93cc51613ddf201fc96c6ef043687b0b984ed9"
R012_HW_D_POST_TABLE_SHA = "1a581827d678e71ddd313f5824ed493587ca15374e88a3f2ddb62b73edc094f9"
R012_HW_D_POST_ALIAS = 10013
R012_HW_D_POST_GROUP = 10013
SBL_SHA = "c8bc5e1d35a3092dc1292d1b8ba13061082e08992a2cb07c05dbf7561a1244b5"
SBL_VALID_BYTES = 178176
AUDIT_D_CANONICAL_RECORD = "hw-d-r012-5f486f5f-low-ecc4-v1"
AUDIT_D_CANONICAL_SHA = "61b10ffdec0af3101657bccd5f4425ca4e8a8e6dc62dd2e8d5f8d08e73bc3595"
AUDIT_D_SOURCE_COMMIT = "5f486f5f9852c26d7d16eb5980a06cccea2a5217"
AUDIT_D_CANONICAL_SOURCE_SHA = "b1144b5a8b20ca543acd7278083b6e7246e21c5b66a8f7d8e6fa1645c7acd0c5"
AUDIT_D_PUBLIC_RECORD = "hw-d-r012-public-91e9-low-ecc4-trial-v1"
AUDIT_D_PUBLIC_SHA = "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362"
AUDIT_D_PUBLIC_MBN_SHA = "ca3e059c9de2209c793948fbb5f155cc8b5cb2011b5c1ca9a903321157dce713"
AUDIT_D_PUBLIC_LOAD_SHA = "e524e0f1429323ee3d612284e5ec0d66059996999e0a03dc891334ec66225572"
AUDIT_D_PUBLIC_SOURCE_SHA = "a4557d97bc569fb6b75d389a09bc5235c7948951cb98feff92652e72e82dba55"
AUDIT_D_PUBLIC_ARCHIVE_SHA = "bcca16ce8060b6f09094c10d0af40f9e48343d62da8e08ffe6cadbca8725c732"
AUDIT_D_PUBLIC_PROVENANCE_SHA = "5281a9894be5ba36018c194321d3cc505360c1d9436b36c0a5d22c7224dd5542"
AUDIT_D_RELEASE_SCOPE = "hw-d-r012-efba-sp64-ecc4-first-install"
AUDIT_D_OEM_SHA = "7f1f7bf0938aae5d6c5816d6816524d5877eefb41e9fbdf4427b24bf34fb2b48"
AUDIT_D_PROFILE = ["h3c-oem-efba-128m-2k-sp64"]


def audit_d_loader_record(payload, report):
    """Independent exact-record selection; never trusts report PASS flags."""
    if (len(payload) == 0x140000 and sha(payload) == AUDIT_D_CANONICAL_SHA and
            report.get("derivative_commit") == AUDIT_D_SOURCE_COMMIT and
            report.get("source_sha256") == AUDIT_D_CANONICAL_SOURCE_SHA and
            report.get("build_profile") == "release" and
            report.get("appsbl_load_profiles") == AUDIT_D_PROFILE and
            report.get("appsbl_storage_profiles") == AUDIT_D_PROFILE and
            report.get("supported_nand_profile_ids") == AUDIT_D_PROFILE):
        return AUDIT_D_CANONICAL_RECORD
    identity = report.get("release_identity")
    known = report.get("known_releases")
    expected_known = [
        {"scope": AUDIT_D_RELEASE_SCOPE, "sequence": 0, "sha256": AUDIT_D_OEM_SHA,
         "bytes": 0x140000, "kind": "oem_baseline"},
        {"scope": AUDIT_D_RELEASE_SCOPE, "sequence": 1, "sha256": AUDIT_D_CANONICAL_SHA,
         "bytes": 0x140000, "kind": "custom_release"},
    ]
    if (len(payload) == 0x140000 and sha(payload) == AUDIT_D_PUBLIC_SHA and
            report.get("partition_sha256") == AUDIT_D_PUBLIC_SHA and
            report.get("mbn_sha256") == AUDIT_D_PUBLIC_MBN_SHA and
            report.get("load_sha256") == AUDIT_D_PUBLIC_LOAD_SHA and
            report.get("base_commit") == AUDIT_D_SOURCE_COMMIT and
            report.get("derivative_commit") == AUDIT_D_SOURCE_COMMIT and
            report.get("source_digest_kind") == "public-source-checksum-list-v1" and
            report.get("source_sha256") == AUDIT_D_PUBLIC_SOURCE_SHA and
            report.get("public_source_archive_sha256") == AUDIT_D_PUBLIC_ARCHIVE_SHA and
            report.get("public_source_provenance_sha256") == AUDIT_D_PUBLIC_PROVENANCE_SHA and
            report.get("build_profile") == "release" and
            report.get("appsbl_load_profiles") == AUDIT_D_PROFILE and
            report.get("appsbl_storage_profiles") == AUDIT_D_PROFILE and
            report.get("supported_nand_profile_ids") == AUDIT_D_PROFILE and
            report.get("ecc_bits") == 4 and report.get("appsbl_load_ecc_bits") == 4 and
            report.get("appsbl_storage_ecc_bits") == 4 and
            report.get("sbl_sha256") == SBL_SHA and report.get("sbl_valid_bytes") == SBL_VALID_BYTES and
            identity == {"scope": AUDIT_D_RELEASE_SCOPE, "sequence": 2,
                         "candidate_sha256": AUDIT_D_PUBLIC_SHA, "candidate_bytes": 0x140000} and
            known == expected_known):
        return AUDIT_D_PUBLIC_RECORD
    return "UNAVAILABLE"


def audit_hw_d_post_attach_table(raw_table):
    """Independently reconstruct the single observed HW-D R012 table variant."""
    if len(raw_table) != 128 * 172 or sha(raw_table) != R012_SOURCE_TABLE_SHA:
        raise ValueError("unexpected R012 source table for post-attach rule")
    names = (b"kernel", b"wifi_fw", b"bt_fw", b"ubi_rootfs", b"rootfs_data")
    reserves = (50, 50, 3, 137, 1)
    kinds = (2, 2, 2, 1, 1)
    for index in range(128):
        row = raw_table[index * 172:(index + 1) * 172]
        if (zlib.crc32(row[:168]) ^ 0xffffffff) & 0xffffffff != struct.unpack_from(">I", row, 168)[0]:
            raise ValueError("R012 source table CRC failure")
        if index >= 5:
            if row[:168] != bytes(168):
                raise ValueError("unexpected nonempty R012 source table row")
            continue
        name_len = struct.unpack_from(">H", row, 14)[0]
        if (struct.unpack_from(">I", row, 0)[0] != reserves[index] or
                struct.unpack_from(">I", row, 4)[0] != 1 or
                struct.unpack_from(">I", row, 8)[0] != 0 or row[12] != kinds[index] or
                row[13] != 0 or name_len != len(names[index]) or
                row[16:16 + name_len] != names[index] or
                row[144] != (1 if index == 4 else 0) or row[145:168] != bytes(23)):
            raise ValueError("R012 source volume-table fields differ from reviewed source")
    changed = bytearray(raw_table)
    offset = 4 * 172
    struct.pack_into(">I", changed, offset, 56)
    changed[offset + 144] = 0
    struct.pack_into(">I", changed, offset + 168,
                     (zlib.crc32(changed[offset:offset + 168]) ^ 0xffffffff) & 0xffffffff)
    allowed = set(range(offset, offset + 4)) | {offset + 144} | set(range(offset + 168, offset + 172))
    if any(a != b and i not in allowed for i, (a, b) in enumerate(zip(raw_table, changed))):
        raise ValueError("R012 post-attach table changed unexpected bytes")
    result = bytes(changed)
    if sha(result) != R012_HW_D_POST_TABLE_SHA:
        raise ValueError("R012 post-attach table does not match observed exact hash")
    return result


def audit_hw_d_post_attach_manifest(source_sha256, candidate_sha256, base_manifest, raw_table):
    """Independently derive the only reviewed post-attach rule for exact D inputs."""
    if source_sha256 != SOURCE_SHA or candidate_sha256 not in HW_D_POST_ATTACH_CANDIDATES:
        return None
    if base_manifest.get("candidate_sha256") != candidate_sha256:
        raise ValueError("post-attach base manifest candidate binding mismatch")
    post_table = audit_hw_d_post_attach_table(raw_table)
    post_manifest = dict(base_manifest)
    post_manifest["volume_table_sha256"] = sha(post_table)
    report = {"geometry": "B/0x3100000/spare64/ECC4",
        "table_policy": "RAW_EXACT", "source_table_sha256": R012_SOURCE_TABLE_SHA,
        "table_sha256": R012_HW_D_POST_TABLE_SHA, "candidate_sha256": candidate_sha256,
        "rootfs_data_reserved_pebs": 56, "rootfs_data_flags": 0,
        "content_group": R012_HW_D_POST_GROUP, "alias": R012_HW_D_POST_ALIAS}
    return post_manifest, report

# Return-source contract, independently restated here so package audit does not
# trust a generated builder report or helper output. This is a byte-source
# expectation only; the separately reviewed return table remains authoritative.
RETURN_ROUTE = "config@mp02.1"
RETURN_COMPATIBLE = "qcom,ipq5018-mp02.1"
RETURN_PRODUCT = {
    "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922": 1,
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": 2,
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": 3,
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": 4,
}
RETURN_NAND_SHA256 = "8a59fdb5d6864a98fa833d0fa56aa615a13acc99038c459d7e0039fed460acfd"
RETURN_NAND_ROOTS = ("/soc/qpic-nand@79b0000", "/soc/pinctrl@1000000/qspi_nand_pins")
RETURN_NAND_NODES = frozenset((RETURN_NAND_ROOTS[0], RETURN_NAND_ROOTS[0] + "/nandcs@0",
    RETURN_NAND_ROOTS[1], *(RETURN_NAND_ROOTS[1] + "/" + n for n in
    ("qspi_clock", "qspi_cs", "qspi_data_0", "qspi_data_1", "qspi_data_2", "qspi_data_3"))))


def return_canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True).encode("ascii")


def return_nand_closure(dt_nodes):
    selected = {path: {key: value.hex() for key, value in props.items()}
                for path, props in dt_nodes.items()
                if any(path == root or path.startswith(root + "/") for root in RETURN_NAND_ROOTS)}
    if set(selected) != RETURN_NAND_NODES:
        raise ValueError("return-source NAND/pinctrl closure has missing or unexpected nodes")
    return return_canonical(selected)


def return_route_data(kernel_fit, route=RETURN_ROUTE):
    if (not isinstance(kernel_fit, bytes) or len(kernel_fit) < 40 or
            kernel_fit[:4] != b"\xd0\x0d\xfe\xed" or struct.unpack_from(">I", kernel_fit, 4)[0] != len(kernel_fit)):
        raise ValueError("return-source kernel FIT must have exact declared length")
    fit = nodes(kernel_fit)
    selected = bridge_kernel.ROUTES.get(route)
    if selected is None:
        raise ValueError("return-source route is not an exact supported kernel config")
    fdt_name, expected_compatible, _ = selected
    config = fit.get("/configurations/" + route)
    if config is None or config.get("kernel") != b"kernel@1\0" or config.get("fdt") != fdt_name.encode("ascii") + b"\0":
        raise ValueError("return-source route missing or differs from explicit hardware-class route")
    kernel = fit.get("/images/kernel@1", {}).get("data")
    dt = fit.get("/images/" + fdt_name, {}).get("data")
    if not isinstance(kernel, bytes) or not kernel or not isinstance(dt, bytes):
        raise ValueError("return-source route lacks actual kernel/DT payload bytes")
    dt_nodes = vendor_fit.fdt_props(dt)
    actual_compatible = dt_nodes.get("/", {}).get("compatible")
    if actual_compatible != (expected_compatible + "\0qcom,ipq5018\0").encode("ascii"):
        raise ValueError("return-source DT compatible differs from reviewed route")
    return kernel, return_nand_closure(dt_nodes)


def return_source_metadata(stock_kernel_fit, bridge_kernel_fit, official_sha256, route=RETURN_ROUTE):
    """Derive exact return-source metadata from the actual nested kernel FITs."""
    product = RETURN_PRODUCT.get(official_sha256)
    source = bridge_kernel.SOURCES.get(official_sha256)
    if product is None or source is None or sha(stock_kernel_fit) != source[1]:
        raise ValueError("return-source official package/stock kernel FIT pin mismatch")
    stock_kernel, stock_nand = return_route_data(stock_kernel_fit, route)
    bridge_kernel_bytes, bridge_nand = return_route_data(bridge_kernel_fit, route)
    if stock_kernel != bridge_kernel_bytes:
        raise ValueError("return-source compressed kernel bytes differ")
    if stock_nand != bridge_nand or sha(stock_nand) != RETURN_NAND_SHA256:
        raise ValueError("return-source NAND/pinctrl closure differs or is unreviewed")
    value = {"schema": 1, "official_sha256": official_sha256,
             "stock_kernel_sha256": sha(stock_kernel_fit),
             "bridge_kernel_sha256": sha(bridge_kernel_fit),
             "compressed_kernel_sha256": sha(stock_kernel),
             "nand_dt_sha256": sha(stock_nand), "product": product,
             "route": route, "compatible": bridge_kernel.ROUTES[route][1]}
    value["revision_sha256"] = sha(return_canonical(value))
    return value


def return_source_header(value):
    """Render the exact generated C header from independently checked fields."""
    mapping = (("official_sha256", "OFFICIAL_SHA256"),
               ("stock_kernel_sha256", "STOCK_KERNEL_SHA256"),
               ("bridge_kernel_sha256", "BRIDGE_KERNEL_SHA256"),
               ("compressed_kernel_sha256", "COMPRESSED_KERNEL_SHA256"),
               ("nand_dt_sha256", "NAND_DT_SHA256"),
               ("revision_sha256", "REVISION_SHA256"),
               ("product", "PRODUCT"), ("route", "ROUTE"),
               ("compatible", "COMPATIBLE"))
    lines = ["/* Source expectation only: not reviewed approval, BC evidence, or hardware identity. */",
             "#ifndef BOOTBRIDGE_RETURN_SOURCE_H", "#define BOOTBRIDGE_RETURN_SOURCE_H"]
    for field, macro in mapping:
        rendered = str(value[field]) if field == "product" else json.dumps(value[field], ensure_ascii=True)
        lines.append("#define BI_RETURN_SOURCE_" + macro + " " + rendered)
    return ("\n".join(lines + ["#endif", ""])).encode("ascii")


def require_arm_source_marker(binary, revision):
    marker = ("BB_RETURN_SOURCE_REVISION:" + revision).encode("ascii")
    if binary.count(marker) != 1:
        raise ValueError("ARM installer must embed exactly one independently recomputed source revision marker")
    return marker.decode("ascii")


def read(path):
    p = Path(path)
    if not stat.S_ISREG(p.lstat().st_mode) or str(p.resolve()).startswith("/dev/"):
        raise ValueError("ordinary offline files only")
    return p.read_bytes()


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def public_provider_paths(hardware_class):
    """Return fixed runtime paths for an explicitly selected public board class."""
    if not isinstance(hardware_class, str) or hardware_class not in build.PUBLIC_BOARD_INPUTS:
        raise ValueError("public provider paths require an explicit A/B/C/D class")
    return ("usr/sbin/bridge-check-HW-" + hardware_class,
            "etc/bridge/providers/HW-" + hardware_class + "/uboot.bin")


def provider_path_contract(hardware_class, bundle_provider_paths=False, fresh_candidate_id=None):
    """Select the report-bound path ABI for public single or bundle builds."""
    if bundle_provider_paths:
        if fresh_candidate_id is not None:
            slug = "c-" + hashlib.sha256(fresh_candidate_id.encode("utf-8")).hexdigest()[:20]
            return ("usr/sbin/bridge-candidates/" + slug + "/provider",
                    "etc/bridge/providers/" + slug + "/uboot.bin"), 0o555
        return public_provider_paths(hardware_class), 0o555
    return ("usr/sbin/bridge-check", "etc/bridge/uboot.bin"), 0o755


def fresh_payload_storage_contract(archive_path, archive_rows, descriptor, final_payload):
    """Independently select and regenerate the data-only EF:BA base/patch."""
    bases = [row for row in archive_rows if row.get("board") == descriptor.get("board") and
             row.get("nand_profile_id") == "h3c-oem-efba-128m-2k-sp64" and
             row.get("load_ecc_bits") == 4 and row.get("storage_ecc_bits") == 4]
    if len(bases) != 1:
        raise ValueError("fresh archive lacks one unique per-board EF:BA 4/4 base")
    base_descriptor = bases[0]
    base_bytes = candidate_manifest.extract_candidate_partition_v2(Path(archive_path), base_descriptor)
    header, metadata = payload_patch_codegen.generate_patch_header(
        base_descriptor, base_bytes, descriptor, final_payload)
    storage_path = "/etc/bridge/provider-payload-bases/" + metadata["base_sha256"] + ".bin"
    storage = {"encoding": "base-patch-v1", "path": storage_path,
        "sha256": metadata["base_sha256"], "bytes": metadata["base_bytes"],
        "patch_header_sha256": sha(header.encode("ascii")),
        "patch_count": metadata["patch_count"], "data_bytes": metadata["patch_bytes"]}
    return base_bytes, header, metadata, storage


def squashfs_regular_file(squashfs, relative_path, expected_mode, label):
    """Extract one fixed path to verify its actual SquashFS mode and file type."""
    rel = Path(relative_path)
    if rel.is_absolute() or ".." in rel.parts or not rel.parts:
        raise ValueError("unsafe SquashFS audit path")
    with tempfile.TemporaryDirectory(prefix="bridge-squashfs-entry-") as tmp:
        # unsquashfs requires its extraction root not to exist yet. Keep the
        # TemporaryDirectory as the cleanup owner and extract into a fresh child.
        dest = Path(tmp) / "extract"
        cp = subprocess.run(["unsquashfs", "-no-progress", "-d", str(dest),
                             str(squashfs), rel.as_posix()],
                            capture_output=True, timeout=30)
        extracted = dest / rel
        if cp.returncode or not extracted.exists() and not extracted.is_symlink():
            detail = cp.stderr.decode("utf-8", "replace").strip()
            suffix = ": " + detail if detail else ""
            raise ValueError("missing SquashFS file: " + label + suffix)
        st = extracted.lstat()
        if not stat.S_ISREG(st.st_mode) or extracted.is_symlink():
            raise ValueError("SquashFS entry is not a regular file: " + label)
        if stat.S_IMODE(st.st_mode) != expected_mode:
            raise ValueError("SquashFS file mode mismatch: " + label)
        return extracted.read_bytes()


def squashfs_has_file(squashfs, relative_path):
    rel = Path(relative_path)
    if rel.is_absolute() or ".." in rel.parts or not rel.parts:
        raise ValueError("unsafe SquashFS audit path")
    cp = subprocess.run(["unsquashfs", "-lc", "-no-progress", str(squashfs)],
                        capture_output=True, timeout=30)
    if cp.returncode:
        raise ValueError("cannot list audited SquashFS")
    wanted = "squashfs-root/" + rel.as_posix()
    return wanted in cp.stdout.decode("utf-8", "replace").splitlines()


def nodes(blob):
    if len(blob) < 40 or blob[:4] != b"\xd0\x0d\xfe\xed":
        raise ValueError("short FIT")
    total = struct.unpack_from(">I", blob, 4)[0]
    if not 40 <= total <= len(blob):
        raise ValueError("invalid FIT bounds")
    tree = vendor_fit.fdt_props(blob[:total])
    for path, props in tree.items():
        if "algo" in props:
            algo = props["algo"].rstrip(b"\0").decode()
            if algo not in ("crc32", "sha1", "sha256"):
                raise ValueError("unreviewed FIT hash algorithm")
            payload = tree[path.rsplit("/", 1)[0]]["data"]
            want = struct.pack(">I", zlib.crc32(payload) & 0xffffffff) if algo == "crc32" else hashlib.new(algo, payload).digest()
            if want != props["value"]:
                raise ValueError("FIT integrity failure: " + path)
    for path,props in tree.items():
        if path.startswith("/images/") and path.count("/") == 2:
            if "data" not in props or not any(n.startswith(path + "/") and "algo" in p for n,p in tree.items()):
                raise ValueError("FIT image lacks internal data/digest")
    return tree


def audit_kernel(candidate, original):
    """Independent semantic preservation check, never rerun the transformer."""
    if any(len(b) < 40 or struct.unpack_from(">I", b, 4)[0] != len(b) for b in (candidate, original)):
        raise ValueError("kernel FIT must have exact declared length")
    k, old = nodes(candidate), nodes(original)
    if set(k) != set(old):
        raise ValueError("kernel FIT topology changed")
    configs = [p for p in old if p.startswith("/configurations/") and p.count("/") == 2]
    images = [p for p in old if p.startswith("/images/") and p.count("/") == 2]
    if len(configs) != 24 or len(images) != 25:
        raise ValueError("unreviewed original FIT layout")
    selected = {"/images/" + v[0] for v in bridge_kernel.ROUTES.values()}
    for path in old:
        image = next((x for x in selected if path == x or path.startswith(x + "/")), None)
        if image is None:
            if k[path] != old[path]:
                raise ValueError("preserved kernel/config/default/image metadata changed: " + path)
        else:
            excluded = {"data"} if path == image else {"value"}
            if {x:v for x,v in k[path].items() if x not in excluded} != {x:v for x,v in old[path].items() if x not in excluded}:
                raise ValueError("selected FIT metadata changed: " + path)
    changes = {}
    expected = {(p, "status") for p in bridge_kernel.DISABLED} | {( "/", "h3c,bridge-profile"), ("/chosen", "bootargs")}
    for image in sorted(selected):
        dt, previous = vendor_fit.fdt_props(k[image]["data"]), vendor_fit.fdt_props(old[image]["data"])
        if set(dt) != set(previous): raise ValueError("DT topology changed")
        changed = {(p,key) for p in dt for key in set(dt[p]) | set(previous[p]) if dt[p].get(key) != previous[p].get(key)}
        if changed != expected: raise ValueError("missing or unplanned DT property change: " + image)
        if (dt["/"]["h3c,bridge-profile"] != b"H3C-BOOTBRIDGE-MULTICONFIG-V1\0" or
            dt["/chosen"]["bootargs"] != b"console=ttyMSM0,115200,n8 ro init=/sbin/init\0" or
            any(dt[p]["status"] != b"disabled\0" for p in bridge_kernel.DISABLED)):
            raise ValueError("DT bridge guard value mismatch")
        changes[image] = sorted(p+":"+key for p,key in changed)
    return changes


def audit_wrapper(raw, stock):
    identity = build.identify_official(stock)
    if len(raw) < 200 or len(raw) % 4 or raw[:6] != b"H3C_+k":
        raise ValueError("invalid OEM wrapper")
    if struct.unpack_from(">I", raw, 68)[0] != len(raw) - 64 or struct.unpack_from(">I", raw, 80)[0] != len(raw) - 160:
        raise ValueError("wrapper length mismatch")
    if sum(w[0] for w in struct.iter_unpack(">I", raw[64:])) & 0xffffffff != 0xffffffff:
        raise ValueError("wrapper checksum failure")
    if any(raw[i] != stock[i] for i in range(160) if i not in range(68, 76) and i not in range(80, 84)):
        raise ValueError("OEM product/version or opaque header fields changed")
    outer = nodes(raw[160:])
    images = [p for p in outer if p.startswith("/images/") and p.count("/") == 2]
    if len(images) != 1 or not images[0].startswith("/images/ubi-bridge-"):
        raise ValueError("outer package must contain exactly one bridge UBI component")
    component = outer[images[0]]
    if (component.get("type") != b"firmware\0" or component.get("compression") != b"none\0" or
        images[0] != "/images/ubi-bridge-" + hashlib.sha1(component["data"]).hexdigest()):
        raise ValueError("invalid bridge UBI type or content-derived name")
    if (outer.get("/configurations") != {"default": b"conf-bridge\0"} or
        outer.get("/configurations/conf-bridge") != {"firmware": images[0].split("/")[-1].encode()+b"\0"}):
        raise ValueError("outer FIT configuration does not bind the single UBI")
    expected_paths = {"/", "/images", images[0], images[0]+"/hash@1", images[0]+"/hash@2",
                      "/configurations", "/configurations/conf-bridge"}
    if set(outer) != expected_paths or outer["/images"]:
        raise ValueError("unreviewed outer FIT topology/boot component")
    if (set(component) != {"description", "data", "type", "arch", "compression"} or
        component["arch"] != b"arm\0" or
        outer[images[0]+"/hash@1"].get("algo") != b"crc32\0" or
        outer[images[0]+"/hash@2"].get("algo") != b"sha1\0"):
        raise ValueError("unreviewed outer UBI properties/digest layout")
    total = struct.unpack_from(">I",raw,164)[0]
    if len(raw) - 160 - total not in range(4) or any(raw[160+total:]):
        raise ValueError("unexpected outer FIT trailing content")
    return identity, outer, images


def public_trial_qualification(payload_report, profile):
    """Adapt catalog evidence to the common audit contract for a finite trial record."""
    binding = build.public_catalog_binding(payload_report, profile, "cc")
    record = profile.get("record")
    if not isinstance(record, str) or not record or not profile.get("class"):
        raise ValueError("public trial requires an exact class-bound first-install record")
    # Public classes have an explicit finite compile-time record selected from
    # the neutral archive descriptor. This says the runtime-gated code path is
    # present; it is not a hardware validation or an unconditional grant.
    return dict(binding, boot_chain_qualification=record,
                install_qualification_available=True)


def audit(candidate, original, trusted_payload_report, trusted_build,
          candidate_archive=None, hardware_class=None, bundle_provider_paths=False,
          fresh_candidate_descriptor=None, candidate_archive_sha256=None,
          candidate_manifest_sha256=None, candidate_descriptors_sha256=None):
    raw, stock = read(candidate), read(original)
    payload_report = json.loads(read(trusted_payload_report))
    identity, outer, image_paths = audit_wrapper(raw, stock)
    fresh_trial = fresh_candidate_descriptor is not None
    public_trial = candidate_archive is not None or hardware_class is not None or fresh_trial
    if public_trial != (candidate_archive is not None and hardware_class is not None):
        raise ValueError("public audit requires exact candidate archive and explicit hardware class")
    if fresh_trial and (not all((candidate_archive_sha256, candidate_manifest_sha256,
                                 candidate_descriptors_sha256))):
        raise ValueError("fresh candidate audit requires all three explicit archive pins")
    if bundle_provider_paths and not public_trial:
        raise ValueError("bundle provider paths require an explicit public archive and hardware class")
    public_descriptor = public_payload = public_components = public_profile = None
    public_archive_doc = None
    if public_trial:
        if fresh_trial:
            parsed = candidate_manifest.read_candidate_archive_v2(Path(candidate_archive),
                candidate_archive_sha256, candidate_manifest_sha256, candidate_descriptors_sha256)
            public_archive_doc = parsed
            matches = [d for d in parsed["candidates"] if d["candidate_id"] ==
                       fresh_candidate_descriptor.get("candidate_id")]
            if len(matches) != 1 or matches[0]["board"] != hardware_class:
                raise ValueError("fresh descriptor is absent or not bound to selected board")
            public_descriptor = dict(matches[0])
            board_mask = {"A":1,"B":2,"C":4,"D":8}[hardware_class]
            variants = ("A0", "C")
            bound = [p for p in nand_schema.runtime_profiles(variants, board_mask)
                     if p["profile_id"] == public_descriptor["nand_profile_id"] and
                     p["ecc_bits"] == public_descriptor["storage_ecc_bits"]]
            if not bound or public_descriptor["load_ecc_bits"] != public_descriptor["storage_ecc_bits"]:
                raise ValueError("fresh candidate has no exact source/runtime NAND profile")
            public_descriptor["build_profile"] = build.candidate_build_profile(
                public_descriptor, hardware_class, identity["model"], bound)
            if public_descriptor != fresh_candidate_descriptor:
                raise ValueError("fresh source report descriptor differs from independently parsed archive")
            public_payload = candidate_manifest.extract_candidate_member_v2(
                Path(candidate_archive), matches[0], "appsbl.partition.bin")
        else:
            public_payload, public_descriptor = build.public_candidate_from_archive(candidate_archive, hardware_class)
        public_components = build.official_bootchain_components(stock, identity)
        public_profile = dict(build.PUBLIC_BOARD_INPUTS[hardware_class], **{
            "class": hardware_class, "product_name": identity["model"],
            "source_sha256": identity["sha256"],
            "appsbl_sha256": public_components["appsbl_padded_sha256"]})
        if fresh_trial:
            profile_id = public_descriptor["nand_profile_id"]
            oob_mask = sum({64:1, 128:2, 256:4}[p["mtd_oob_bytes"]] for p in bound)
            nand = dict(bound[0], accepted_mtd_oob_mask=oob_mask)
            public_profile.update({"nand_profile_id": profile_id, "nand_profile": nand,
                "nand_profiles": bound, "accepted_mtd_oob_mask": oob_mask,
                "ecc": public_descriptor["storage_ecc_bits"],
                "load_ecc": public_descriptor["load_ecc_bits"],
                "candidate_id": public_descriptor["candidate_id"],
                "record": "hw-%s-%s-%s-%s" % (hardware_class.lower(), profile_id,
                    public_descriptor["load_ecc_bits"], public_descriptor["storage_ecc_bits"]),
                "model": bound[0]["model_aliases"][0], "manufacturer": bound[0]["manufacturer_id"],
                "chip": bound[0]["chip_id"], "raw_spare": bound[0]["raw_spare_bytes"],
                "mtd_oob": bound[0]["mtd_oob_bytes"], "capacity": bound[0]["capacity_mib"],
                "product_id": return_source.PRODUCTS[identity["sha256"]]})
    component = outer[image_paths[0]]
    trusted_build = Path(trusted_build)
    build_report = json.loads(read(trusted_build / "BUILD-REPORT.json"))
    if (build_report.get("candidate", {}).get("sha256") != sha(raw) or
        build_report.get("source_official_sha256") != identity["sha256"] or
        build_report.get("install_available") is not False):
        raise ValueError("trusted build identity or offline installation claim mismatch")
    with tempfile.TemporaryDirectory(prefix="nx30-bridge-audit-") as tmp:
        root = Path(tmp)
        (root / "bridge.ubi").write_bytes(component["data"])
        meta, volumes, _, _ = vendor_ubi.parse(root / "bridge.ubi")
        stock_outer = nodes(stock[160:])
        stock_ubi = next(v["data"] for p, v in stock_outer.items() if p.startswith("/images/ubi-") and "data" in v)
        (root / "stock.ubi").write_bytes(stock_ubi)
        sm, sv, _, _ = vendor_ubi.parse(root / "stock.ubi")
        expected_root_contract = root_binding_contract.derive(identity["sha256"], stock_ubi)
        try:
            built_root_contract = root_binding_contract.verify_same_source_and_built(
                expected_root_contract, (trusted_build / "bridge.ubi").read_bytes())
        except (OSError, root_binding_contract.RootBindingContractError) as exc:
            raise ValueError("built bridge UBI does not satisfy source rootfs geometry contract") from exc
        expected_root_record = {**root_binding_contract.report_record(expected_root_contract),
                                "built_ubi": built_root_contract}
        if build_report.get("root_binding_contract") != expected_root_record:
            raise ValueError("rootfs geometry contract report differs from official and built UBI bytes")
        root_contract_header = trusted_build / "bridge_root_binding_contract.generated.h"
        if (not root_contract_header.is_file() or
                root_contract_header.read_bytes() != root_binding_contract.render_header(expected_root_contract).encode("ascii")):
            raise ValueError("generated rootfs geometry contract header differs from exact official UBI")
        if meta["volume_table"] != sm["volume_table"]:
            raise ValueError("volume table differs from original")
        if any(volumes[n] != sv[n] for n in ["wifi_fw", "bt_fw"]):
            raise ValueError("ancillary firmware changed")
        if volumes["rootfs_data"]:
            raise ValueError("unexpected prepopulated writable data")
        changed = audit_kernel(volumes["kernel"], sv["kernel"])
        selected_route = public_profile["route"] if public_trial else RETURN_ROUTE
        expected_source = return_source_metadata(sv["kernel"], volumes["kernel"], identity["sha256"], selected_route)
        if build_report.get("return_source") != expected_source:
            raise ValueError("builder return_source report differs from independently recomputed actual FIT contract")
        source_header = trusted_build / "bridge_oem_return_source.h"
        if read(source_header) != return_source_header(expected_source):
            raise ValueError("generated return-source header differs from independently recomputed actual FIT contract")
        if public_trial and public_profile is not None:
            source_variants = ("A0", "C")
            source_table_mask = sum(nand_schema.TABLE_MASKS[v] for v in source_variants)
            expected_registry = return_source.c_return_contract_header(
                [expected_source], {identity["sha256"]: public_components},
                nand_schema.profiles()["profiles"], source_table_mask=source_table_mask,
                exit_policy=2)
            registry_path = trusted_build / "bridge_oem_return_registry.h"
            registry_info = build_report.get("return_source_registry")
            if (not isinstance(registry_info, dict) or
                    registry_info.get("path") != "bridge_oem_return_registry.h" or
                    registry_info.get("exit_policy") != 2 or
                    registry_info.get("source_table_mask") != source_table_mask or
                    registry_info.get("source_table_variants") != list(source_variants) or
                    read(registry_path) != expected_registry.encode("ascii") or
                    registry_info.get("sha256") != sha(expected_registry.encode("ascii"))):
                raise ValueError("provider return registry differs from independently regenerated exact source/profile contract")
        expected_base_payload = expected_patch_header = expected_patch_metadata = expected_payload_storage = None
        if fresh_trial:
            expected_base_payload, expected_patch_header, expected_patch_metadata, expected_payload_storage = \
                fresh_payload_storage_contract(candidate_archive, public_archive_doc["candidates"],
                                               public_descriptor, public_payload)
            patch_header_path = trusted_build / "bridge_payload_patch.h"
            if (read(patch_header_path) != expected_patch_header.encode("ascii") or
                    build_report.get("payload_storage") != expected_payload_storage or
                    build_report.get("payload_patch") != expected_patch_metadata):
                raise ValueError("fresh payload patch header/storage report differs from exact archive bytes")
        squash = volumes["ubi_rootfs"]
        if squash[:4] != b"hsqs":
            raise ValueError("no SquashFS rootfs")
        (root / "rootfs.squashfs").write_bytes(squash)

        def cat(path):
            p = subprocess.run(["unsquashfs", "-cat", str(root / "rootfs.squashfs"), path], capture_output=True, timeout=20)
            if p.returncode:
                raise ValueError("missing rootfs file: " + path)
            return p.stdout

        (provider_path, payload_path), provider_mode = provider_path_contract(
            hardware_class, bundle_provider_paths,
            public_descriptor["candidate_id"] if fresh_trial else None)
        if fresh_trial:
            payload_path = expected_payload_storage["path"].lstrip("/")
        binaries = {}
        binary_contents = {}
        for path in ["sbin/init", provider_path, "usr/sbin/bridge-dhcp"]:
            b = cat(path)
            if b[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", b, 18)[0] != 40:
                raise ValueError("not a 32-bit little-endian ARM ELF: " + path)
            if b != read(trusted_build / "rootfs" / path):
                raise ValueError("binary differs from trusted offline build: " + path)
            binaries[path] = {"bytes": len(b), "sha256": sha(b)}
            binary_contents[path] = b
        provider_file = trusted_build / "rootfs" / provider_path
        provider_st = provider_file.lstat()
        if (not stat.S_ISREG(provider_st.st_mode) or provider_file.is_symlink() or
                stat.S_IMODE(provider_st.st_mode) != provider_mode):
            raise ValueError("compiled provider path or mode mismatch")
        image_provider = squashfs_regular_file(root / "rootfs.squashfs", provider_path,
                                              provider_mode, "compiled provider")
        if image_provider != cat(provider_path):
            raise ValueError("provider image differs from its compiled ELF")
        payload_file = trusted_build / "rootfs" / payload_path
        payload_st = payload_file.lstat()
        if (not stat.S_ISREG(payload_st.st_mode) or payload_file.is_symlink() or
                stat.S_IMODE(payload_st.st_mode) != 0o400):
            raise ValueError("embedded candidate path or mode mismatch")
        image_payload = squashfs_regular_file(root / "rootfs.squashfs", payload_path,
                                             0o400, "embedded candidate")
        if image_payload != cat(payload_path):
            raise ValueError("payload image differs from its staged candidate")
        if b"exec /sbin/init" not in cat("etc/preinit") or b"exec /sbin/init" not in cat("init"):
            raise ValueError("init entry mismatch")
        if cat("etc/bridge/profile").strip() != b"H3C-BOOTBRIDGE-RUNTIME-v1":
            raise ValueError("bad rootfs marker")
        if cat("etc/bridge/rootfs-id").strip() != b"H3C-BOOTBRIDGE-ROOTFS-v1":
            raise ValueError("bad rootfs identity")
        physical_payload = cat(payload_path)
        if fresh_trial:
            if (physical_payload != expected_base_payload or
                    sha(physical_payload) != expected_payload_storage["sha256"] or
                    len(physical_payload) != expected_payload_storage["bytes"]):
                raise ValueError("staged fresh base differs from independently extracted EF:BA candidate")
            payload = public_payload
        else:
            payload = physical_payload
        if public_trial and bundle_provider_paths:
            if fresh_trial:
                forbidden_paths = ["etc/bridge/uboot.bin", "etc/bridge/uboot.json"]
                forbidden_paths.extend(public_provider_paths(board)[0]
                                        for board in build.PUBLIC_BOARD_INPUTS)
                if any(squashfs_has_file(root / "rootfs.squashfs", path) for path in forbidden_paths):
                    raise ValueError("fresh provider build contains a legacy unbound payload path")
                payload_meta = None
            else:
                forbidden_paths = ["usr/sbin/bridge-check", "etc/bridge/uboot.bin",
                                   "etc/bridge/uboot.json"]
                for board in build.PUBLIC_BOARD_INPUTS:
                    if board != hardware_class:
                        other_provider, other_payload = public_provider_paths(board)
                        forbidden_paths.extend((other_provider, other_payload))
                if any(squashfs_has_file(root / "rootfs.squashfs", path) for path in forbidden_paths):
                    raise ValueError("public provider build contains a legacy or foreign provider/payload path")
                for relative in forbidden_paths:
                    try:
                        (trusted_build / "rootfs" / relative).lstat()
                    except FileNotFoundError:
                        continue
                    raise ValueError("public provider build contains an unexpected source-tree path")
                payload_meta = None
        else:
            payload_meta = json.loads(cat("etc/bridge/uboot.json"))
            if public_trial:
                for board in build.PUBLIC_BOARD_INPUTS:
                    class_provider, class_payload = public_provider_paths(board)
                    if (squashfs_has_file(root / "rootfs.squashfs", class_provider) or
                            squashfs_has_file(root / "rootfs.squashfs", class_payload)):
                        raise ValueError("legacy public provider build contains a class-specific provider path")
                    for relative in (class_provider, class_payload):
                        try:
                            (trusted_build / "rootfs" / relative).lstat()
                        except FileNotFoundError:
                            continue
                        raise ValueError("legacy public provider build contains a class-specific source-tree path")
        if public_trial:
            if payload != public_payload:
                raise ValueError("embedded payload differs from explicit neutral archive board selection")
            expected_embedded = {"candidate_id": public_descriptor["candidate_id"],
                "archive_sha256": candidate_archive_sha256 if fresh_trial else build.PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256,
                "board": hardware_class, "partition_bytes": len(payload),
                "partition_sha256": sha(payload), "hardware_validation": "NOT_EXECUTED"}
            if build_report.get("embedded_uboot") != expected_embedded:
                raise ValueError("reported embedded U-Boot identity differs from actual partition bytes")
            qualification = public_trial_qualification(payload_report, public_profile)
            independently_selected = public_profile["record"]
            expected_mode = "CONTROLLED_TRIAL_RUNTIME_GATED"
            expected_payload_meta = build.public_runtime_payload_metadata(payload, public_descriptor,
                public_profile, identity, expected_source, qualification)
            if (build_report.get("hardware_class") != hardware_class or
                    build_report.get("public_candidate_descriptor") != public_descriptor or
                    build_report.get("official_bootchain_components") != public_components or
                    build_report.get("hardware_validation") != "NOT_EXECUTED"):
                raise ValueError("build report does not bind the exact public board/source inputs")
            if fresh_trial:
                expected_header = build.generated_first_install_profile_header(hardware_class, identity,
                    public_components, nand_profile=public_profile["nand_profile"],
                    candidate_id=public_descriptor["candidate_id"])
            else:
                expected_header = build.generated_first_install_profile_header(hardware_class, identity, public_components)
            if read(trusted_build / "bridge_first_install_profile.h") != expected_header.encode():
                raise ValueError("generated immutable first-install profile differs from independent source extraction")
        else:
            qualification = build.validate_payload_report(payload, payload_report)
            independently_selected = audit_d_loader_record(payload, payload_report)
            expected_mode = "D_FIRST_INSTALL_RUNTIME_GATED" if qualification["install_qualification_available"] else "READONLY_DIAGNOSTIC"
            expected_payload_meta = build.runtime_payload_metadata(payload, payload_report)
        reported_qualification = build_report.get("nand_qualification", {})
        if (build_report.get("runtime_mode") != expected_mode or
                reported_qualification.get("boot_chain_qualification") != qualification["boot_chain_qualification"] or
                qualification["boot_chain_qualification"] != independently_selected or
                reported_qualification.get("install_qualification_available") is not qualification["install_qualification_available"]):
            raise ValueError("build qualification differs from independent exact candidate/source record")
        if payload_meta is not None and payload_meta != expected_payload_meta:
            raise ValueError("embedded public metadata differs from the trusted code identity or contains private fields")
        if len(payload) != 0x140000:
            raise ValueError("embedded APPSBL candidate partition has unexpected size")
        if public_trial:
            if (sha(payload) != public_descriptor["files"]["appsbl.partition.bin"]["sha256"] or
                    public_components["sbl_sha256"] != SBL_SHA or
                    public_components["sbl_valid_bytes"] != SBL_VALID_BYTES):
                raise ValueError("public candidate or exact official SBL/APPSBL extraction mismatch")
        elif (payload_report.get("partition_sha256") != sha(payload) or
              payload_report.get("sbl_sha256") != SBL_SHA or payload_report.get("sbl_valid_bytes") != SBL_VALID_BYTES):
            raise ValueError("embedded U-Boot or fixed R012 SBL qualification mismatch")
        for source, path in [("boot.sh", "etc/bridge/boot.sh"), ("profile", "etc/bridge/profile"),
                             ("rootfs-id", "etc/bridge/rootfs-id"), ("init", "init"), ("preinit", "etc/preinit")]:
            if cat(path) != read(build.PROJECT / "runtime" / source):
                raise ValueError("runtime source mismatch: " + path)
        if b"192.168.10.1" not in cat("etc/bridge/boot.sh"):
            raise ValueError("bridge LAN address mismatch")
        installer = binary_contents[provider_path]
        source_marker = require_arm_source_marker(installer, expected_source["revision_sha256"])
        # Fresh providers compile the archive's complete source/board/format
        # candidate ID. The older report record label is descriptive metadata;
        # it is not the C first-install gate's identity.
        compiled_record = (public_descriptor["candidate_id"] if fresh_trial else
                           qualification["boot_chain_qualification"])
        if qualification["install_qualification_available"] and compiled_record.encode() not in installer:
            raise ValueError("finite first-install record missing from compiled installer")
        if public_trial:
            # The compiler folds comparisons between the two generated route
            # literals. Their source identity remains bound by the required
            # ARM revision marker and the independently checked return-source
            # header above; an unused route string need not survive in ELF.
            for marker in (hardware_class.encode(), public_profile["product_name"].encode(),
                    public_profile["appsbl_sha256"].encode(),
                    compiled_record.encode()):
                if marker not in installer:
                    raise ValueError("compiled installer lacks explicit source-bound board/profile marker")
        if (sha(payload).encode() not in installer or qualification["catalog_sha256"].encode() not in installer or
                any(profile_id.encode() not in installer for profile_id in
                    qualification["supported"] + qualification["load"] + qualification["storage"])):
            raise ValueError("compiled installer is not bound to the trusted payload/catalog/profile metadata")
        (root / "stock.squashfs").write_bytes(sv["ubi_rootfs"])
        provenance = build_report.get("rootfs", {}).get("oem_input_files", {})
        required = {"bin/busybox", "sbin/kmodloader", "lib/firmware/qca-nss0-retail.bin"} | {
            "lib/modules/4.4.60/" + name + ".ko" for name in ("qca-ssdk", "qca-nss-dp", "qca-nss-drv")}
        if not required.issubset(provenance): raise ValueError("missing source dependency closure")
        allowed = required | {"lib/"+name for name in ("libc.so", "ld-musl-arm.so.1", "libgcc_s.so.1", "libubox.so", "librpc.so")}
        if not set(provenance).issubset(allowed): raise ValueError("unreviewed OEM dependency path")
        elf_dependencies = []
        for path, info in provenance.items():
            if "symlink" in info:
                target = info["symlink"]
                if target.startswith("/") or ".." in target.split("/"): raise ValueError("unsafe OEM dependency link")
                for archive in (root / "rootfs.squashfs", root / "stock.squashfs"):
                    listing = subprocess.run(["unsquashfs", "-lls", str(archive), path], capture_output=True,text=True,check=True).stdout
                    if "squashfs-root/" + path + " -> " + info["symlink"] + "\n" not in listing:
                        raise ValueError("OEM dependency link mismatch: " + path)
            else:
                original_file = subprocess.run(["unsquashfs", "-cat", str(root / "stock.squashfs"), path],capture_output=True,check=True).stdout
                if cat(path) != original_file or sha(original_file) != info.get("sha256"):
                    raise ValueError("OEM dependency differs from this original: " + path)
                if original_file.startswith(b"\x7fELF"):
                    elf_path = root / ("dep-" + sha(path.encode())[:16]); elf_path.write_bytes(original_file)
                    dynamic = subprocess.run(["readelf", "-d", str(elf_path)], capture_output=True,text=True,check=True).stdout
                    elf_dependencies += re.findall(r"\(NEEDED\).*?\[([^]]+)\]",dynamic)
        for dependency in elf_dependencies:
            if "lib/" + dependency not in provenance: raise ValueError("OEM ELF dependency closure missing: " + dependency)
        if len(stock_ubi) > 41943040: raise ValueError("original UBI exceeds slot")
        padded = stock_ubi + b"\xff" * (41943040 - len(stock_ubi))
        (root / "stock-slot.ubi").write_bytes(padded)
        manifest_count = 0
        # These are reviewed logical namespaces, not a hardware/bootability
        # assertion. Keep this audit expectation independent of header rendering.
        pool_domain = {
            "NX30": (1, 10012, 1, "nx30-r012-archived-whole-pin-v1"),
            "RC3000": (2, 21301, 0, "rc3000-r013l01-archived-whole-pin-v1"),
            "RT3000": (3, 30015, 0, "rt3000-r015-archived-whole-pin-v1"),
            "RW3000": (4, 40012, 0, "rw3000-r012-archived-whole-pin-v1"),
        }
        product, alias, exit_policy, source_review = pool_domain[identity["model"]]
        if public_trial:
            exit_policy = 2
        pool_entries = []
        hw_d_post_base = None
        expected_source_volume_table_sha256 = None
        geometries = ((public_profile["mtd_oob"], public_profile["ecc"]),) if public_trial else ((64,4),(128,4),(128,8))
        for offset in (0x900000, 0x3100000):
            for oob,ecc in geometries:
                m = build_oem_manifest.build_manifest(root / "stock-slot.ubi", expected_ubi_sha256=sha(padded),candidate_sha256=sha(payload),observed_geometry={"physical_offset":offset,"size_bytes":41943040,"erase_bytes":131072,"page_bytes":2048,"oob_bytes":oob,"ecc_bits":ecc})
                # ARM EABI: geometry32 + digests64 + 4 padded bos_volume48.
                encoded = struct.pack("<QQIIII",offset,41943040,131072,2048,oob,ecc)+bytes.fromhex(sha(payload))+bytes.fromhex(m["volume_table_sha256"])
                for v in m["immutable"]:
                    encoded += struct.pack("<Q",v["size_bytes"])+bytes.fromhex(v["sha256"])+struct.pack("<I",v["type"])+b"\0"*4
                # bos_manifest is 288 bytes on ARM EABI. The five following
                # uint32 fields and four tail-padding bytes form a 312-byte rule.
                encoded += struct.pack("<IIIII", alias, exit_policy, product, alias, 0) + bytes(4)
                if encoded not in installer: raise ValueError("compiled six-geometry OEM pool binding absent")
                pool_entries.append(dict(manifest=m, source_package_sha256=identity["sha256"],
                    source_review_id=source_review, content_group=alias, exit_policy=exit_policy,
                    product=product, alias=alias, table_policy=0))
                if offset == 0x3100000 and oob == 64 and ecc == 4:
                    hw_d_post_base = m
                manifest_count += 1
        if public_trial:
            source_table_meta, _, _, source_tables = vendor_ubi.parse(root / "stock-slot.ubi")
            if (len(source_tables) != 2 or source_tables[0]["raw"] != source_tables[1]["raw"] or
                    source_table_meta.get("volume_table_raw_sha256") != sha(source_tables[0]["raw"])):
                raise ValueError("public autoresize source must have two exact, CRC-checked table copies")
            expected_source_volume_table_sha256 = sha(source_tables[0]["raw"])
            rootfs_row = source_tables[0]["raw"][4 * 172:5 * 172]
            if (struct.unpack_from(">I", rootfs_row, 0)[0] != 1 or rootfs_row[144] != 1 or
                    vendor_ubi.crc(rootfs_row[:168]) != struct.unpack_from(">I", rootfs_row, 168)[0]):
                raise ValueError("public policy2 source is not the exact rootfs_data reserve=1/autoresize=1 row")
            for base_entry in list(pool_entries):
                policy_entry = dict(base_entry, table_policy=2, alias=base_entry["alias"]+10000)
                geo = policy_entry["manifest"]["geometry"]
                encoded = struct.pack("<QQIIII",geo["physical_offset"],geo["size_bytes"],
                    geo["erase_bytes"],geo["page_bytes"],geo["oob_bytes"],geo["ecc_bits"])
                encoded += bytes.fromhex(sha(payload)+policy_entry["manifest"]["volume_table_sha256"])
                for volume in policy_entry["manifest"]["immutable"]:
                    encoded += struct.pack("<Q",volume["size_bytes"])+bytes.fromhex(volume["sha256"])+struct.pack("<I",volume["type"])+bytes(4)
                encoded += struct.pack("<IIIII",alias,exit_policy,product,alias+10000,2)+bytes(4)
                if encoded not in installer:
                    raise ValueError("compiled exact OEM autoresize-normalized rule absent")
                pool_entries.append(policy_entry)
                manifest_count += 1
        expected_post_report = None
        candidate_sha256 = sha(payload)
        if (not public_trial and identity["sha256"] == SOURCE_SHA and
                candidate_sha256 in HW_D_POST_ATTACH_CANDIDATES):
            if hw_d_post_base is None:
                raise ValueError("missing base manifest for observed HW-D geometry")
            source_table_meta, _, _, source_tables = vendor_ubi.parse(root / "stock-slot.ubi")
            if (source_table_meta.get("volume_table_raw_sha256") != R012_SOURCE_TABLE_SHA or
                    len(source_tables) != 2 or source_tables[0]["raw"] != source_tables[1]["raw"]):
                raise ValueError("R012 source table copies differ from pinned table")
            post_manifest, expected_post_report = audit_hw_d_post_attach_manifest(
                identity["sha256"], candidate_sha256, hw_d_post_base, source_tables[0]["raw"])
            post_entry = dict(manifest=post_manifest, source_package_sha256=SOURCE_SHA,
                source_review_id=source_review, content_group=R012_HW_D_POST_GROUP,
                exit_policy=exit_policy, product=product, alias=R012_HW_D_POST_ALIAS,
                table_policy=0)
            post_geometry = post_manifest["geometry"]
            encoded = struct.pack("<QQIIII", post_geometry["physical_offset"],
                post_geometry["size_bytes"], post_geometry["erase_bytes"],
                post_geometry["page_bytes"], post_geometry["oob_bytes"],
                post_geometry["ecc_bits"])
            encoded += bytes.fromhex(sha(payload) + post_manifest["volume_table_sha256"])
            for volume in post_manifest["immutable"]:
                encoded += struct.pack("<Q", volume["size_bytes"])
                encoded += bytes.fromhex(volume["sha256"])
                encoded += struct.pack("<I", volume["type"]) + bytes(4)
            encoded += struct.pack("<IIIII", R012_HW_D_POST_GROUP, exit_policy,
                product, R012_HW_D_POST_ALIAS, 0) + bytes(4)
            if encoded not in installer:
                raise ValueError("compiled exact HW-D R012 post-attach rule absent")
            pool_entries.append(post_entry)
            manifest_count += 1
        pool_revision = sha(json.dumps({"schema": "bootbridge-oem-pool-v2", "rules": pool_entries},
                            sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("ascii"))
        rules_report = build_report.get("oem_return_rules", {})
        if (bytes.fromhex(pool_revision) not in installer or
                rules_report.get("pool_revision_sha256") != pool_revision or
                rules_report.get("exit_policy") != exit_policy or
                rules_report.get("table_policy") != ("RAW_EXACT+OEM_AUTORESIZE_NORMALIZED" if public_trial else "RAW_EXACT") or
                rules_report.get("source_package_sha256") != identity["sha256"] or
                rules_report.get("source_volume_table_sha256") != expected_source_volume_table_sha256 or
                rules_report.get("count") != manifest_count or
                rules_report.get("hw_d_r012_post_attach") != expected_post_report):
            raise ValueError("OEM content pool revision/source/exit-policy binding differs from reviewed rules")
        files = subprocess.run(["unsquashfs", "-lls", str(root / "rootfs.squashfs")], capture_output=True, text=True, check=True).stdout
        if not any("dev/console" in line and "5," in line and line.lstrip().startswith("c") for line in files.splitlines()):
            raise ValueError("initial console character node absent")
        for forbidden in ["etc/rc.d/", "sbin/mount_root", "bin/webs", "libmwopenrpc", "bin/dnsmasq", "fw_setenv", "ubiformat", "etc/shadow"]:
            if forbidden in files:
                raise ValueError("unexpected OEM service or writer: " + forbidden)
        for name in ["index.html", "main.js", "style.css"]:
            asset = cat("www/" + name)
            if (not asset or asset != read(build.PROJECT / "web" / name) or
                asset != read(trusted_build / "rootfs/www" / name)):
                raise ValueError("web asset differs from trusted source/build: " + name)
    return {"status": "PASS_OFFLINE_PACKAGE_AUDIT", "candidate_bytes": len(raw),
            "candidate_sha256": sha(raw), "single_ubi_component": image_paths[0],
            "provider_path": "/" + provider_path, "payload_path": "/" + payload_path,
            "provider_mode": provider_mode,
            "hardware_class": hardware_class if public_trial else "D_LEGACY",
            "hardware_validation": "NOT_EXECUTED" if public_trial else "LEGACY_D_RULE_ONLY",
            "no_outer_bootloader_components": True, "original_kernel_bytes_preserved": True,
            "volume_table_preserved": True, "dt_changes": changed, "source_official_identity": identity,
            "oem_manifest_count": manifest_count, "oem_pool_revision_sha256": pool_revision,
            "return_source": expected_source, "return_source_header_sha256": sha(return_source_header(expected_source)),
            "arm_return_source_marker": source_marker,
            "oem_exit_policy": exit_policy, "install_available": False,
            "production_chain_provider": qualification["boot_chain_qualification"],
            "runtime_device_qualification": "NOT_EXECUTED",
            "arm_binaries": binaries, "initial_console_node_present": True,
            "embedded_uboot_bytes": len(payload), "embedded_uboot_sha256": sha(payload),
            "root_binding_contract": expected_root_record,
            "nand_catalog_sha256": qualification["catalog_sha256"],
            "supported_nand_profile_ids": qualification["supported"],
            "appsbl_load_profiles": qualification["load"],
            "appsbl_storage_profiles": qualification["storage"],
            "maintenance_ip": "192.168.10.1", "backup_download_required": False,
            "device_operations": False, "OEM_WEB_ACCEPTANCE": "NOT_EXECUTED", "HARDWARE_BOOT": "NOT_EXECUTED"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--payload-report", type=Path, required=True,
                        help="trusted external payload report whose catalog/profile fields are checked")
    parser.add_argument("--candidate-archive", type=Path,
                        help="pinned neutral ABCD archive; requires --hardware-class")
    parser.add_argument("--hardware-class", choices=tuple(build.PUBLIC_BOARD_INPUTS),
                        help="explicit compiled candidate class A/B/C/D")
    parser.add_argument("--trusted-build", type=Path, required=True, help="reviewed frozen offline build directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("use a fresh report path")
    if bool(args.candidate_archive) != bool(args.hardware_class):
        parser.error("--candidate-archive and --hardware-class must be supplied together")
    result = audit(args.candidate, args.official, args.payload_report, args.trusted_build,
                   args.candidate_archive, args.hardware_class)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(result, ensure_ascii=False))
