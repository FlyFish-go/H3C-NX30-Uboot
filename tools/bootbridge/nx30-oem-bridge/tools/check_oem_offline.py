#!/usr/bin/env python3
"""Run bounded, offline NX30 R012 OEM FIT and wrapper checks in QEMU/bwrap.

This harness creates only synthetic MTD/boot-info trees and mock flash tools.
It does not contact a device, run a web server, or expose host /sys, /proc,
/dev, or networking to the guest processes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
from typing import Any


FIT_MAGIC = b"\xd0\x0d\xfe\xed"
WRAPPER_MARKER = b"H3C_+k"
MAX_CANDIDATE_BYTES = 512 * 1024 * 1024

PLATFORM_FILES = (
    "bin/busybox",
    "usr/bin/dumpimage",
    "lib/libc.so",
    "lib/libgcc_s.so.1",
    "lib/librpc.so",
    "lib/functions.sh",
    "lib/ipq806x.sh",
    "lib/upgrade/platform.sh",
    "lib/upgrade/common.sh",
)

APPLET_LINKS = (
    "ash", "awk", "basename", "cat", "cp", "cut", "dd", "find", "sh",
    "grep", "hexdump", "ls", "mkdir", "mv", "rm", "sed", "stat", "strings",
    "sync", "touch", "wc",
)

# Pinned from the extracted original R012 image used for this verifier. These
# prevent accidentally running against a nearby R011/R012 reference root.
OEM_INPUT_SHA256 = {
    "bin/busybox": "32652d80c7e7c040790fc7f6bbf085c4d22de01106123fed437d3f7d06f68b5b",
    "usr/bin/dumpimage": "a8e87208763061d38d4146424eeb8cd08f3ea8d8f7c5944d8f191d5f95a44ee0",
    "lib/libmwopenrpc.so": "a973f416540c3a6dd1d9fbc131bd1e4bbdfcac280dbd224d0a98d32976274e93",
    "lib/libc.so": "3be96d2f5917524ff67e5cd15b525ccdc8d84ac045fb9cb4db40764a9369d6d5",
    "lib/libgcc_s.so.1": "e1f691c7621f727a075ed825cb639c28790d53273c33d584f8c33a0a4b6de795",
    "lib/librpc.so": "dfb65a692d17a8e5b44d6e39de2be9b9b9aa70977d51c89814ea92c94ef41e6a",
    "lib/functions.sh": "b3baf7182cba862e590a5f80d822fdd44f7804d9ba0ceef204743e8dd723e9dc",
    "lib/ipq806x.sh": "3527f22859c6aa0b726db9bb1edf6b9770673eb0415c80ebcaa476b48995001b",
    "lib/upgrade/platform.sh": "ad429d1286f32bdf06eb749f279953001ba5446f94f8bbbe910171f44e8ec6c1",
    "lib/upgrade/common.sh": "f01c2e2ec72caa14c7156fa143d4d16ffcc64005697909080f21d6e8c9d9e580",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_file(root: Path, rel: str, data: bytes | str, mode: int = 0o600) -> Path:
    path = root / rel.lstrip("/")
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    path.write_bytes(data)
    path.chmod(mode)
    return path


def link_file(root: Path, rel: str, target: str) -> None:
    path = root / rel.lstrip("/")
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or path.exists():
        path.unlink()
    path.symlink_to(target)


def copy_required(source_root: Path, target_root: Path, rel: str) -> None:
    source = source_root / rel
    if not source.is_file():
        raise FileNotFoundError(f"required OEM input is missing: {source}")
    target = target_root / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target, follow_symlinks=True)


def verify_oem_inputs(oem_root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for rel, expected in OEM_INPUT_SHA256.items():
        path = oem_root / rel
        if not path.is_file():
            raise FileNotFoundError(f"pinned original R012 input is missing: {path}")
        actual = sha256(path.read_bytes())
        rows.append({"path": str(path), "sha256": actual, "expected_sha256": expected})
        if actual != expected:
            raise ValueError(f"OEM input hash mismatch for {path}: expected {expected}, got {actual}")
    return rows


def find_oem_file(oem_root: Path, names: tuple[str, ...]) -> Path:
    for rel in names:
        path = oem_root / rel
        if path.is_file():
            return path
    raise FileNotFoundError(f"none of the expected OEM files exist: {names}")


def fdt_nodes(blob: bytes) -> dict[str, dict[str, bytes]]:
    if len(blob) < 40 or blob[:4] != FIT_MAGIC:
        raise ValueError("not an FDT/FIT blob")
    total, struct_off, strings_off = struct.unpack_from(">III", blob, 4)
    strings_size = struct.unpack_from(">I", blob, 32)[0]
    struct_size = struct.unpack_from(">I", blob, 36)[0]
    if total != len(blob):
        raise ValueError(f"FDT totalsize {total} differs from file size {len(blob)}")
    if struct_off + struct_size > total or strings_off + strings_size > total:
        raise ValueError("FDT structure/string table exceeds FIT bounds")
    structure = blob[struct_off:struct_off + struct_size]
    strings = blob[strings_off:strings_off + strings_size]
    pos = 0
    stack: list[str] = []
    nodes: dict[str, dict[str, bytes]] = {}
    while pos + 4 <= len(structure):
        token = struct.unpack_from(">I", structure, pos)[0]
        pos += 4
        if token == 1:  # FDT_BEGIN_NODE
            end = structure.find(b"\0", pos)
            if end < 0:
                raise ValueError("unterminated FDT node name")
            stack.append(structure[pos:end].decode("ascii", "replace"))
            pos = (end + 4) & ~3
            node_path = "/" + "/".join(part for part in stack if part)
            nodes[node_path] = {}
        elif token == 2:  # FDT_END_NODE
            if not stack:
                raise ValueError("unbalanced FDT end-node token")
            stack.pop()
        elif token == 3:  # FDT_PROP
            if pos + 8 > len(structure):
                raise ValueError("truncated FDT property header")
            size, name_off = struct.unpack_from(">II", structure, pos)
            pos += 8
            if name_off >= len(strings) or pos + size > len(structure):
                raise ValueError("FDT property exceeds bounds")
            name_end = strings.find(b"\0", name_off)
            if name_end < 0:
                raise ValueError("unterminated FDT property name")
            prop_name = strings[name_off:name_end].decode("ascii", "replace")
            node_path = "/" + "/".join(part for part in stack if part)
            nodes.setdefault(node_path, {})[prop_name] = structure[pos:pos + size]
            pos = (pos + size + 3) & ~3
        elif token == 4:  # FDT_NOP
            continue
        elif token == 9:  # FDT_END
            break
        else:
            raise ValueError(f"unknown FDT token {token}")
    return nodes


def fit_blob(blob: bytes) -> bytes:
    """Return the exact FDT payload, allowing only zero alignment after totalsize."""
    if len(blob) < 8 or blob[:4] != FIT_MAGIC:
        raise ValueError("not an FDT/FIT blob")
    total = struct.unpack_from(">I", blob, 4)[0]
    if total < 40 or total > len(blob):
        raise ValueError(f"invalid FDT totalsize {total} for {len(blob)} available bytes")
    if any(blob[total:]):
        raise ValueError("nonzero bytes follow FDT totalsize")
    exact = blob[:total]
    fdt_nodes(exact)
    return exact


def fit_offset(package: bytes) -> tuple[int, bool] | None:
    if package.startswith(FIT_MAGIC):
        try:
            fit_blob(package)
            return 0, False
        except ValueError:
            return None
    start = 0
    while True:
        marker = package.find(WRAPPER_MARKER, start, min(len(package), 64 * 1024))
        if marker < 0:
            return None
        offset = marker + 160
        if offset + 40 <= len(package) and package[offset:offset + 4] == FIT_MAGIC:
            try:
                fit_blob(package[offset:])
                return offset, True
            except ValueError:
                pass
        start = marker + 1


def fit_has_bridge_ubi(fit: bytes) -> bool:
    try:
        nodes = fdt_nodes(fit)
    except ValueError:
        return False
    return any(path.startswith("/images/ubi-bridge-") for path in nodes)


def inspect_candidate_file(path: Path) -> dict[str, Any] | None:
    try:
        size = path.stat().st_size
        if not path.is_file() or size < 40 or size > MAX_CANDIDATE_BYTES:
            return None
        data = path.read_bytes()
        result = fit_offset(data)
        if result is None:
            return None
        offset, wrapped = result
        fit = fit_blob(data[offset:])
        if not fit_has_bridge_ubi(fit):
            return None
        return {
            "path": path,
            "data": data,
            "fit": fit,
            "fit_offset": offset,
            "is_wrapper": wrapped,
        }
    except (OSError, ValueError):
        return None


def build_candidates(build: Path) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    for path in build.rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        result = inspect_candidate_file(path)
        if result is not None:
            candidates.append(result)
    candidates.sort(
        key=lambda item: (
            not item["is_wrapper"],
            "bridge" not in item["path"].name.lower(),
            len(item["data"]),
            str(item["path"]),
        )
    )
    return candidates


def run_bwrap(
    root: Path,
    argv: list[str],
    qemu: Path,
    bwrap: Path,
    env: dict[str, str] | None = None,
    timeout: int = 180,
) -> dict[str, Any]:
    cmd = [
        str(bwrap), "--unshare-all", "--die-with-parent", "--new-session",
        "--bind", str(root), "/",
        "--ro-bind", str(qemu), "/qemu-arm",
        "--dev", "/dev",
        "--chdir", "/tmp",
        "--setenv", "PATH", "/bin:/usr/bin:/sbin:/usr/sbin",
        "--setenv", "LD_LIBRARY_PATH", "/lib:/usr/lib",
    ]
    for key, value in sorted((env or {}).items()):
        cmd += ["--setenv", key, value]
    cmd += ["/qemu-arm", *argv]
    try:
        completed = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout, check=False
        )
        return {
            "host_rc": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
            "timed_out": False,
            "command": cmd,
        }
    except subprocess.TimeoutExpired as error:
        return {
            "host_rc": None,
            "stdout": error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or ""),
            "stderr": error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or ""),
            "timed_out": True,
            "command": cmd,
        }


def save_run(directory: Path, name: str, result: dict[str, Any]) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.stdout.txt").write_text(result.get("stdout", ""), errors="replace")
    (directory / f"{name}.stderr.txt").write_text(result.get("stderr", ""), errors="replace")
    (directory / f"{name}.json").write_text(json.dumps(result, indent=2) + "\n")


def setup_synthetic_paths(root: Path, nand: bool = True) -> None:
    write_file(root, "tmp/sysinfo/board_name", "ap-mp02.1\n")
    write_file(root, "proc/device-tree/model", "Qualcomm Technologies, Inc. IPQ5018/AP-MP02.1\0")
    write_file(root, "proc/sys/vm/drop_caches", "0\n")
    entries = [
        (2, "0:BOOTCONFIG"), (3, "0:BOOTCONFIG1"),
        (8, "0:APPSBL"), (9, "0:APPSBL_1"),
        (14, "0:HLOS"), (15, "rootfs"), (16, "rootfs_1"),
    ]
    if nand:
        entries.append((22, "ubi_rootfs"))
    write_file(
        root,
        "proc/mtd",
        "dev: size erasesize name\n" + "".join(
            f'mtd{number}: 02800000 00020000 "{name}"\n'
            for number, name in entries
        ),
    )
    for number, _ in entries:
        write_file(root, f"sys/class/mtd/mtd{number}/writesize", "2048\n")
        write_file(root, f"sys/class/mtd/mtd{number}/ecc_strength", "4\n")
    for name, target in (("0:APPSBL", "0:APPSBL_1"), ("rootfs", "rootfs_1")):
        write_file(root, f"proc/boot_info/{name}/upgradepartition", target + "\n")
        write_file(root, f"proc/boot_info/{name}/primaryboot", "0\n")
    write_file(root, "proc/boot_info/getbinary_bootconfig", b"SYNTHETIC BOOTCONFIG0\n")
    write_file(root, "proc/boot_info/getbinary_bootconfig1", b"SYNTHETIC BOOTCONFIG1\n")
    write_file(root, "sys/firmware/devicetree/base/soc_version_major", b"\x01")
    write_file(root, "sys/mock/boot_layout", "0\n")
    write_file(root, "sys/devices/system/qfprom/qfprom0/authenticate", "0\n")


def install_qemu_dispatch(root: Path) -> None:
    """Shell calls must not depend on host binfmt inside the isolated root.

    Keep the OEM shell files unchanged. Functions dispatch their external applets,
    dumpimage, and our mock scripts explicitly through the mounted QEMU binary.
    Mock scripts source the same functions in their independent shell process.
    """
    lines = [f'{name}() {{ /qemu-arm /bin/busybox {name} "$@"; }}' for name in APPLET_LINKS]
    lines.append('dumpimage() { /qemu-arm /usr/bin/dumpimage "$@"; }')
    lines += [f'{name}() {{ /qemu-arm /bin/busybox sh /sbin/{name} "$@"; }}'
              for name in ("mtd", "ubiformat", "ubidetach", "fw_printenv", "reboot")]
    write_file(root, "tmp/qemu-command-functions.sh", "\n".join(lines) + "\n")


def mock_tools(root: Path) -> None:
    write_file(
        root,
        "sbin/mtd",
        "#!/bin/sh\n. /tmp/qemu-command-functions.sh || exit 96\n"
        "echo MTD $* >> /tmp/actions.log\n"
        "last=\"\"; for arg in \"$@\"; do last=\"$arg\"; done\n"
        "dest=$(basename \"$last\")\n"
        "mkdir -p /tmp/mock-writes\n"
        "cat > /tmp/mock-writes/$dest.bin\n"
        "exit 0\n",
        0o700,
    )
    write_file(
        root,
        "sbin/ubiformat",
        "#!/bin/sh\n. /tmp/qemu-command-functions.sh || exit 96\n"
        "echo UBIFORMAT $* >> /tmp/actions.log\n"
        "source=; want_source=0\n"
        "for arg in \"$@\"; do\n"
        "  if [ \"$want_source\" = 1 ]; then source=\"$arg\"; want_source=0; continue; fi\n"
        "  [ \"$arg\" = -f ] && want_source=1\n"
        "done\n"
        "[ -n \"$source\" ] || exit 97\n"
        "cp \"$source\" /tmp/mock-ubi-target.bin || exit 98\n"
        "rc=\"${MOCK_UBIFORMAT_RC:-0}\"\n"
        "if [ \"$rc\" = 7 ]; then\n"
        "  printf '\\000' | dd of=/tmp/mock-ubi-target.bin bs=1 count=1 conv=notrunc 2>/dev/null\n"
        "  echo INJECTED_UBIFORMAT_FAILURE_AFTER_TARGET_CORRUPTION >> /tmp/actions.log\n"
        "fi\n"
        "exit \"$rc\"\n",
        0o700,
    )
    write_file(
        root,
        "sbin/ubidetach",
        "#!/bin/sh\n. /tmp/qemu-command-functions.sh || exit 96\necho UBIDETACH $* >> /tmp/actions.log\nexit 0\n",
        0o700,
    )
    write_file(
        root,
        "sbin/reboot",
        "#!/bin/sh\n. /tmp/qemu-command-functions.sh || exit 96\necho REBOOT_BLOCKED $* >> /tmp/actions.log\nexit 99\n",
        0o700,
    )
    write_file(
        root,
        "sbin/fw_printenv",
        "#!/bin/sh\n. /tmp/qemu-command-functions.sh || exit 96\nprintf '%s\\n' 'machid=8040000' 'productname=NX30' 'swversion=NX30V100R012'\n",
        0o700,
    )
    write_file(root, "tmp/actions.log", "")
    (root / "tmp/mock-writes").mkdir(parents=True, exist_ok=True)


def setup_platform_root(oem_root: Path, root: Path) -> None:
    root.mkdir(parents=True)
    for rel in PLATFORM_FILES:
        copy_required(oem_root, root, rel)
    verify_platform_inputs(root)
    for rel in ("lib/libiconv.so.2", "usr/lib/libiconv.so.2"):
        if (oem_root / rel).is_file():
            copy_required(oem_root, root, rel)
    link_file(root, "lib/ld-musl-arm.so.1", "/lib/libc.so")
    link_file(root, "lib/ld-musl-armhf.so.1", "/lib/libc.so")
    for applet in APPLET_LINKS:
        link_file(root, f"bin/{applet}", "/bin/busybox")
    install_qemu_dispatch(root)
    mock_tools(root)
    setup_synthetic_paths(root, nand=True)


def verify_platform_inputs(root: Path) -> None:
    """Check the actual sandbox copies, not only the original input directory."""
    for rel in PLATFORM_FILES:
        actual = sha256((root / rel).read_bytes())
        if actual != OEM_INPUT_SHA256[rel]:
            raise ValueError(f"copied OEM input hash mismatch: {rel}")


def checked_shell_source(path: str, marker: str, functions: tuple[str, ...]) -> str:
    """A library's final command status is not its loading/initialization status.

    R012 functions.sh ends with an optional UCI include whose condition is false
    in this bounded root. Keep the original bytes; syntax-check and require its
    exports rather than treating that legitimate final false as a load failure.
    The sandbox copies are pinned separately before QEMU starts.
    """
    quoted = shlex.quote(path)
    names = " ".join(shlex.quote(name) for name in functions)
    return f"""/qemu-arm /bin/busybox sh -n {quoted} || {{ echo 'OEM library syntax check failed: {path}' >&2; exit 96; }}
. {quoted}
oem_source_rc=$?
echo {marker}_SOURCE_RC=$oem_source_rc
for oem_required_function in {names}; do
    command -v "$oem_required_function" > /dev/null || {{ echo "Missing OEM function: $oem_required_function" >&2; exit 96; }}
done
echo {marker}_EXPORTS_RC=0
"""


def run_platform_case(
    oem_root: Path,
    fit: bytes,
    output: Path,
    qemu: Path,
    bwrap: Path,
    name: str,
    ubiformat_rc: int = 0,
) -> dict[str, Any]:
    root = output / name / "root"
    setup_platform_root(oem_root, root)
    write_file(root, "tmp/bridge.itb", fit)
    harness = """#!/bin/sh
. /tmp/qemu-command-functions.sh || exit 96
/qemu-arm /bin/busybox --list > /tmp/busybox-applets || exit 96
for applet in __REQUIRED_APPLETS__; do
    grep -qx "$applet" /tmp/busybox-applets || { echo "Missing BusyBox applet: $applet" >&2; exit 96; }
done
# Exercise nested QEMU execution before interpreting any OEM check result.
touch /tmp/dependency-smoke || exit 96
cat /tmp/dependency-smoke > /dev/null || exit 96
/qemu-arm /usr/bin/dumpimage -l /tmp/bridge.itb > /tmp/dependency-dumpimage || exit 96
echo SANDBOX_DEPENDENCIES_RC=0
export SAVE_CONFIG=0 CONF_TAR=/tmp/no-config
__CHECKED_OEM_SOURCES__
for n in 2 3 8 9 14 15 16 22; do touch /dev/mtd$n /dev/mtdblock$n || exit 96; done
echo -n '' > /tmp/actions.log
platform_check_image /tmp/bridge.itb
check=$?
echo PLATFORM_CHECK_RC=$check
do_rc=1
if [ "$check" = 0 ]; then
    platform_do_upgrade /tmp/bridge.itb
    do_rc=$?
    echo PLATFORM_DO_RC=$do_rc
fi
echo ROOTFS_PRIMARY=$(cat /proc/boot_info/rootfs/primaryboot)
echo LAYOUT=$(cat /sys/mock/boot_layout)
cat /tmp/actions.log
[ "$check" = 0 ] || exit "$check"
exit "$do_rc"
""".replace("__REQUIRED_APPLETS__", " ".join(APPLET_LINKS)).replace(
        "__CHECKED_OEM_SOURCES__",
        checked_shell_source("/lib/functions.sh", "OEM_FUNCTIONS", ("append", "include", "config_get"))
        + checked_shell_source("/lib/upgrade/platform.sh", "OEM_PLATFORM",
            ("ipq806x_board_name", "get_image", "platform_check_image", "platform_do_upgrade", "image_demux", "do_flash_bootconfig")),
    )
    write_file(root, "tmp/run.sh", harness, 0o700)
    result = run_bwrap(
        root,
        ["/bin/busybox", "sh", "/tmp/run.sh"],
        qemu,
        bwrap,
        env={"MOCK_UBIFORMAT_RC": str(ubiformat_rc)},
    )
    result["copied_oem_input_sha256"] = {
        rel: sha256((root / rel).read_bytes()) for rel in PLATFORM_FILES
    }
    actions = root / "tmp/actions.log"
    result["actions"] = actions.read_text(errors="replace") if actions.exists() else ""
    writes: dict[str, Any] = {}
    for path in sorted((root / "tmp/mock-writes").glob("*.bin")):
        data = path.read_bytes()
        writes[path.name] = {"bytes": len(data), "sha256": sha256(data)}
    result["mock_mtd_writes"] = writes
    result["mock_ubiformat_rc"] = ubiformat_rc
    target = root / "tmp/mock-ubi-target.bin"
    result["mock_ubiformat_target"] = (
        {"bytes": target.stat().st_size, "sha256": sha256(target.read_bytes()), "path": str(target)}
        if target.is_file() else None
    )
    result["rootfs_primary_value"] = (root / "proc/boot_info/rootfs/primaryboot").read_text().strip()
    result["layout_value"] = (root / "sys/mock/boot_layout").read_text().strip()
    save_run(output / name, "platform", result)
    return result


def setup_outer_root(oem_root: Path, root: Path, checker: Path) -> None:
    root.mkdir(parents=True)
    for rel in ("bin/busybox", "lib/libc.so", "lib/libgcc_s.so.1", "lib/libmwopenrpc.so"):
        copy_required(oem_root, root, rel)
    iconv = find_oem_file(oem_root, ("lib/libiconv.so.2", "usr/lib/libiconv.so.2"))
    copy_required(oem_root, root, iconv.relative_to(oem_root).as_posix())
    link_file(root, "lib/ld-musl-arm.so.1", "/lib/libc.so")
    link_file(root, "lib/ld-musl-armhf.so.1", "/lib/libc.so")
    link_file(root, "bin/sh", "/bin/busybox")
    for applet in ("cat", "cut", "grep", "hexdump", "sed", "strings", "tr", "wc"):
        link_file(root, f"bin/{applet}", "/bin/busybox")
    copy_path = root / "usr/bin/check_outer"
    copy_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(checker, copy_path)
    copy_path.chmod(0o700)
    fw_mock = (
        "#!/bin/sh\n"
        "case \"${1:-}\" in\n"
        "  productname) echo NX30 ;;\n"
        "  swversion) echo NX30V100R012 ;;\n"
        "  *) printf '%s\\n' 'machid=8040000' 'productname=NX30' 'swversion=NX30V100R012' ;;\n"
        "esac\n"
    )
    for rel in ("bin/fw_printenv", "sbin/fw_printenv", "usr/bin/fw_printenv", "usr/sbin/fw_printenv"):
        write_file(root, rel, fw_mock, 0o700)
    setup_synthetic_paths(root, nand=True)
    write_file(root, "etc/fw_env.config", "# synthetic; no real environment access\n")
    write_file(root, "tmp/actions.log", "")


def marker_offset(package: bytes) -> int | None:
    pos = package.find(WRAPPER_MARKER, 0, min(len(package), 64 * 1024))
    return pos if pos >= 0 else None


def corrupt_checksum(package: bytes) -> bytes:
    marker = marker_offset(package)
    if marker is None:
        raise ValueError("outer package has no H3C_+k marker")
    offset = marker + 160 + 512
    if offset >= len(package):
        raise ValueError("package is too short for checksum corruption fixture")
    changed = bytearray(package)
    changed[offset] ^= 1
    return bytes(changed)


def recompute_outer_word_sum(package: bytes, marker: int) -> bytes:
    start = marker + 64
    checksum = marker + 72
    if (len(package) - start) % 4:
        raise ValueError("outer package length from checksum start is not word aligned")
    changed = bytearray(package)
    changed[checksum:checksum + 4] = b"\0\0\0\0"
    total = 0
    for offset in range(start, len(changed), 4):
        total = (total + struct.unpack_from(">I", changed, offset)[0]) & 0xFFFFFFFF
    struct.pack_into(">I", changed, checksum, (0xFFFFFFFF - total) & 0xFFFFFFFF)
    return bytes(changed)


def wrong_product(package: bytes) -> bytes:
    marker = marker_offset(package)
    if marker is None:
        raise ValueError("outer package has no H3C_+k marker")
    field = marker + 64 + 35
    version_marker = package.find(b"V", field, field + 36)
    if version_marker < 0 or version_marker - field < 2:
        raise ValueError("cannot locate product prefix in outer version field")
    changed = bytearray(package)
    prefix_len = version_marker - field
    replacement = (b"X" * prefix_len)[:prefix_len]
    changed[field:version_marker] = replacement
    return recompute_outer_word_sum(bytes(changed), marker)


def run_outer_case(
    root: Path,
    package: bytes,
    name: str,
    qemu: Path,
    bwrap: Path,
    output: Path,
) -> dict[str, Any]:
    package_path = write_file(root, "tmp/firmware.bin", package)
    result = run_bwrap(
        root,
        ["/usr/bin/check_outer", "/tmp/firmware.bin"],
        qemu,
        bwrap,
        timeout=120,
    )
    result["fixture"] = name
    result["input_bytes"] = len(package)
    result["input_sha256"] = sha256(package)
    result["input_path_in_guest"] = "/tmp/firmware.bin"
    save_run(output, name, result)
    try:
        package_path.unlink()
    except OSError:
        pass
    return result


def checker_function_rc(result: dict[str, Any]) -> int | None:
    if result.get("host_rc") != 0 or result.get("timed_out"):
        return None
    values = [line.removeprefix("MW_APP_CheckFirmware rc=")
              for line in result.get("stdout", "").splitlines()
              if line.startswith("MW_APP_CheckFirmware rc=")]
    if len(values) != 1:
        return None
    try:
        return int(values[0])
    except ValueError:
        return None


def compile_checker(cc: str, source: Path, destination: Path, output: Path) -> dict[str, Any]:
    command = shlex.split(cc) + ["-O2", "-Wall", "-Wextra", "-Wl,--export-dynamic", "-o", str(destination), str(source), "-ldl"]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    record = {
        "command": command,
        "host_rc": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "output": str(destination),
        "output_exists": destination.is_file(),
    }
    (output / "compile-check-outer.json").write_text(json.dumps(record, indent=2) + "\n")
    if result.returncode != 0 or not destination.is_file():
        raise RuntimeError("target compiler failed to build check_outer.c; see compile-check-outer.json")
    return record


def choose_candidate(build: Path, explicit_fit: Path | None, explicit_package: Path | None) -> tuple[Path, bytes, Path | None, bytes | None]:
    package_path = explicit_package
    if package_path is not None and not package_path.is_file():
        raise FileNotFoundError(f"candidate package does not exist: {package_path}")
    package_data = package_path.read_bytes() if package_path is not None else None

    fit_path = explicit_fit
    if fit_path is not None:
        if not fit_path.is_file():
            raise FileNotFoundError(f"candidate FIT does not exist: {fit_path}")
        fit_data = fit_path.read_bytes()
        located = fit_offset(fit_data)
        if located is None:
            raise ValueError(f"candidate FIT is not a valid FIT: {fit_path}")
        if located[1] and package_path is None:
            package_path, package_data = fit_path, fit_data
        fit_data = fit_blob(fit_data[located[0]:])
        if not fit_has_bridge_ubi(fit_data):
            raise ValueError(f"candidate FIT has no ubi-bridge-* component: {fit_path}")
        if package_data is not None:
            packaged_fit = fit_offset(package_data)
            if packaged_fit is None or not packaged_fit[1] or fit_blob(package_data[packaged_fit[0]:]) != fit_data:
                raise ValueError("candidate package does not contain the explicitly selected FIT byte-for-byte")
        return fit_path, fit_data, package_path, package_data

    candidates = build_candidates(build)
    if not candidates:
        raise FileNotFoundError(f"no FIT with a ubi-bridge-* image found under {build}")
    selected = candidates[0]
    if package_path is None and selected["is_wrapper"]:
        package_path = selected["path"]
        package_data = selected["data"]
    elif package_data is not None:
        packaged_fit = fit_offset(package_data)
        if packaged_fit is None or not packaged_fit[1] or fit_blob(package_data[packaged_fit[0]:]) != selected["fit"]:
            raise ValueError("candidate package does not contain the selected ubi-bridge FIT byte-for-byte")
    return selected["path"], selected["fit"], package_path, package_data


def _marker(result: dict[str, Any], name: str) -> str | None:
    values = [line[len(name) + 1:] for line in result.get("stdout", "").splitlines()
              if line.startswith(name + "=")]
    return values[0] if len(values) == 1 else None


def _execution_errors(result: dict[str, Any]) -> list[str]:
    errors = []
    if result.get("host_rc") != 0 or result.get("timed_out") is not False:
        errors.append("guest command failed, timed out, or lacks execution status")
    stderr = result.get("stderr", "").lower()
    if any(text in stderr for text in ("not found", "cannot execute", "exec format", "error loading", "error while loading")):
        errors.append("sandbox command or dynamic dependency could not execute")
    return errors


def evaluate_summary(summary: dict[str, Any], candidate_fit: bytes) -> bool:
    """Validate observations, including the unsafe *expected* OEM failure case.

    A zero process exit alone proves nothing: every parser return, dispatch,
    target byte hash, selector, and final BOOTCONFIG mock write is checked.
    """
    checks: list[dict[str, Any]] = []
    def record(name: str, errors: list[str]) -> None:
        checks.append({"name": name, "status": "FAIL" if errors else "PASS", "errors": errors})

    expected_outer = {"official": 0, "candidate": 0,
                      "candidate-corrupt-checksum": -2, "candidate-wrong-product": -2}
    outer = summary.get("outer_checker", [])
    for name, expected in expected_outer.items():
        rows = [row for row in outer if row.get("fixture") == name]
        errors = []
        if len(rows) != 1:
            errors.append("exactly one required outer-check fixture was not completed")
        else:
            errors += _execution_errors(rows[0])
            if rows[0].get("function_rc") != expected or checker_function_rc(rows[0]) != expected:
                errors.append(f"OEM checker did not uniquely return expected rc={expected}")
        record("outer:" + name, errors)
    if len(outer) != len(expected_outer):
        record("outer:fixture_set", ["unexpected, missing, or duplicate outer fixtures"])

    try:
        nodes = fdt_nodes(candidate_fit)
        images = [(path.rsplit("/", 1)[1], props["data"]) for path, props in nodes.items()
                  if path.startswith("/images/ubi-bridge-") and path.count("/") == 2 and "data" in props]
        if len(images) != 1:
            raise ValueError("expected exactly one inline ubi-bridge component")
        ubi_name, ubi_bytes = images[0]
        if not ubi_bytes or ubi_bytes[0] == 0:
            raise ValueError("fixture cannot prove first-byte corruption of this UBI component")
    except (ValueError, KeyError, struct.error) as error:
        record("platform:expected_input", [str(error)])
        ubi_name, ubi_bytes = "", b""
    expected_bc = {
        "mtd2.bin": b"SYNTHETIC BOOTCONFIG0\n".ljust(2048, b"\0"),
        "mtd3.bin": b"SYNTHETIC BOOTCONFIG1\n".ljust(2048, b"\0"),
    }
    platform = summary.get("platform_cases", [])
    for name, injected in (("candidate", 0), ("candidate-ubiformat-rc7-after-mock-target-corruption", 7)):
        rows = [row for row in platform if row.get("fixture") == name]
        errors = []
        if len(rows) != 1:
            errors.append("exactly one required platform fixture was not completed")
        else:
            row = rows[0]; errors += _execution_errors(row)
            if row.get("copied_oem_input_sha256") != {rel: OEM_INPUT_SHA256[rel] for rel in PLATFORM_FILES}:
                errors.append("copied sandbox OEM inputs do not match pinned original hashes")
            for marker, expected in (("SANDBOX_DEPENDENCIES_RC", "0"),
                                     ("OEM_FUNCTIONS_SOURCE_RC", "1"), ("OEM_FUNCTIONS_EXPORTS_RC", "0"),
                                     ("OEM_PLATFORM_SOURCE_RC", "0"), ("OEM_PLATFORM_EXPORTS_RC", "0"),
                                     ("PLATFORM_CHECK_RC", "0"),
                                     ("PLATFORM_DO_RC", "0"), ("ROOTFS_PRIMARY", "1"), ("LAYOUT", "0")):
                if _marker(row, marker) != expected:
                    errors.append(f"missing, duplicate, or unexpected {marker}; expected {expected}")
            if row.get("rootfs_primary_value") != "1" or row.get("layout_value") != "0":
                errors.append("synthetic selector/layout files do not match finalization output")
            if row.get("mock_ubiformat_rc") != injected:
                errors.append("fault-injection mode differs from the expected fixture")
            expected_actions = ["UBIDETACH -f -p /dev/mtd15",
                                f"UBIFORMAT /dev/mtd16 -y -f /tmp/{ubi_name}.bin"]
            if injected:
                expected_actions.append("INJECTED_UBIFORMAT_FAILURE_AFTER_TARGET_CORRUPTION")
            expected_actions += ["MTD -e /dev/mtd2 write - /dev/mtd2", "MTD -e /dev/mtd3 write - /dev/mtd3"]
            if row.get("actions", "").splitlines() != expected_actions:
                errors.append("mock action sequence is missing, unexpected, or targets another partition")
            expected_target = b"\0" + ubi_bytes[1:] if injected else ubi_bytes
            target = row.get("mock_ubiformat_target") or {}
            if not ubi_bytes or target.get("bytes") != len(expected_target) or target.get("sha256") != sha256(expected_target):
                errors.append("mock UBI target bytes do not match the expected successful/corrupted input")
            writes = row.get("mock_mtd_writes", {})
            if set(writes) != set(expected_bc):
                errors.append("BOOTCONFIG mock writes missing, duplicated, or include unexpected MTD targets")
            else:
                for key, expected in expected_bc.items():
                    if writes[key].get("bytes") != len(expected) or writes[key].get("sha256") != sha256(expected):
                        errors.append("BOOTCONFIG mock bytes differ: " + key)
            row["validation_errors"] = errors
            row["verification_status"] = ("FAIL_PLATFORM_FIXTURE" if errors else
                "PASS_EXPECTED_OEM_FAIL_OPEN_REPRODUCED" if injected else "PASS_EXPECTED_SYNTHETIC_DISPATCH")
        record("platform:" + name, errors)
    if len(platform) != 2:
        record("platform:fixture_set", ["unexpected, missing, or duplicate platform fixtures"])
    summary["checks"] = checks
    summary["failed_checks"] = [row["name"] for row in checks if row["status"] != "PASS"]
    summary["validation_errors"] = [row["name"] + ": " + error for row in checks for error in row["errors"]]
    passed = not summary["failed_checks"]
    summary["status"] = "PASS_OFFLINE_OEM_CHECKS" if passed else "FAIL_OFFLINE_OEM_CHECKS"
    summary["expected_oem_fail_open_reproduced"] = any(row.get("verification_status") == "PASS_EXPECTED_OEM_FAIL_OPEN_REPRODUCED" for row in platform)
    return passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True, help="build3 directory containing candidate FIT/wrapper")
    parser.add_argument("--oem-root", type=Path, required=True, help="exact extracted original R012 rootfs")
    parser.add_argument("--cc", required=True, help="target ARM compiler command for check_outer.c")
    parser.add_argument("--output", type=Path, required=True, help="new private output directory; must not exist")
    parser.add_argument("--official", type=Path, help="official R012 wrapper package (optional baseline)")
    parser.add_argument("--candidate-fit", type=Path, help="explicit raw candidate FIT (optional)")
    parser.add_argument("--candidate-package", type=Path, help="explicit candidate OEM wrapper (optional)")
    args = parser.parse_args()

    build = args.build.resolve()
    oem_root = args.oem_root.resolve()
    if not build.is_dir():
        parser.error(f"build directory does not exist: {build}")
    if not oem_root.is_dir():
        parser.error(f"OEM root does not exist: {oem_root}")
    try:
        oem_input_rows = verify_oem_inputs(oem_root)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.output.exists():
        parser.error(f"output path already exists; select a new output path: {args.output}")
    output = args.output.resolve()
    output.mkdir(parents=True, mode=0o700)

    bwrap = shutil.which("bwrap")
    qemu_name = shutil.which("qemu-arm-static")
    if not bwrap or not qemu_name:
        parser.error("bwrap and qemu-arm-static must already be installed on this offline host")
    bwrap_path = Path(bwrap)
    qemu = Path(qemu_name)

    fit_path, candidate_fit, candidate_package_path, candidate_package = choose_candidate(
        build, args.candidate_fit, args.candidate_package
    )
    (output / "inputs").mkdir(mode=0o700)
    copied_fit = write_file(output, "inputs/bridge.itb", candidate_fit)
    input_rows: list[dict[str, Any]] = [
        {"role": "candidate-fit", "path": str(fit_path), "bytes": len(candidate_fit), "sha256": sha256(candidate_fit)}
    ]
    if candidate_package_path is not None and candidate_package is not None:
        input_rows.append({
            "role": "candidate-package", "path": str(candidate_package_path),
            "bytes": len(candidate_package), "sha256": sha256(candidate_package),
        })
    official_data: bytes | None = None
    if args.official is not None and args.official.is_file():
        official_data = args.official.read_bytes()
        input_rows.append({"role": "official-package", "path": str(args.official.resolve()), "bytes": len(official_data), "sha256": sha256(official_data)})

    checker_source = Path(__file__).resolve().parents[1] / "tests" / "check_outer.c"
    checker_binary = output / "outer-check-arm"
    compile_result = compile_checker(args.cc, checker_source, checker_binary, output)

    summary: dict[str, Any] = {
        "scope": "offline OEM parser and dispatcher fixtures only; no device, web server, real MTD, or hardware auth",
        "sandbox": "bwrap --unshare-all; synthetic /proc and /sys; private /dev; QEMU user mode; mock mtd/ubiformat/ubidetach/reboot",
        "outer_process_context": "synthetic absolute exports at OEM webs addresses 0x50000000/0x50200000 with zeroed private anonymous 2 MiB maps; ELF symbol sizes are unspecified, so this emulation is explicitly not target acceptance",
        "oem_root": str(oem_root),
        "build": str(build),
        "output": str(output),
        "official_package": str(args.official.resolve()) if args.official else None,
        "oem_inputs": oem_input_rows,
        "inputs": input_rows,
        "compiler": compile_result,
        "candidate_fit_copy": str(copied_fit),
        "outer_checker": [],
        "platform_cases": [],
        "not_established": [
            "live Web acceptance or device authentication state",
            "hardware flash behavior, selector commit, ECC, or bootability",
            "real qfprom/TZ secure-world behavior",
        ],
    }

    outer_work = output / "outer-root"
    setup_outer_root(oem_root, outer_work, checker_binary)
    baseline_ok = False
    if official_data is None:
        summary["outer_checker"].append({"fixture": "official", "status": "NOT_ESTABLISHED_NO_OFFICIAL_PACKAGE"})
    else:
        official_row = run_outer_case(
            outer_work, official_data, "official", qemu, bwrap_path, output / "outer-results"
        )
        official_row["function_rc"] = checker_function_rc(official_row)
        summary["outer_checker"].append(official_row)
        baseline_ok = official_row["function_rc"] == 0

    if not baseline_ok:
        summary["outer_checker"].append({
            "fixture": "candidate-and-mutations",
            "status": "NOT_ESTABLISHED_OFFICIAL_BASELINE_DID_NOT_RETURN_ZERO",
        })
    elif candidate_package is None:
        summary["outer_checker"].append({
            "fixture": "candidate-and-mutations",
            "status": "NOT_RUN_NO_CANDIDATE_WRAPPER_FILE",
        })
    else:
        row = run_outer_case(
            outer_work, candidate_package, "candidate", qemu, bwrap_path, output / "outer-results"
        )
        row["function_rc"] = checker_function_rc(row)
        summary["outer_checker"].append(row)
        for fixture_name, transform in (
            ("candidate-corrupt-checksum", corrupt_checksum),
            ("candidate-wrong-product", wrong_product),
        ):
            try:
                changed = transform(candidate_package)
            except ValueError as error:
                summary["outer_checker"].append({"fixture": fixture_name, "status": "NOT_RUN", "reason": str(error)})
                continue
            row = run_outer_case(outer_work, changed, fixture_name, qemu, bwrap_path, output / "outer-results")
            row["function_rc"] = checker_function_rc(row)
            summary["outer_checker"].append(row)

    positive = run_platform_case(oem_root, candidate_fit, output, qemu, bwrap_path, "candidate", ubiformat_rc=0)
    summary["platform_cases"].append({"fixture": "candidate", **positive})
    negative = run_platform_case(
        oem_root, candidate_fit, output, qemu, bwrap_path,
        "candidate-ubiformat-rc7-after-mock-target-corruption", ubiformat_rc=7,
    )
    summary["platform_cases"].append({
        "fixture": "candidate-ubiformat-rc7-after-mock-target-corruption",
        "same_valid_candidate_fit_as_positive_case": True,
        **negative,
    })

    passed = evaluate_summary(summary, candidate_fit)
    (output / "RESULT.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({
        "status": summary["status"], "failed_checks": summary["failed_checks"],
        "result_file": str(output / "RESULT.json"),
        "candidate_fit": str(fit_path),
        "outer_cases": [{"fixture": x.get("fixture"), "host_rc": x.get("host_rc"), "function_rc": x.get("function_rc"), "status": x.get("status")} for x in summary["outer_checker"]],
        "platform_cases": [{"fixture": x.get("fixture"), "host_rc": x.get("host_rc"), "mock_ubiformat_rc": x.get("mock_ubiformat_rc")} for x in summary["platform_cases"]],
        "scope": summary["scope"],
    }, indent=2, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"check_oem_offline: {error}", file=sys.stderr)
        raise SystemExit(2)
