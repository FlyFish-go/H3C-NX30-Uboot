#!/usr/bin/env python3
"""Run the production UBI validator against the immutable RT3000 OEM catalog."""
import ctypes
import hashlib
import json
import os
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "u-boot-2016/failsafe/modules/firmware_rt3000_b.c"
CATALOG = ROOT / "doc/RT3000-B-OEM-CATALOG-20261008.json"


def make_test_lib(tmp):
    src = SOURCE.read_text()
    begin = src.index("static int fw_crc_ok(")
    end = src.index("\nstatic const struct rt3_oem_fw *fw_catalog_match", begin)
    validator = src[begin:end]
    import re
    validator = re.sub(r"return -(EINVAL|EFBIG|EBADMSG|EOVERFLOW|ENOSPC);",
                       r"return host_err(__LINE__, \1);", validator)
    prefix = r"""
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
static int host_err(int line, int code) { (void)line; return -code; }
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define RT3_FW_MAX_UPLOAD (80U * 1024U * 1024U)
#define RT3_FW_MAX_PEB_COUNT 640U
#define RT3_UBI_FREE_MARGIN 24U
#define RT3_UBI_PEB_SIZE 0x20000U
#define RT3_UBI_PAGE_SIZE 2048U
#define RT3_UBI_VID_OFFSET 2048U
#define RT3_UBI_DATA_OFFSET 4096U
#define RT3_UBI_LEB_SIZE (RT3_UBI_PEB_SIZE - RT3_UBI_DATA_OFFSET)
#define RT3_UBI_VTBL_REC_SIZE 172U
#define RT3_UBI_VTBL_SIZE (128U * RT3_UBI_VTBL_REC_SIZE)
#define RT3_UBI_LAYOUT_ID (0x7fffffffU - 4096U)
#define UBI_EC_HDR_MAGIC 0x55424923U
#define UBI_VID_HDR_MAGIC 0x55424921U
#define UBI_VID_HDR_SIZE 64U
#define UBI_VERSION 1
#define UBI_CRC32_INIT 0xffffffffU
#define UBI_VID_DYNAMIC 1
#define UBI_VID_STATIC 2
#define UBI_COMPAT_REJECT 5
#define UBI_FM_SB_VOLUME_ID (RT3_UBI_LAYOUT_ID + 1)
#define UBI_FM_DATA_VOLUME_ID (RT3_UBI_LAYOUT_ID + 2)
#define UBI_VOL_NAME_MAX 127
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
struct rt3_ubi_summary { u32 image_pebs; u32 reserved_pebs; };
static u32 get_unaligned_be32(const void *v) { const u8 *p=v; return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
static u16 get_unaligned_be16(const void *v) { const u8 *p=v; return ((u16)p[0]<<8)|p[1]; }
static u16 fw_get_unaligned_le16(const u8 *p) { return p[0] | ((u16)p[1] << 8); }
static u64 fw_get_unaligned_le64(const u8 *p) { u64 v=0; int i; for(i=7;i>=0;i--) v=(v<<8)|p[i]; return v; }
static u32 crc32(u32 seed, const unsigned char *data, size_t len) {
    u32 c = seed; size_t i; int b;
    for (i=0; i<len; i++) { c ^= data[i]; for (b=0; b<8; b++) c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U))); }
    return c;
}
"""
    src_text = prefix + "\n" + validator + r"""
int rt3_host_validate(const unsigned char *p, size_t n, unsigned int *image_pebs,
                      unsigned int *reserved_pebs) {
    struct rt3_ubi_summary s; int r=fw_validate_ubi(p,n,&s);
    *image_pebs=s.image_pebs; *reserved_pebs=s.reserved_pebs; return r;
}
"""
    cpath = Path(tmp) / "ubi_validator.c"
    so = Path(tmp) / "ubi_validator.so"
    cpath.write_text(src_text)
    subprocess.run(["gcc", "-shared", "-fPIC", "-O2", str(cpath), "-o", str(so)], check=True)
    lib = ctypes.CDLL(str(so))
    lib.rt3_host_validate.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                     ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
    lib.rt3_host_validate.restype = ctypes.c_int
    return lib


def extract_ubi(entry):
    package = Path(entry["package_paths"][0]).read_bytes()
    assert len(package) == entry["package_bytes"], entry["release"]
    assert hashlib.sha256(package).hexdigest() == entry["package_sha256"], entry["release"]
    fit = package[entry["fit_offset"]:entry["fit_offset"] + entry["fit_total_size"]]
    assert len(fit) == entry["fit_total_size"]
    # libfdt is the same parser used by the firmware module; use its zero-copy property API.
    libfdt = ctypes.CDLL("libfdt.so.1")
    libfdt.fdt_check_header.argtypes = [ctypes.c_void_p]
    libfdt.fdt_check_header.restype = ctypes.c_int
    libfdt.fdt_path_offset.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    libfdt.fdt_path_offset.restype = ctypes.c_int
    libfdt.fdt_getprop.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p,
                                  ctypes.POINTER(ctypes.c_int)]
    libfdt.fdt_getprop.restype = ctypes.c_void_p
    fit_buf = ctypes.create_string_buffer(fit)
    assert libfdt.fdt_check_header(fit_buf) == 0, entry["release"]
    node = libfdt.fdt_path_offset(fit_buf, ("/images/" + entry["fit_ubi_image_name"]).encode())
    assert node >= 0, entry["release"]
    length = ctypes.c_int()
    prop = libfdt.fdt_getprop(fit_buf, node, b"data", ctypes.byref(length))
    assert prop and length.value == entry["fit_ubi_bytes"], entry["release"]
    ubi = ctypes.string_at(prop, length.value)
    assert hashlib.sha256(ubi).hexdigest() == entry["fit_ubi_sha256"], entry["release"]
    return bytearray(ubi)


def validate(lib, data):
    image_pebs, reserved_pebs = ctypes.c_uint(), ctypes.c_uint()
    backing = (ctypes.c_ubyte * len(data)).from_buffer(data)
    result = lib.rt3_host_validate(backing, len(data), ctypes.byref(image_pebs), ctypes.byref(reserved_pebs))
    return result, image_pebs.value, reserved_pebs.value


def crc(data):
    c = 0xffffffff
    for value in data:
        c ^= value
        for _ in range(8):
            c = (c >> 1) ^ (0xedb88320 if c & 1 else 0)
    return c & 0xffffffff


def main():
    entries = json.loads(CATALOG.read_text())["entries"]
    rt = [e for e in entries if e["model"] == "RT3000"]
    assert len(rt) == 16, len(rt)
    with tempfile.TemporaryDirectory(prefix="rt3-ubi-hosttest-") as td:
        lib = make_test_lib(td)
        images = {}
        for entry in rt:
            ubi = extract_ubi(entry)
            ret, image_pebs, reserved = validate(lib, ubi)
            assert ret == 0, (entry["release"], ret)
            assert image_pebs == len(ubi) // 0x20000
            assert reserved > 0
            images[entry["release"]] = ubi
        sample = bytearray(images["R012"])
        # EC header CRC corruption is rejected.
        bad = bytearray(sample); bad[5] ^= 1
        assert validate(lib, bad)[0] != 0
        # VTBL empty slots must carry CRC32 of their zero-filled record body.
        # Locate both layout-volume PEBs and corrupt an empty slot CRC.
        layouts = []
        for pos in range(0, len(sample), 0x20000):
            vid = pos + 2048
            if struct.unpack_from(">I", sample, vid)[0] == 0x55424921 and struct.unpack_from(">I", sample, vid + 8)[0] == 0x7fffffff - 4096:
                layouts.append(pos)
        assert len(layouts) == 2
        table = layouts[0] + 4096
        empty = next(i for i in range(128) if sample[table + i*172:table+i*172+168] == b"\0"*168)
        bad = bytearray(sample); bad[table + empty*172 + 171] ^= 1
        assert validate(lib, bad)[0] != 0
        # Removing the required kernel volume entry from both redundant tables fails.
        bad = bytearray(sample)
        table_offsets = [pos + 4096 for pos in layouts]
        kernel_record = next(i for i in range(128)
                             if bad[table_offsets[0] + i*172 + 16:table_offsets[0] + i*172 + 22] == b"kernel")
        for table_off in table_offsets:
            rec = table_off + kernel_record * 172
            bad[rec:rec+168] = b"\0" * 168
            struct.pack_into(">I", bad, rec+168, crc(bad[rec:rec+168]))
        assert validate(lib, bad)[0] != 0
        # Missing the final LEB implied by FIT totalsize or SquashFS bytes_used fails.
        def volume_id(name):
            table_off = layouts[0] + 4096
            for i in range(128):
                rec = sample[table_off + i*172:table_off + (i+1)*172]
                n = struct.unpack_from(">H", rec, 14)[0]
                if n and rec[16:16+n].decode() == name:
                    return i
            raise AssertionError(name)
        kernel_id = volume_id("kernel")
        rootfs_id = volume_id("ubi_rootfs")
        peb_by_lnum = {}
        for pos in range(0, len(sample), 0x20000):
            vid = pos + 2048
            if struct.unpack_from(">I", sample, vid)[0] == 0x55424921:
                peb_by_lnum[(struct.unpack_from(">I", sample, vid+8)[0],
                             struct.unpack_from(">I", sample, vid+12)[0])] = pos
        kernel_peb = peb_by_lnum[(kernel_id, 0)]
        fit_size = struct.unpack_from(">I", sample, kernel_peb+4096+4)[0]
        kernel_last = (fit_size + 126975) // 126976 - 1
        rootfs_peb = peb_by_lnum[(rootfs_id, 0)]
        squash_bytes = struct.unpack_from("<Q", sample, rootfs_peb+4096+40)[0]
        rootfs_last = (squash_bytes + 126975) // 126976 - 1
        for volid, last_lnum in ((kernel_id, kernel_last), (rootfs_id, rootfs_last)):
            bad = bytearray(sample)
            last_peb = peb_by_lnum[(volid, last_lnum)]
            bad[last_peb+2048:last_peb+2048+64] = b"\xff"*64
            assert validate(lib, bad)[0] != 0
        # Fastmap carries physical mappings incompatible with our bad-block remap and is rejected.
        bad = bytearray(sample)
        free_peb = next(pos for pos in range(0, len(bad), 0x20000)
                        if struct.unpack_from(">I", bad, pos+2048)[0] == 0x55424921 and
                        struct.unpack_from(">I", bad, pos+2048+8)[0] < 128)
        vid = free_peb + 2048
        struct.pack_into(">I", bad, vid+8, 0x7fffffff - 4096 + 1)
        struct.pack_into(">I", bad, vid+60, crc(bad[vid:vid+60]))
        assert validate(lib, bad)[0] != 0
        # A duplicate logical eraseblock number is rejected after recomputing VID CRC.
        ordinary = []
        for pos in range(0, len(sample), 0x20000):
            vid = pos + 2048
            if struct.unpack_from(">I", sample, vid)[0] == 0x55424921:
                vol = struct.unpack_from(">I", sample, vid + 8)[0]
                if vol < 128 and vol != (0x7fffffff - 4096):
                    ordinary.append((pos, vol))
        pair = next((a,b) for ix,a in enumerate(ordinary) for b in ordinary[ix+1:] if a[1] == b[1])
        bad = bytearray(sample)
        srcpos, _ = pair[0]
        dstpos, _ = pair[1]
        srcvid, dstvid = srcpos + 2048, dstpos + 2048
        struct.pack_into(">I", bad, dstvid + 12, struct.unpack_from(">I", bad, srcvid + 12)[0])
        struct.pack_into(">I", bad, dstvid + 60, crc(bad[dstvid:dstvid+60]))
        assert validate(lib, bad)[0] != 0
        # A static-volume data corruption is rejected by its VID data CRC.
        static_pos = next(pos for pos in range(0, len(sample), 0x20000)
                          if sample[pos+2048] == 0x55 and sample[pos+2049] == 0x42 and
                          sample[pos+2053] == 2 and struct.unpack_from(">I", sample, pos+2048+20)[0] > 0)
        bad = bytearray(sample); bad[static_pos + 4096 + 5] ^= 1
        assert validate(lib, bad)[0] != 0
    print("UBI_HOSTTEST_PASS: 16 RT3000 OEM payloads; malformed, incomplete, duplicate, CRC, and fastmap cases")


if __name__ == "__main__":
    main()
