#!/usr/bin/env python3
"""Host regression for full-size RX3000 UBI capacity handling.

This builds the candidate's real HTTP handler and core with the existing test
shim. It never connects to hardware or a network and never writes source trees.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

EXPECTED_FIXTURE_SHA256 = "db24893ff19b0871a0b43db7febcbc59e3ad0c52d9dd007604ababf588d927e9"
EXPECTED_FIXTURE_SIZE = 40 * 1024 * 1024
PEB_SIZE = 0x20000
TARGETS = (("rootfs", 0x900000), ("rootfs_1", 0x3100000))
FLASH_SIZE = 128 * 1024 * 1024


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            block = stream.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def load_handler(tree):
    test_path = tree / "u-boot-2016/test/firmware_rt3000_b_handler_hosttest.py"
    spec = importlib.util.spec_from_file_location("rx3000_handler_hosttest", test_path)
    if not spec or not spec.loader:
        raise RuntimeError(f"cannot import handler harness: {test_path}")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.ROOT = tree
    helper.MODULE = tree / "u-boot-2016/failsafe/modules/firmware_rt3000_b.c"
    helper.OEM_HEADER = tree / "u-boot-2016/failsafe/modules/firmware_oem_rt3000_b.h"
    for needed in (helper.MODULE, helper.OEM_HEADER,
                   tree / "u-boot-2016/failsafe/modules/firmware_rt3000_b_core.c",
                   tree / "u-boot-2016/include/rt3000_b_fw_core.h"):
        if not needed.is_file():
            raise FileNotFoundError(f"candidate tree is missing required source: {needed}")

    # Add test-only instrumentation to the generated host harness. Production
    # sources remain verbatim; bad-block and whole-flash hash probes are wrappers.
    helper.SHIM = helper.SHIM.replace(
        "#include <openssl/sha.h>",
        "extern unsigned char *SHA256(const unsigned char *, size_t, unsigned char *);",
    ).replace(
        "static struct mtd_info nand_info[1];",
        "static struct mtd_info nand_info[1];\nstatic u64 host_bad_offset = ~(u64)0;",
    ).replace(
        "static int nand_block_isbad(struct mtd_info *m,u64 off){(void)m;(void)off;return 0;}",
        "static int nand_block_isbad(struct mtd_info *m,u64 off){(void)m;return off==host_bad_offset;}",
    )
    helper.SHIM = helper.re.sub(r"^static size_t strlcpy[^\n]*\n", "", helper.SHIM, flags=helper.re.M)
    helper.SHIM = helper.re.sub(r"^static size_t strlcat[^\n]*\n", "", helper.SHIM, flags=helper.re.M)
    helper.SHIM = helper.SHIM.replace(
        "static const struct rt3_partition *rt3_layout_find(",
        "static int rt3_layout_find(",
    )
    helper.WRAPPERS += r"""
void host_fill_flash(unsigned char value) { memset(host_flash,value,host_flash_size); host_bad_offset=~(u64)0; }
void host_set_bad_offset(u64 offset) { host_bad_offset=offset; }
unsigned int host_window_opens_get(void) { return host_window_opens; }
int host_copy_range(u64 offset,u64 size,unsigned char *out) {
    if(offset>host_flash_size || size>host_flash_size-offset) return -1;
    memcpy(out,host_flash+offset,size); return 0;
}
int host_copy_outside(u64 offset,u64 size,unsigned char *out) {
    if(offset>host_flash_size || size>host_flash_size-offset) return -1;
    memcpy(out,host_flash,offset);
    memcpy(out+offset,host_flash+offset+size,host_flash_size-offset-size);
    return 0;
}
int host_validate_raw_ubi(const unsigned char *data,size_t size,unsigned int *image_pebs,
                          unsigned int *reserved_pebs) {
    struct rt3_ubi_summary summary; int ret=fw_validate_ubi(data,size,&summary);
    if(!ret){*image_pebs=summary.image_pebs;*reserved_pebs=summary.reserved_pebs;}
    return ret;
}
"""
    original_run = helper.subprocess.run
    def run_with_runtime_soname(command, *args, **kwargs):
        command = ["-Wl,-l:libcrypto.so.3" if item == "-lcrypto" else item
                   for item in command]
        return original_run(command, *args, **kwargs)
    helper.subprocess.run = run_with_runtime_soname
    return helper


def bind_lib(lib):
    lib.host_reset.argtypes = [ctypes.c_int]
    lib.host_fill_flash.argtypes = [ctypes.c_ubyte]
    lib.host_set_bad_offset.argtypes = [ctypes.c_uint64]
    lib.host_copy_range.argtypes = [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_void_p]
    lib.host_copy_outside.argtypes = [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_void_p]
    for name in ("host_window_opens_get", "host_window_closes_get",
                 "host_erase_count_get", "host_write_count_get", "host_mtd_flags_get"):
        getattr(lib, name).restype = ctypes.c_uint
    lib.host_window_is_open.restype = ctypes.c_int
    lib.host_validate_raw_ubi.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                          ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
    lib.host_validate_raw_ubi.restype = ctypes.c_int
    return lib


def c_hash(lib, name, offset, size):
    copy_size = FLASH_SIZE - size if name == "host_copy_outside" else size
    copied = (ctypes.c_ubyte * copy_size)()
    if getattr(lib, name)(offset, size, copied) != 0:
        raise AssertionError(f"host hash failed: {name}")
    return hashlib.sha256(memoryview(copied)).hexdigest()


def dispatch(helper, lib, path, method, fields=()):
    fields = tuple((name, bytes(raw) if isinstance(raw, bytearray) else raw)
                   for name, raw in fields)
    code, body, keep = helper.dispatch(lib, path, method, fields)
    try:
        obj = json.loads(body) if body else {}
    except json.JSONDecodeError:
        obj = {"body_status": "non_json"}
    return code, obj, keep


def validate_and_commit(helper, lib, fixture, target_name, expected_digest):
    lib.host_reset(1)
    lib.host_fill_flash(0xA5)
    target_offset = dict(TARGETS)[target_name]
    outside_before = c_hash(lib, "host_copy_outside", target_offset, EXPECTED_FIXTURE_SIZE)
    validate_code, response, keep = dispatch(helper, lib, "/firmware/validate", 2,
                                             (("firmware", fixture), ("target", target_name), ("mode", "dual")))
    assert validate_code == 200 and response.get("status") == "ok", (target_name, validate_code, response)
    assert lib.host_erase_count_get() == 0 and lib.host_window_opens_get() == 0
    token = response["token"]
    commit_code, committed, _ = dispatch(helper, lib, "/firmware/write", 2,
                                         (("token", token), ("target", target_name), ("mode", "dual")))
    assert commit_code == 200 and committed.get("verified") is True, (target_name, commit_code, committed)
    target_hash = c_hash(lib, "host_copy_range", target_offset, EXPECTED_FIXTURE_SIZE)
    outside_after = c_hash(lib, "host_copy_outside", target_offset, EXPECTED_FIXTURE_SIZE)
    assert target_hash == expected_digest, (target_name, target_hash, expected_digest)
    assert outside_after == outside_before, (target_name, outside_before, outside_after)
    assert lib.host_erase_count_get() == 320
    # All-0xff pages stay erased; every other page is written once.
    data_pages = sum(1 for o in range(0, len(fixture), 2048)
                     if fixture[o:o + 2048] != b"\xff" * 2048)
    assert lib.host_write_count_get() == data_pages
    assert lib.host_ff_page_writes_get() == 0
    assert lib.host_window_opens_get() == 1 and lib.host_window_closes_get() == 1
    assert lib.host_window_is_open() == 0 and lib.host_mtd_flags_get() == 0x80
    replay, _, _ = dispatch(helper, lib, "/firmware/write", 2,
                            (("token", token), ("target", target_name), ("mode", "dual")))
    assert replay == 409 and lib.host_erase_count_get() == 320
    return {"validate_http": validate_code, "commit_http": commit_code,
            "target": target_name, "target_offset": target_offset,
            "image_pebs": 320, "written_pages": int(lib.host_write_count_get()),
            "erased_pebs": int(lib.host_erase_count_get()),
            "target_sha256": target_hash, "fixture_sha256": expected_digest,
            "outside_sha256_before": outside_before, "outside_sha256_after": outside_after,
            "window_opens": int(lib.host_window_opens_get()),
            "window_closes": int(lib.host_window_closes_get()), "token_replay_http": replay}


def run_mutation_and_bad_block(helper, lib, fixture):
    results = {}
    # A staged-source mutation invalidates and consumes the validation token.
    lib.host_reset(1)
    lib.host_fill_flash(0xA5)
    code, response, keep = dispatch(helper, lib, "/firmware/validate", 2,
                                    (("firmware", fixture), ("target", "rootfs_1"), ("mode", "dual")))
    assert code == 200
    token = response["token"]
    staged = keep[0]
    old = staged[0]
    staged[0] ^= 1
    changed, _, _ = dispatch(helper, lib, "/firmware/write", 2,
                             (("token", token), ("target", "rootfs_1"), ("mode", "dual")))
    staged[0] = old
    replay, _, _ = dispatch(helper, lib, "/firmware/write", 2,
                            (("token", token), ("target", "rootfs_1"), ("mode", "dual")))
    assert changed == 409 and replay == 409
    assert lib.host_window_opens_get() == 0 and lib.host_erase_count_get() == 0
    results["staged_mutation"] = {"write_http": changed, "replay_http": replay,
                                  "window_opens": 0, "erased_pebs": 0}

    # Actual core bad-block scan must reject the full 320-PEB stream before opening
    # the window or erasing; the token is consumed even on this commit failure.
    lib.host_reset(1)
    lib.host_fill_flash(0xA5)
    code, response, _ = dispatch(helper, lib, "/firmware/validate", 2,
                                 (("firmware", fixture), ("target", "rootfs_1"), ("mode", "dual")))
    assert code == 200
    token = response["token"]
    lib.host_set_bad_offset(0x3100000 + 117 * PEB_SIZE)
    rejected, body, _ = dispatch(helper, lib, "/firmware/write", 2,
                                 (("token", token), ("target", "rootfs_1"), ("mode", "dual")))
    replay, _, _ = dispatch(helper, lib, "/firmware/write", 2,
                            (("token", token), ("target", "rootfs_1"), ("mode", "dual")))
    assert rejected == 500 and replay == 409, (rejected, body, replay)
    assert lib.host_window_opens_get() == 0 and lib.host_erase_count_get() == 0
    assert lib.host_window_is_open() == 0 and lib.host_mtd_flags_get() == 0x80
    results["one_bad_peb"] = {"write_http": rejected, "replay_http": replay,
                              "window_opens": int(lib.host_window_opens_get()),
                              "erased_pebs": int(lib.host_erase_count_get()),
                              "mtd_flags": int(lib.host_mtd_flags_get())}
    return results


def run_parser_bounds(lib, fixture):
    # Extend the real valid stream only with EC-valid, same-sequence, VID-empty
    # PEBs. This exercises parser limits, not the 40MiB dual-slot HTTP target.
    first_ec = fixture[:64]
    full = bytearray(fixture)
    blank = bytearray(b"\xff" * PEB_SIZE)
    blank[:64] = first_ec
    for _ in range(320):
        full.extend(blank)
    assert len(full) == 640 * PEB_SIZE

    def validate(data):
        backing = (ctypes.c_ubyte * len(data)).from_buffer(data)
        image_pebs = ctypes.c_uint()
        reserved = ctypes.c_uint()
        ret = lib.host_validate_raw_ubi(backing, len(data), ctypes.byref(image_pebs), ctypes.byref(reserved))
        return ret, image_pebs.value, reserved.value

    ret640, pebs640, reserved640 = validate(full)
    assert ret640 == 0 and pebs640 == 640 and reserved640 > 0, (ret640, pebs640, reserved640)
    too_large = bytearray(full)
    too_large.extend(blank)
    ret641, _, _ = validate(too_large)
    assert ret641 != 0
    return {"640_peb_http_independent_parser_status": ret640,
            "640_peb_count": pebs640, "reserved_pebs": reserved640,
            "641_peb_parser_rejected": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", required=True, type=Path, help="candidate U-Boot source tree root")
    parser.add_argument("--fixture", required=True, type=Path, help="40MiB RX3000 mtd15 ordinary-data fixture")
    parser.add_argument("--output", required=True, type=Path, help="JSON evidence output path")
    parser.add_argument("--expect-reject", action="store_true",
                        help="assert candidate rejects fixture at HTTP validate without opening/erasing")
    args = parser.parse_args()
    tree = args.tree.resolve()
    fixture_path = args.fixture.resolve()
    if fixture_path.stat().st_size != EXPECTED_FIXTURE_SIZE:
        raise SystemExit(f"fixture size mismatch: expected {EXPECTED_FIXTURE_SIZE} bytes")
    fixture_hash = sha256_file(fixture_path)
    if fixture_hash != EXPECTED_FIXTURE_SHA256:
        raise SystemExit("fixture SHA256 does not match the authorized RX3000 mtd15 identity")
    fixture = fixture_path.read_bytes()
    helper = load_handler(tree)
    helper.map_upload_window()
    evidence = {"schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
                "test": "RX3000 full-PEB capacity regression; host-only; no device/network writes",
                "tree": str(tree), "fixture_bytes": len(fixture), "fixture_sha256": fixture_hash,
                "result": "running"}
    with tempfile.TemporaryDirectory(prefix="rx3000-capacity-hosttest-") as td:
        lib = bind_lib(helper.build_test_lib(td))
        lib.host_reset(1)
        if args.expect_reject:
            lib.host_fill_flash(0xA5)
            code, response, _ = dispatch(helper, lib, "/firmware/validate", 2,
                                         (("firmware", fixture), ("target", "rootfs_1"), ("mode", "dual")))
            assert code == 422 and lib.host_window_opens_get() == 0 and lib.host_erase_count_get() == 0, (code, response)
            assert response.get("error") == "UBI capacity leaves insufficient layout and wear-level reserve", response
            evidence["expected_old_rejection"] = {"validate_http": code,
                "error": response["error"],
                "window_opens": int(lib.host_window_opens_get()),
                "erased_pebs": int(lib.host_erase_count_get())}
        else:
            evidence["target_commits"] = [
                validate_and_commit(helper, lib, fixture, name, fixture_hash)
                for name, _ in TARGETS
            ]
            evidence["negative_cases"] = run_mutation_and_bad_block(helper, lib, fixture)
            evidence["parser_max_bounds"] = run_parser_bounds(lib, bytearray(fixture))

            # Exercise the production validator on a real structural CRC failure.
            bad = bytearray(fixture)
            bad[60] ^= 1  # EC header CRC byte in the first PEB
            lib.host_reset(1)
            code, _, _ = dispatch(helper, lib, "/firmware/validate", 2,
                                  (("firmware", bad), ("target", "rootfs_1"), ("mode", "dual")))
            assert code == 422 and lib.host_erase_count_get() == 0 and lib.host_window_opens_get() == 0
            evidence["invalid_ec_crc_http"] = code
    evidence["result"] = "PASS"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    print(f"RX3000_CAPACITY_HOSTTEST_PASS: fixture_sha256={fixture_hash} output={args.output.resolve()}")


if __name__ == "__main__":
    main()
