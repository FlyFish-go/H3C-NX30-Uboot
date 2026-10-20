#!/usr/bin/env python3
"""Source-bound rootfs UBI geometry and generated C contract."""

import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile

import vendor_ubi

PEB_BYTES = 131072
VID_HEADER_BYTES = 64
ROOTFS_VOLUME = "ubi_rootfs"
HEADER_NAME = "bridge_root_binding_contract.generated.h"
COMPILE_FLAG = '-DBI_ROOT_BINDING_CONTRACT_HEADER="' + HEADER_NAME + '"'


class RootBindingContractError(ValueError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _parse_bytes(raw, label):
    if not isinstance(raw, bytes) or not raw:
        raise RootBindingContractError(label + " must be nonempty bytes")
    with tempfile.TemporaryDirectory(prefix="root-binding-ubi-") as tmp:
        path = Path(tmp) / "input.ubi"
        path.write_bytes(raw)
        try:
            meta, volumes, _, _ = vendor_ubi.parse(path)
        except Exception as exc:
            raise RootBindingContractError(label + " failed UBI validation") from exc
    return meta, volumes


def derive(official_sha256, ubi_bytes):
    """Derive source rootfs geometry from the official UBI volume table."""
    if (not isinstance(official_sha256, str) or len(official_sha256) != 64 or
            any(ch not in "0123456789abcdef" for ch in official_sha256)):
        raise RootBindingContractError("official source SHA-256 is malformed")
    meta, _ = _parse_bytes(ubi_bytes, "official UBI")
    matches = [row for row in meta["volume_table"] if row.get("name") == ROOTFS_VOLUME]
    if len(matches) != 1:
        raise RootBindingContractError("official UBI must have exactly one ubi_rootfs volume")
    row = matches[0]
    peb = PEB_BYTES
    ec_rows = [r for r in meta["rows"] if r.get("kind") == "EC_VID"]
    geometries = {(r.get("vid_offset"), r.get("data_offset")) for r in ec_rows}
    if geometries != {(2048, 4096)} or len(ubi_bytes) % peb:
        raise RootBindingContractError("official UBI has unreviewed PEB/VID/data geometry")
    vid_offset, data_offset = next(iter(geometries))
    usable = peb - data_offset
    write_bytes = data_offset - vid_offset
    reserved = row.get("reserved_pebs")
    capacity = row.get("capacity")
    if (type(reserved) is not int or reserved <= 0 or
            capacity != reserved * usable or row.get("type") != "dynamic"):
        raise RootBindingContractError("official ubi_rootfs reserve/capacity/type is invalid")
    table_sha = meta.get("volume_table_raw_sha256")
    if not isinstance(table_sha, str) or len(table_sha) != 64:
        raise RootBindingContractError("official UBI has no exact raw volume-table digest")
    contract = {
        "schema": "bootbridge-root-binding-contract-v1",
        "source_official_sha256": official_sha256,
        "source_ubi_sha256": sha256(ubi_bytes),
        "source_volume_table_sha256": table_sha,
        "volume_bytes": capacity,
        "usable_leb_bytes": usable,
        "reserved_ebs": reserved,
        "mtd_write_bytes": write_bytes,
    }
    return contract


def contract_sha256(contract):
    canonical = json.dumps(contract, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=True).encode("ascii")
    return sha256(canonical)


def render_header(contract):
    required = {"schema", "source_official_sha256", "source_ubi_sha256",
                "source_volume_table_sha256", "volume_bytes", "usable_leb_bytes",
                "reserved_ebs", "mtd_write_bytes"}
    if not isinstance(contract, dict) or set(contract) != required or contract.get("schema") != "bootbridge-root-binding-contract-v1":
        raise RootBindingContractError("root binding contract shape mismatch")
    for key in ("volume_bytes", "usable_leb_bytes", "reserved_ebs", "mtd_write_bytes"):
        if type(contract[key]) is not int or contract[key] <= 0:
            raise RootBindingContractError("root binding contract integer is invalid: " + key)
    if contract["volume_bytes"] != contract["usable_leb_bytes"] * contract["reserved_ebs"]:
        raise RootBindingContractError("root binding capacity does not match reserved LEB count")
    digest = contract_sha256(contract)
    text = "\n".join((
        "#ifndef BOOTBRIDGE_ROOT_BINDING_CONTRACT_GENERATED_H",
        "#define BOOTBRIDGE_ROOT_BINDING_CONTRACT_GENERATED_H",
        "#include <stdint.h>",
        "#define BI_ROOT_BINDING_CONTRACT { UINT64_C(%d), UINT64_C(%d), UINT64_C(%d), UINT64_C(%d) }" % (
            contract["volume_bytes"], contract["usable_leb_bytes"],
            contract["reserved_ebs"], contract["mtd_write_bytes"]),
        '#define BI_ROOT_BINDING_CONTRACT_SHA256 "' + digest + '"',
        "#endif",
        ""))
    return text


def report_record(contract, header_path="bridge_root_binding_contract.generated.h"):
    header = render_header(contract).encode("ascii")
    return {"path": header_path, "sha256": sha256(header), "header_bytes": len(header),
            "contract_sha256": contract_sha256(contract), "contract": dict(contract)}


def verify_same_source_and_built(source_contract, built_ubi_bytes):
    """Require the actual generated UBI rootfs row to match the bound source contract."""
    meta, _ = _parse_bytes(built_ubi_bytes, "built bridge UBI")
    rows = [row for row in meta["volume_table"] if row.get("name") == ROOTFS_VOLUME]
    if len(rows) != 1:
        raise RootBindingContractError("built UBI must have exactly one ubi_rootfs row")
    row = rows[0]
    if (row.get("capacity") != source_contract["volume_bytes"] or
            row.get("reserved_pebs") != source_contract["reserved_ebs"] or
            meta.get("volume_table_raw_sha256") != source_contract["source_volume_table_sha256"]):
        raise RootBindingContractError("built bridge UBI rootfs geometry/table differs from source contract")
    return {"built_ubi_sha256": sha256(built_ubi_bytes),
            "built_volume_table_sha256": meta["volume_table_raw_sha256"],
            "volume_bytes": row["capacity"], "reserved_ebs": row["reserved_pebs"]}


def _regular(path, label):
    path = Path(path)
    try:
        st = path.lstat()
    except OSError as exc:
        raise RootBindingContractError(label + " is missing") from exc
    if not stat.S_ISREG(st.st_mode) or path.is_symlink():
        raise RootBindingContractError(label + " must be a regular non-symlink file")
    return path


def _tree_digest(root, label):
    """Hash a directory without trusting timestamps or following external links."""
    root = Path(root).resolve(strict=True)
    rows = []
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        rel = path.relative_to(root).as_posix()
        st = path.lstat()
        if stat.S_ISDIR(st.st_mode):
            rows.append({"path": rel, "kind": "directory", "mode": st.st_mode & 0o777})
        elif stat.S_ISREG(st.st_mode):
            data = path.read_bytes()
            rows.append({"path": rel, "kind": "file", "mode": st.st_mode & 0o777,
                         "bytes": len(data), "sha256": sha256(data)})
        elif stat.S_ISLNK(st.st_mode):
            target = os.readlink(path)
            resolved = (path.parent / target).resolve(strict=True)
            try:
                resolved.relative_to(root)
            except ValueError as exc:
                raise RootBindingContractError(label + " contains an external symlink") from exc
            if not resolved.is_file() and not resolved.is_dir():
                raise RootBindingContractError(label + " contains a dangling symlink")
            rows.append({"path": rel, "kind": "symlink", "target": target})
        else:
            raise RootBindingContractError(label + " contains a special file")
    canonical = json.dumps(rows, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=True).encode("ascii")
    return sha256(canonical), len(rows)


def _gate_tools_digest(root):
    """Pin only immutable guest inputs, excluding disposable logs/capsules."""
    root = Path(root).resolve(strict=True)
    names = ("build_guest_capsule.sh", "build_guest_modules.sh", "run_guest_gate.sh", "build_guest_probe.sh",
             "verify_guest_module.sh",
             "root_binding_guest_probe.c", "guest_mount_root.c", "guest-init",
             "guest-tools/usr/sbin/nandwrite", "guest-tools/usr/sbin/ubiattach")
    rows = []
    for name in names:
        path = _regular(root / name, "gate tool " + name)
        data = path.read_bytes()
        rows.append({"path": name, "bytes": len(data), "sha256": sha256(data),
                     "mode": path.stat().st_mode & 0o777})
    source_names = ("gluebi-module/Makefile", "gluebi-module/gluebi.c",
                    "gluebi-module/r3_mtd_slots.c", "gluebi-module/ubi-media.h",
                    "mtdblock-compat/Makefile", "mtdblock-compat/mtd_blkdevs.c",
                    "mtdblock-compat/mtdblock.c", "mtdblock-compat/mtdcore.h",
                    "mtdblock-compat/SOURCE-PROVENANCE.json")
    for name in source_names:
        path = _regular(root / name, "guest module source " + name)
        data = path.read_bytes()
        rows.append({"path": name, "bytes": len(data), "sha256": sha256(data),
                     "mode": path.stat().st_mode & 0o777})
    modules_digest, modules_entries = _tree_digest(root / "guest-modules", "guest-modules")
    rows.append({"path": "guest-modules", "sha256": modules_digest,
                 "entries": modules_entries})
    canonical = json.dumps(rows, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=True).encode("ascii")
    return sha256(canonical)


def read_gate_config(config_path):
    """Read exact local inputs for the live QEMU/sysfs gate; no defaults."""
    path = _regular(config_path, "root binding gate config")
    try:
        raw = path.read_bytes()
        config = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RootBindingContractError("root binding gate config is invalid") from exc
    keys = {"schema", "kernel_image", "base_initrd", "kernel_release", "module_root",
            "qemu_bin", "qemu_data_dir", "gate_tools_dir"}
    if not isinstance(config, dict) or set(config) != keys or config.get("schema") != "r3-live-gluebi-qemu-config-v1":
        raise RootBindingContractError("root binding gate config schema mismatch")
    if (not isinstance(config["kernel_release"], str) or not config["kernel_release"] or
            any(ord(ch) < 0x21 for ch in config["kernel_release"])):
        raise RootBindingContractError("root binding gate kernel release is invalid")
    for key in ("kernel_image", "base_initrd", "qemu_bin"):
        config[key] = str(_regular(config[key], key).resolve(strict=True))
    for key in ("module_root", "gate_tools_dir"):
        directory = Path(config[key])
        if directory.is_symlink() or not directory.is_dir():
            raise RootBindingContractError(key + " must be a real directory")
        config[key] = str(directory.resolve(strict=True))
    if config["qemu_data_dir"]:
        directory = Path(config["qemu_data_dir"])
        if directory.is_symlink() or not directory.is_dir():
            raise RootBindingContractError("qemu_data_dir must be empty or a real directory")
        config["qemu_data_dir"] = str(directory.resolve(strict=True))
    else:
        config["qemu_data_dir"] = ""
    for name in ("build_guest_capsule.sh", "build_guest_modules.sh", "run_guest_gate.sh", "build_guest_probe.sh",
                 "verify_guest_module.sh",
                 "guest-init", "guest-tools/usr/sbin/nandwrite", "guest-tools/usr/sbin/ubiattach",
                 "guest_mount_root.c", "root_binding_guest_probe.c"):
        candidate = Path(config["gate_tools_dir"]) / name
        _regular(candidate, "gate tool " + name)
    guest_modules = Path(config["gate_tools_dir"]) / "guest-modules"
    module_manifest_path = _regular(guest_modules / "modules.json", "guest module manifest")
    try:
        module_manifest_raw = module_manifest_path.read_bytes()
        module_manifest = json.loads(module_manifest_raw.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RootBindingContractError("guest module manifest is invalid") from exc
    module_names = ("gluebi_guest.ko", "r3_mtd_slots.ko", "mtd_blkdevs_guest.ko", "mtdblock_guest.ko")
    module_sources = ("gluebi-module/Makefile", "gluebi.c", "r3_mtd_slots.c",
        "ubi-media.h", "mtdblock-compat/Makefile", "mtdblock-compat/mtd_blkdevs.c",
        "mtdblock-compat/mtdblock.c", "mtdblock-compat/mtdcore.h", "mtdblock-compat/SOURCE-PROVENANCE.json")
    valid_hash = lambda value: isinstance(value, str) and len(value) == 64 and all(
        ch in "0123456789abcdef" for ch in value)
    if (not isinstance(module_manifest, dict) or
            set(module_manifest) != {"schema", "status", "kernel_release", "kernel_build_inputs",
                                     "modules", "sources", "variance"} or
            module_manifest.get("schema") != "r3-guest-module-set-v1" or
            module_manifest.get("status") != "BUILT" or
            module_manifest.get("kernel_release") != config["kernel_release"] or
            module_manifest.get("variance") != "guest-only-mtdblock-registration-compatibility-for-MTD_UBIVOLUME; production validator unchanged"):
        raise RootBindingContractError("guest module manifest status/kernel/variance is invalid")
    build_inputs = module_manifest.get("kernel_build_inputs")
    if (not isinstance(build_inputs, dict) or set(build_inputs) != {"config_sha256", "module_symvers_sha256"} or
            any(not valid_hash(value) for value in build_inputs.values())):
        raise RootBindingContractError("guest module build inputs are not source-pinned")
    module_rows = module_manifest.get("modules")
    if not isinstance(module_rows, dict) or set(module_rows) != set(module_names):
        raise RootBindingContractError("guest module manifest has an incomplete module set")
    for name in module_names:
        row = module_rows[name]
        data = _regular(guest_modules / name, "guest module " + name).read_bytes()
        if (not isinstance(row, dict) or set(row) != {"sha256", "bytes"} or
                row.get("sha256") != sha256(data) or row.get("bytes") != len(data)):
            raise RootBindingContractError("guest module does not match its immutable manifest: " + name)
    source_rows = module_manifest.get("sources")
    if not isinstance(source_rows, dict) or set(source_rows) != set(module_sources):
        raise RootBindingContractError("guest module manifest source set is incomplete")
    for name in module_sources:
        row = source_rows[name]
        local_name = name if name.startswith("mtdblock-compat/") else (
            "gluebi-module/Makefile" if name == "gluebi-module/Makefile" else
            "gluebi-module/" + name)
        data = _regular(Path(config["gate_tools_dir"]) / local_name,
                        "guest module source " + name).read_bytes()
        if (not isinstance(row, dict) or set(row) != {"sha256", "bytes"} or
                row.get("sha256") != sha256(data) or row.get("bytes") != len(data)):
            raise RootBindingContractError("guest module source differs from its manifest: " + name)
    config["input_pins"] = {
        "kernel_image_sha256": sha256(Path(config["kernel_image"]).read_bytes()),
        "base_initrd_sha256": sha256(Path(config["base_initrd"]).read_bytes()),
        "qemu_sha256": sha256(Path(config["qemu_bin"]).read_bytes()),
        "module_tree_sha256": _tree_digest(config["module_root"], "module_root")[0],
        "guest_module_manifest_sha256": sha256(module_manifest_raw),
        "guest_module_tree_sha256": _tree_digest(guest_modules, "guest_modules")[0],
        "gate_tools_tree_sha256": _gate_tools_digest(config["gate_tools_dir"]),
    }
    config["module_manifest"] = module_manifest
    config["config_path"] = str(path.resolve(strict=True))
    config["config_sha256"] = sha256(raw)
    return config


def gate_projection(receipt):
    """Stable result fields for a separately repeated independent gate run."""
    if not isinstance(receipt, dict):
        raise RootBindingContractError("QEMU gate receipt is not an object")
    keys = ("schema", "status", "reason", "model", "provider_executables_run",
            "synthetic_sysfs_or_mount_evidence", "qemu", "guest", "input_bridge_ubi",
            "ubi_layout", "generated_contract_header", "production_probe_sha256", "observed_gate_line",
            "isolation_command_policy")
    return {key: receipt.get(key) for key in keys}


def gate_binding_key(result):
    """Fields that must remain identical when audit repeats the guest gate."""
    if not isinstance(result, dict) or result.get("status") != "PASS":
        raise RootBindingContractError("recorded root binding gate is not PASS")
    capsule = result.get("capsule")
    receipt = result.get("receipt")
    if not isinstance(capsule, dict) or not isinstance(receipt, dict):
        raise RootBindingContractError("root binding gate receipts are incomplete")
    return {"schema": result.get("schema"), "status": result.get("status"),
            "model": result.get("model"), "config": result.get("config"),
            "scripts": result.get("scripts"),
            "capsule_initrd_sha256": capsule.get("initrd_sha256"),
            "capsule_probe_sha256": capsule.get("probe_sha256"),
            "projection": result.get("projection")}


def run_live_gate(config_path, model, contract, contract_header_path, bridge_ubi_path,
                  output_dir, relative_to=None):
    """Build isolated kernel capsule and run actual GlueBI/sysfs validation once."""
    config = read_gate_config(config_path)
    model = str(model)
    if not model or any(not (ch.isalnum() or ch in "._-") for ch in model):
        raise RootBindingContractError("QEMU gate model label is invalid")
    header_path = _regular(contract_header_path, "generated root binding contract header").resolve(strict=True)
    ubi_path = _regular(bridge_ubi_path, "final bridge UBI").resolve(strict=True)
    expected_header = render_header(contract).encode("ascii")
    if header_path.read_bytes() != expected_header:
        raise RootBindingContractError("QEMU gate header differs from source-derived contract")
    out = Path(output_dir)
    if out.exists() or out.is_symlink():
        raise RootBindingContractError("QEMU gate output directory must be new")
    out.mkdir(parents=True, mode=0o700)
    capsule_dir = out / "capsule"
    gate_dir = out / "result"
    scripts = Path(config["gate_tools_dir"])
    capsule_script = _regular(scripts / "build_guest_capsule.sh", "capsule builder")
    gate_script = _regular(scripts / "run_guest_gate.sh", "live QEMU gate")
    script_hashes = {"build_guest_capsule_sha256": sha256(capsule_script.read_bytes()),
                     "run_guest_gate_sha256": sha256(gate_script.read_bytes())}
    qemu_path = Path(config["qemu_bin"])
    source_dir = Path(__file__).resolve().parents[1] / "src"
    binding_c = _regular(source_dir / "bridge_root_binding.c", "production root-binding source")
    binding_h = _regular(source_dir / "bridge_root_binding.h", "production root-binding header")
    input_pins = dict(config["input_pins"])
    input_pins.update({"bridge_ubi_sha256": sha256(ubi_path.read_bytes()),
        "contract_header_sha256": sha256(expected_header),
        "production_binding_c_sha256": sha256(binding_c.read_bytes()),
        "production_binding_h_sha256": sha256(binding_h.read_bytes())})
    cmd = ["bash", str(capsule_script), config["kernel_image"], config["base_initrd"],
           config["kernel_release"], config["module_root"],
           str(source_dir), str(header_path),
           str(ubi_path), str(capsule_dir)]
    try:
        subprocess.run(cmd, check=True, capture_output=True, timeout=600)
    except (OSError, subprocess.SubprocessError) as exc:
        detail = getattr(exc, "stderr", b"")
        if isinstance(detail, bytes):
            detail = detail.decode("utf-8", "replace")
        raise RootBindingContractError("QEMU guest capsule build failed: " + str(detail)[-2000:]) from exc
    capsule_path = capsule_dir / "capsule.json"
    try:
        capsule_raw = _regular(capsule_path, "capsule receipt").read_bytes()
        capsule = json.loads(capsule_raw.decode("utf-8"))
    except (RootBindingContractError, UnicodeError, json.JSONDecodeError) as exc:
        raise RootBindingContractError("capsule receipt is invalid") from exc
    if (capsule.get("schema") != "r3-live-gluebi-qemu-capsule-v1" or
            capsule.get("status") != "PREPARED" or
            capsule.get("bridge_ubi") != {"path": str(ubi_path), "sha256": sha256(ubi_path.read_bytes()),
                                           "bytes": ubi_path.stat().st_size} or
            capsule.get("generated_contract_header") != {"path": str(header_path),
                                                           "sha256": sha256(expected_header)} or
            capsule.get("kernel", {}).get("sha256") != input_pins["kernel_image_sha256"] or
            capsule.get("base_initrd", {}).get("sha256") != input_pins["base_initrd_sha256"]):
        raise RootBindingContractError("capsule receipt does not bind exact gate inputs")
    module_manifest = config["module_manifest"]
    if capsule.get("guest_modules") != module_manifest:
        raise RootBindingContractError("capsule did not embed the exact pinned guest module manifest")
    transition = capsule.get("guest_transition_source")
    transition_path = Path(config["gate_tools_dir"]) / "guest_mount_root.c"
    if (not isinstance(transition, dict) or
            transition != {"path": str(transition_path.resolve(strict=True)),
                           "sha256": sha256(transition_path.read_bytes())}):
        raise RootBindingContractError("capsule transition helper is not the pinned source")
    built_meta, _ = _parse_bytes(ubi_path.read_bytes(), "final bridge UBI")
    root_rows = [row for row in built_meta["volume_table"] if row.get("name") == ROOTFS_VOLUME]
    layout = capsule.get("ubi_layout")
    if len(root_rows) != 1 or not isinstance(layout, dict) or set(layout) != {
            "peb_bytes", "vid_header_offset", "data_offset", "root_volume_id", "root_volume_name",
            "root_volume_capacity", "root_volume_reserved_ebs", "source_contract_matches_built_image"}:
        raise RootBindingContractError("capsule UBI layout receipt is incomplete")
    root_row = root_rows[0]
    if (layout != {"peb_bytes": PEB_BYTES, "vid_header_offset": contract["mtd_write_bytes"],
                   "data_offset": PEB_BYTES - contract["usable_leb_bytes"],
                   "root_volume_id": root_row.get("id"), "root_volume_name": ROOTFS_VOLUME,
                   "root_volume_capacity": contract["volume_bytes"],
                   "root_volume_reserved_ebs": contract["reserved_ebs"],
                   "source_contract_matches_built_image": True} or
            root_row.get("capacity") != contract["volume_bytes"] or
            root_row.get("reserved_pebs") != contract["reserved_ebs"]):
        raise RootBindingContractError("capsule layout differs from the actual final UBI contract")
    gate_args = ["bash", str(gate_script), config["qemu_bin"], str(capsule_dir), model, str(gate_dir)]
    if config["qemu_data_dir"]:
        gate_args.append(config["qemu_data_dir"])
    try:
        subprocess.run(gate_args, check=False, capture_output=True, timeout=240)
    except (OSError, subprocess.SubprocessError) as exc:
        raise RootBindingContractError("live QEMU root binding gate could not run") from exc
    receipt_path = gate_dir / (model + ".gate.json")
    try:
        receipt_raw = _regular(receipt_path, "QEMU gate receipt").read_bytes()
        receipt = json.loads(receipt_raw.decode("utf-8"))
    except (RootBindingContractError, UnicodeError, json.JSONDecodeError) as exc:
        raise RootBindingContractError("QEMU gate receipt is invalid") from exc
    qemu = receipt.get("qemu", {})
    isolation = receipt.get("isolation_command_policy", {})
    expected_capsule_header = capsule["generated_contract_header"]
    if (receipt.get("schema") != "r3-live-gluebi-qemu-gate-v1" or
            receipt.get("status") != "PASS" or receipt.get("model") != model or
            receipt.get("provider_executables_run") is not False or
            receipt.get("synthetic_sysfs_or_mount_evidence") is not False or
            receipt.get("input_bridge_ubi") != capsule["bridge_ubi"] or
            receipt.get("generated_contract_header") != expected_capsule_header or
            receipt.get("production_probe_sha256") != capsule.get("production_probe_sha256") or
            not isinstance(receipt.get("observed_gate_line"), str) or
            "R3_GATE status=PASS" not in receipt["observed_gate_line"] or
            qemu.get("returncode") != 0 or qemu.get("acceleration") != "tcg" or qemu.get("network") != "none" or
            qemu.get("host_block_devices") != [] or qemu.get("host_mtd_devices") != [] or
            qemu.get("shared_directories") != [] or qemu.get("usb_passthrough") is not False or
            any(isolation.get(k) is not True for k in
                ("nodefaults", "nic_none", "no_drive", "no_device_forwarding", "no_virtfs",
                 "no_host_mtd_or_block_passthrough"))):
        raise RootBindingContractError("live QEMU sysfs/GlueBI root binding gate failed")
    log = receipt.get("qemu_log")
    if not isinstance(log, dict):
        raise RootBindingContractError("QEMU gate did not retain its log receipt")
    log_path = _regular(log.get("path", ""), "QEMU log")
    if log.get("sha256") != sha256(log_path.read_bytes()):
        raise RootBindingContractError("QEMU gate log digest mismatch")
    guest_modules = Path(config["gate_tools_dir"]) / "guest-modules"
    current_inputs = {
        "kernel_image_sha256": sha256(Path(config["kernel_image"]).read_bytes()),
        "base_initrd_sha256": sha256(Path(config["base_initrd"]).read_bytes()),
        "qemu_sha256": sha256(qemu_path.read_bytes()),
        "module_tree_sha256": _tree_digest(config["module_root"], "module_root")[0],
        "guest_module_manifest_sha256": sha256((guest_modules / "modules.json").read_bytes()),
        "guest_module_tree_sha256": _tree_digest(guest_modules, "guest_modules")[0],
        "gate_tools_tree_sha256": _gate_tools_digest(config["gate_tools_dir"]),
        "bridge_ubi_sha256": sha256(ubi_path.read_bytes()),
        "contract_header_sha256": sha256(header_path.read_bytes()),
        "production_binding_c_sha256": sha256(binding_c.read_bytes()),
        "production_binding_h_sha256": sha256(binding_h.read_bytes()),
    }
    if current_inputs != input_pins:
        raise RootBindingContractError("live gate input changed while capsule or guest was running")
    if (sha256(capsule_script.read_bytes()) != script_hashes["build_guest_capsule_sha256"] or
            sha256(gate_script.read_bytes()) != script_hashes["run_guest_gate_sha256"]):
        raise RootBindingContractError("live gate script changed during execution")

    def saved_path(path):
        return Path(path).relative_to(relative_to).as_posix() if relative_to else str(path)

    return {
        "schema": "bootbridge-live-gluebi-qemu-gate-v1", "status": "PASS", "model": model,
        "config": {"path": config["config_path"], "sha256": config["config_sha256"],
                   **input_pins}, "scripts": script_hashes,
        "capsule": {"path": saved_path(capsule_path), "sha256": sha256(capsule_raw),
                    "initrd_sha256": capsule["capsule_initrd_sha256"],
                    "probe_sha256": capsule["production_probe_sha256"], "document": capsule},
        "receipt": {"path": saved_path(receipt_path), "sha256": sha256(receipt_raw),
                    "document": receipt},
        "projection": gate_projection(receipt),
    }
