#!/usr/bin/env python3
"""Exercise the real RT3000-B HTTP callbacks with a stub MTD and real OEM UBI data."""
import ctypes
import hashlib
import importlib.util
import json
import os
from datetime import datetime, timezone
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "u-boot-2016/failsafe/modules/firmware_rt3000_b.c"
OEM_HEADER = ROOT / "u-boot-2016/failsafe/modules/firmware_oem_catalog.h"
CATALOG = Path(os.environ.get("RT3000_OEM_CATALOG", str(ROOT / "doc/RT3000-B-OEM-CATALOG-20261008.json")))
UBI_TEST = Path(__file__).with_name("firmware_rt3000_b_ubi_hosttest.py")
EVIDENCE = Path(os.environ.get("RT3000_B_HANDLER_EVIDENCE_OUT", "handler-hosttest.json"))
UPLOAD_ADDR = 0x44000000
UPLOAD_SIZE = 80 * 1024 * 1024 + 64 * 1024
UPLOAD_ARRAY = None
UPLOAD_HOLDER = None

SHIM = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <openssl/sha.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef unsigned long ulong;
#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define ARCH_DMA_MINALIGN 64
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SZ_1M (1024U*1024U)
#define SZ_64K (64U*1024U)
#define SHA256_SUM_LEN 32
#define CHUNKSZ_SHA256 4096
#define MTD_WRITEABLE 0x400
#define MTD_NANDFLASH 4
#define UBI_EC_HDR_MAGIC 0x55424923U
#define UBI_VID_HDR_MAGIC 0x55424921U
#define UBI_VERSION 1
#define UBI_CRC32_INIT 0xffffffffU
#define UBI_VID_HDR_SIZE 64
#define UBI_VID_DYNAMIC 1
#define UBI_VID_STATIC 2
#define UBI_COMPAT_REJECT 5
#define UBI_FM_SB_VOLUME_ID (0x7fffffffU - 4096U + 1)
#define UBI_FM_DATA_VOLUME_ID (0x7fffffffU - 4096U + 2)
#define UBI_VOL_NAME_MAX 127
#define HTTP_CB_NEW 0
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_RESP_STD 0
#define RT3_LAYOUT_MAX_PARTITIONS 16
#define RT3_LAYOUT_PARTITION_NAME_LEN 16
#define RT3_LAYOUT_UNSUPPORTED 0
#define RT3_LAYOUT_DUAL 1
#define RT3_LAYOUT_SINGLE 2
struct rt3_partition { char name[16]; unsigned int display_index; u64 offset, size; };
struct rt3_layout { int mode; unsigned int count; struct rt3_partition parts[16]; u32 fingerprint; };
struct mtd_info { u64 size; u32 writesize, erasesize, flags, type; };
struct erase_info { struct mtd_info *mtd; u64 addr, len, fail_addr; int scrub; };
struct httpd_form_value { const char *name; char *data; size_t size; };
struct httpd_form { unsigned int count; struct httpd_form_value *values; };
struct httpd_request { int method; struct httpd_form form; };
struct httpd_response_info { int code, connection_close; const char *content_type; };
struct httpd_response { int status; struct httpd_response_info info; const char *data; size_t size; };
struct httpd_instance { int unused; };
enum httpd_uri_handler_status { host_uri_status=0 };
typedef void (*host_uri_cb)(enum httpd_uri_handler_status, struct httpd_request *, struct httpd_response *);
struct host_uri { char path[64]; host_uri_cb cb; };
static struct host_uri host_uris[8]; static unsigned int host_uri_count;
static struct httpd_instance host_inst;
static struct mtd_info nand_info[1];
static unsigned char *host_flash;
static u64 host_flash_size = 128ULL*1024*1024;
static struct rt3_layout host_layout;
static int host_layout_reads, host_layout_invalidates, host_slot_invalidates;
static int host_window_open, host_window_opens, host_window_closes;
static u32 host_saved_flags;
static u64 host_touched_min, host_touched_max;
static int host_erase_count, host_write_count, host_read_count, host_fail_readback, host_ff_page_writes;
static u64 host_write_min, host_write_max, host_erase_min, host_erase_max;
static u8 host_sha_buf[SHA256_SUM_LEN];
static int get_unaligned_be32(const void *x) { const u8 *p=x; return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3]; }
static u16 get_unaligned_be16(const void *x) { const u8 *p=x; return ((u16)p[0]<<8)|p[1]; }
static u32 crc32(u32 seed, const unsigned char *p, size_t n) { size_t i; int b; u32 c=seed; for(i=0;i<n;i++){ c^=p[i]; for(b=0;b<8;b++) c=(c>>1)^(0xedb88320U & (0U-(c&1U))); } return c; }
static void sha256_csum_wd(const unsigned char *p, unsigned int n, unsigned char *d, unsigned int chunk) { (void)chunk; SHA256(p,n,d); }
static size_t strlcpy(char *d,const char *s,size_t n){size_t l=strlen(s);if(n){size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l;}
static size_t strlcat(char *d,const char *s,size_t n){size_t a=strlen(d),b=strlen(s);if(a<n){size_t c=b<n-a-1?b:n-a-1;memcpy(d+a,s,c);d[a+c]=0;}return a+b;}
static const char *rt3_layout_mode_name(int mode) { return mode==RT3_LAYOUT_DUAL?"dual":mode==RT3_LAYOUT_SINGLE?"single":NULL; }
static int rt3_layout_read(struct rt3_layout *out){host_layout_reads++;*out=host_layout;return host_layout.mode==RT3_LAYOUT_UNSUPPORTED?-EINVAL:0;}
static void rt3_layout_invalidate_cache(void){host_layout_invalidates++;}
static const struct rt3_partition *rt3_layout_find(const struct rt3_layout *l,const char *n,const struct rt3_partition **out){unsigned i;for(i=0;i<l->count;i++)if(!strcmp(l->parts[i].name,n)){*out=&l->parts[i];return 0;}return -ENOENT;}
static void slots_rt3000_b_invalidate_cache(void){host_slot_invalidates++;}
static struct httpd_form_value *httpd_request_find_value(struct httpd_request *r,const char *n){unsigned i;for(i=0;i<r->form.count;i++)if(!strcmp(r->form.values[i].name,n))return &r->form.values[i];return NULL;}
static void httpd_register_uri_handler(struct httpd_instance *i,const char *p,host_uri_cb cb,void *arg){(void)i;(void)arg;strlcpy(host_uris[host_uri_count].path,p,sizeof(host_uris[host_uri_count].path));host_uris[host_uri_count++].cb=cb;}
static u64 host_bad_off=~(u64)0;
static int nand_block_isbad(struct mtd_info *m,u64 off){(void)m;return off==host_bad_off;}
static int mtd_erase(struct mtd_info *m,struct erase_info *e){if(!host_window_open||!(m->flags&MTD_WRITEABLE)||e->addr<host_erase_min||e->addr+e->len>host_erase_max)return -EROFS;memset(host_flash+e->addr,0xff,e->len);if(e->addr<host_touched_min)host_touched_min=e->addr;if(e->addr+e->len>host_touched_max)host_touched_max=e->addr+e->len;host_erase_count++;return 0;}
static void *memchr_inv(const void *s,int c,size_t n){const u8 *p=s;size_t i;for(i=0;i<n;i++)if(p[i]!=(u8)c)return (void *)(p+i);return NULL;}
static int mtd_write(struct mtd_info *m,u64 off,size_t n,size_t *done,const u8 *p){size_t i;if(!host_window_open||!(m->flags&MTD_WRITEABLE)||off<host_write_min||off+n>host_write_max)return -EROFS;for(i=0;i+m->writesize<=n;i+=m->writesize)if(!memchr_inv(p+i,0xff,m->writesize))host_ff_page_writes++;memcpy(host_flash+off,p,n);if(off<host_touched_min)host_touched_min=off;if(off+n>host_touched_max)host_touched_max=off+n;*done=n;host_write_count++;return 0;}
static int mtd_read(struct mtd_info *m,u64 off,size_t n,size_t *done,u8 *p){(void)m;if(off+n>host_flash_size)return -EINVAL;memcpy(p,host_flash+off,n);*done=n;host_read_count++;if(host_fail_readback&&host_read_count==host_fail_readback)p[0]^=1;return 0;}
static int rt3_fw_write_window_open(struct mtd_info *m,u64 off,u64 size){unsigned i;int found=0;if(host_window_open||m!=&nand_info[0])return -EROFS;rt3_layout_invalidate_cache();if(rt3_layout_read(&host_layout))return -EROFS;for(i=0;i<host_layout.count;i++)if((!strcmp(host_layout.parts[i].name,"rootfs")||!strcmp(host_layout.parts[i].name,"rootfs_1"))&&host_layout.parts[i].offset==off&&host_layout.parts[i].size==size)found++;if(found!=1)return -EROFS;host_saved_flags=m->flags;host_window_open=1;host_window_opens++;m->flags|=MTD_WRITEABLE;host_erase_min=host_write_min=off;host_erase_max=host_write_max=off+size;return 0;}
static void rt3_fw_write_window_close(void){if(host_window_open){nand_info[0].flags=host_saved_flags;host_window_open=0;host_window_closes++;}}
static int rt3_fw_write_window_allows(struct mtd_info *m,u64 off,u64 size){return host_window_open&&m==&nand_info[0]&&off>=host_write_min&&off+size<=host_write_max;}
extern int fdt_check_header(const void *); static u32 fdt_totalsize(const void *p){return (u32)get_unaligned_be32((const u8 *)p+4);} extern int fdt_path_offset(const void *,const char *); extern const void *fdt_getprop(const void *,int,const char *,int *);
"""

WRAPPERS = r"""
struct host_field { const char *name; const unsigned char *data; size_t size; };
void host_reset(int mode) {
    host_bad_off=~(u64)0;
    memset(&host_layout,0,sizeof(host_layout)); host_layout.mode=mode;
    if(mode==RT3_LAYOUT_DUAL){host_layout.count=2;strlcpy(host_layout.parts[0].name,"rootfs",16);host_layout.parts[0].display_index=16;host_layout.parts[0].offset=0x900000;host_layout.parts[0].size=40ULL*1024*1024;strlcpy(host_layout.parts[1].name,"rootfs_1",16);host_layout.parts[1].display_index=17;host_layout.parts[1].offset=0x3100000;host_layout.parts[1].size=40ULL*1024*1024;}
    else if(mode==RT3_LAYOUT_SINGLE){host_layout.count=1;strlcpy(host_layout.parts[0].name,"rootfs",16);host_layout.parts[0].display_index=16;host_layout.parts[0].offset=0x900000;host_layout.parts[0].size=80ULL*1024*1024;}
    host_layout.fingerprint=0x12345678;
    nand_info[0].size=host_flash_size;nand_info[0].writesize=2048;nand_info[0].erasesize=131072;nand_info[0].type=MTD_NANDFLASH;nand_info[0].flags=0x80;
    if(!host_flash)host_flash=malloc(host_flash_size);
    host_uri_count=0;host_layout_reads=host_layout_invalidates=host_slot_invalidates=0;
    host_window_open=host_window_opens=host_window_closes=0;host_saved_flags=0;host_touched_min=~(u64)0;host_touched_max=0;host_erase_count=host_write_count=host_read_count=host_fail_readback=host_ff_page_writes=0;
    memset(&fw_pending,0,sizeof(fw_pending));
    firmware_rt3000_b_register_uri_handlers(&host_inst);
}
void host_set_fingerprint(unsigned int value){host_layout.fingerprint=value;}
void host_fail_readback_at(int n){host_fail_readback=n;}
int host_dispatch(const char *path,int method,const struct host_field *fields,unsigned int count,int *code,char *out,size_t cap){
    struct httpd_request req; struct httpd_response resp; struct httpd_form_value vals[8]; unsigned i;
    memset(&req,0,sizeof(req));memset(&resp,0,sizeof(resp));req.method=method;req.form.count=count;req.form.values=vals;
    for(i=0;i<count;i++){vals[i].name=fields[i].name;vals[i].data=(char *)fields[i].data;vals[i].size=fields[i].size;}
    for(i=0;i<host_uri_count;i++)if(!strcmp(path,host_uris[i].path)){host_uris[i].cb(HTTP_CB_NEW,&req,&resp);*code=resp.info.code;if(resp.data){size_t n=resp.size<cap-1?resp.size:cap-1;memcpy(out,resp.data,n);out[n]=0;}else if(cap)out[0]=0;return 0;}
    *code=404;if(cap)out[0]=0;return -ENOENT;
}
void host_clear_target(const char *name){const struct rt3_partition *p=NULL;rt3_layout_find(&host_layout,name,&p);if(p)memset(host_flash+p->offset,0xff,p->size);}
unsigned int host_reserve_margin_get(void){return fw_reserve_margin(&nand_info[0]);}
unsigned int host_erase_count_get(void){return host_erase_count;}
unsigned int host_write_count_get(void){return host_write_count;}
unsigned int host_ff_page_writes_get(void){return host_ff_page_writes;}
void host_set_bad_block(u64 off){host_bad_off=off;}
int host_flash_equals(u64 off,const u8 *p,size_t n){return off+n<=host_flash_size&&!memcmp(host_flash+off,p,n);}
int host_flash_all_ff(u64 off,size_t n){size_t i;for(i=0;i<n;i++)if(host_flash[off+i]!=0xff)return 0;return 1;}
unsigned int host_read_count_get(void){return host_read_count;}
unsigned int host_window_closes_get(void){return host_window_closes;}
unsigned int host_slot_invalidates_get(void){return host_slot_invalidates;}
unsigned int host_layout_invalidates_get(void){return host_layout_invalidates;}
unsigned int host_mtd_flags_get(void){return nand_info[0].flags;}
u64 host_touched_min_get(void){return host_touched_min;}
u64 host_touched_max_get(void){return host_touched_max;}
int host_window_is_open(void){return host_window_open;}
"""


class HostField(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("data", ctypes.c_void_p), ("size", ctypes.c_size_t)]


def build_test_lib(tmp):
    src = MODULE.read_text()
    src = re.sub(r"^#include\s+.*$", "", src, flags=re.M)
    # The real production allowlist is embedded verbatim in the generated test TU.
    catalog = OEM_HEADER.read_text()
    core_header = ROOT / "u-boot-2016/include/rt3000_b_fw_core.h"
    core_src = (ROOT / "u-boot-2016/failsafe/modules/firmware_rt3000_b_core.c").read_text()
    core_src = re.sub(r"^#include\s+.*$", "", core_src, flags=re.M)
    # QSDK FIT unwrap and platform notice live in their own production files.
    extra_headers = "".join("\n#include \"%s\"" % (ROOT / "u-boot-2016/include" / h)
                            for h in ("rt3_fw_fit_ubi.h", "rt3_fw_platform.h"))
    extra_objs = []
    for f in ("firmware_fit_ubi.c", "firmware_platform.c"):
        obj = Path(tmp) / (f[:-2] + ".o")
        subprocess.run(["gcc", "-c", "-fPIC", "-O1", "-std=gnu99", "-DRT3_FW_HOST_TEST", "-DUSE_HOSTCC",
                        "-I" + str(ROOT / "u-boot-2016/tools"), "-idirafter", str(ROOT / "u-boot-2016/include"),
                        "-o", str(obj), str(ROOT / "u-boot-2016/failsafe/modules" / f)], check=True)
        extra_objs.append(str(obj))
    test_src = (SHIM + "\n#include \"" + str(core_header) + "\"" + extra_headers + "\n" + catalog + "\n" +
                src + "\n" + core_src + "\n" + WRAPPERS)
    cfile = Path(tmp) / "http_handlers.c"
    sofile = Path(tmp) / "http_handlers.so"
    cfile.write_text(test_src)
    subprocess.run(["gcc", "-shared", "-fPIC", "-O1", "-w", "-DRT3_FW_HOST_TEST",
                    str(cfile), *extra_objs,
                    "-Wl,-l:libfdt.so.1", "-lcrypto", "-o", str(sofile)], check=True)
    lib = ctypes.CDLL(str(sofile))
    lib.host_reset.argtypes = [ctypes.c_int]
    lib.host_dispatch.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(HostField), ctypes.c_uint,
                                  ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_size_t]
    lib.host_dispatch.restype = ctypes.c_int
    return lib


def map_upload_window():
    global UPLOAD_ARRAY, UPLOAD_HOLDER
    libc = ctypes.CDLL(None, use_errno=True)
    libc.mmap.restype = ctypes.c_void_p
    libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
    prot_rw = 0x1 | 0x2
    map_private = 0x02
    map_anon = 0x20
    map_fixed_noreplace = 0x100000
    result = libc.mmap(ctypes.c_void_p(UPLOAD_ADDR), UPLOAD_SIZE, prot_rw,
                       map_private | map_anon | map_fixed_noreplace, -1, 0)
    if result in (None, ctypes.c_void_p(-1).value) or result != UPLOAD_ADDR:
        raise RuntimeError("cannot reserve exact staged upload window at 0x44000000: errno=%d" % ctypes.get_errno())
    UPLOAD_HOLDER = (libc, result)
    UPLOAD_ARRAY = (ctypes.c_ubyte * UPLOAD_SIZE).from_address(UPLOAD_ADDR)


def dispatch(lib, path, method, fields=(), stage_upload=True):
    buffers = []
    array = (HostField * len(fields))()
    for i, (name, raw) in enumerate(fields):
        if isinstance(raw, str): raw = raw.encode()
        if name == "firmware" and stage_upload:
            if len(raw) > UPLOAD_SIZE:
                raise ValueError("test fixture exceeds staged RAM reservation")
            ctypes.memmove(UPLOAD_ADDR, raw, len(raw))
            buf = UPLOAD_ARRAY
            data_ptr = UPLOAD_ADDR
        else:
            buf = ctypes.create_string_buffer(raw)
            data_ptr = ctypes.cast(buf, ctypes.c_void_p).value
        buffers.append(buf)
        array[i] = HostField(name.encode(), data_ptr, len(raw))
    code = ctypes.c_int()
    out = ctypes.create_string_buffer(2048)
    lib.host_dispatch(path.encode(), method, array, len(fields), ctypes.byref(code), out, len(out))
    return code.value, out.value.decode(errors="replace"), buffers


def without_oem_volumes(ubi):
    """Rename the OEM-only wifi_fw/bt_fw volumes so the image reads as a third-party system."""
    import struct, zlib
    out = bytearray(ubi)
    for peb in range(0, len(out), 0x20000):
        if out[peb:peb+4] != b"UBI#":
            continue
        vid = peb + struct.unpack(">I", out[peb+16:peb+20])[0]
        data = peb + struct.unpack(">I", out[peb+20:peb+24])[0]
        if out[vid:vid+4] != b"UBI!" or struct.unpack(">I", out[vid+8:vid+12])[0] != 0x7fffefff:
            continue
        for r in range(128):
            rec = data + r * 172
            for old, new in ((b"wifi_fw\0", b"wifi_qs\0"), (b"bt_fw\0", b"bt_qs\0")):
                if out[rec+16:rec+16+len(old)] == old:
                    out[rec+16:rec+16+len(new)] = new
                    crc = zlib.crc32(bytes(out[rec:rec+168])) ^ 0xffffffff
                    out[rec+168:rec+172] = struct.pack(">I", crc)
    assert out != bytearray(ubi)
    return bytes(out)

def token_from(response):
    value = json.loads(response)
    return value["token"]


def run():
    map_upload_window()
    spec = importlib.util.spec_from_file_location("ubi_test", UBI_TEST)
    ubi_test = importlib.util.module_from_spec(spec); spec.loader.exec_module(ubi_test)
    entries = json.loads(CATALOG.read_text())["entries"]
    rt = [e for e in entries if e["model"] == "RT3000"]
    assert len(rt) == 16
    # One allowlist on every board: all 47 packages of all four models.
    oem = [e for e in entries if e["model"] in ("NX30", "RC3000", "RT3000", "RW3000")]
    assert len(oem) == 47
    evidence = {"schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
                "sample": "RT3000-B source-only host callback harness; no device/network/flash operation",
                "staging_window": {"address": UPLOAD_ADDR, "bytes": UPLOAD_SIZE},
                "production_module": str(MODULE), "catalog": str(CATALOG),
                "result": "running", "reserve_margin_pebs": None,
                "get_layout": None, "oem_validate": [], "negative_callback_cases": [],
                "raw_validate": None, "mock_commit": None, "mock_readback_failure": None}
    with tempfile.TemporaryDirectory(prefix="rt3-handler-hosttest-") as td:
        lib = build_test_lib(td)
        lib.host_reset(1)
        evidence["reserve_margin_pebs"] = int(lib.host_reserve_margin_get())
        assert evidence["reserve_margin_pebs"] == 4
        code, body, _ = dispatch(lib, "/firmware/layout", 1)
        obj = json.loads(body)
        assert code == 200 and obj["status"] == "ok" and obj["mode"] == "dual"
        assert obj["write_supported"] is True and obj["ptable_write_supported"] is True
        assert [(p["name"],p["offset"],p["size"]) for p in obj["partitions"]] == [
            ("rootfs",0x900000,40*1024*1024),
            ("rootfs_1",0x3100000,40*1024*1024)]
        assert all("mtd" not in p for p in obj["partitions"])
        evidence["get_layout"] = {"http_status": code, "json": obj}
        assert dispatch(lib,"/firmware/layout",2)[0] == 405
        assert dispatch(lib,"/firmware/validate",1)[0] == 405

        # Every exact OEM package passes through the actual validate callback, including target capacity.
        for entry in oem:
            package = Path(entry["package_paths"][0]).read_bytes()
            fields = [("firmware",package),("target","rootfs_1"),("mode","dual")]
            code, body, keep = dispatch(lib,"/firmware/validate",2,fields)
            response = json.loads(body)
            assert code == 200 and response["status"] == "ok", (entry["release"], code, body)
            assert response["target"] == "rootfs_1" and response["mode"] == "dual"
            assert response["format"] == "h3c_rt3000_oem_ubi"
            assert response["firmware_identity"] == entry["model"] + " " + entry["release"]
            assert response["sha256"] == entry["package_sha256"]
            evidence["oem_validate"].append({"release": entry["release"], "http_status": code,
                "bytes": response["bytes"], "sha256": response["sha256"],
                "format": response["format"], "firmware_identity": response["firmware_identity"],
                "target": response["target"], "mode": response["mode"]})
            # A later validate always invalidates the previous token, even if this request fails.
            oldtoken = response["token"]
            badfields = [("firmware",b"bad"),("target","rootfs_1"),("mode","dual")]
            badcode, badbody, badkeep = dispatch(lib,"/firmware/validate",2,badfields)
            assert badcode == 422 and json.loads(badbody)["error"] == "unsupported firmware format"
            evidence["negative_callback_cases"].append({"case": "new_invalid_validate_invalidates_prior_token", "http_status": badcode})
            writefields = [("token",oldtoken),("target","rootfs_1"),("mode","dual")]
            assert dispatch(lib,"/firmware/write",2,writefields)[0] == 409
            assert lib.host_erase_count_get() == 0

        # An H3C package whose exact bytes are not listed gets its own error, not "invalid UBI".
        tampered = bytearray(Path(next(e for e in entries if e["model"] == "RC3000" and e["release"] == "R013L01")["package_paths"][0]).read_bytes())
        tampered[-1] ^= 0xff
        code, body, _ = dispatch(lib,"/firmware/validate",2,[("firmware",bytes(tampered)),("target","rootfs_1"),("mode","dual")])
        assert code == 422 and json.loads(body)["error"] == "unknown H3C OEM package", (code, body)
        assert lib.host_erase_count_get() == 0
        evidence["negative_callback_cases"].append({"case": "unlisted_h3c_package", "http_status": code})

        # The staging window is mandatory; heap-backed direct pointers are rejected before parsing.
        ubi = ubi_test.extract_ubi(next(e for e in rt if e["release"] == "R012"))
        outside_code, outside_body, _ = dispatch(lib,"/firmware/validate",2,
            [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")], stage_upload=False)
        assert outside_code == 413 and lib.host_erase_count_get() == 0
        evidence["negative_callback_cases"].append({"case": "non_staged_pointer_rejected", "http_status": outside_code})
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        response=json.loads(body); assert code==200 and response["firmware_identity"]=="unidentified"
        assert response["format"]=="ubi_unidentified" and response["bytes"]==len(ubi)
        evidence["raw_validate"] = {k:v for k,v in response.items() if k != "token"}
        digest=response["sha256"]; token=response["token"]
        assert digest==hashlib.sha256(ubi).hexdigest()
        # Wrong target/mode/table, altered staged bytes, duplicates, embedded NUL, and malformed token all fail before erase.
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs"),("mode","dual")])[0] == 409
        assert lib.host_erase_count_get()==0
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs_1"),("mode","dual")])[0] == 409
        assert dispatch(lib,"/firmware/write",2,[("token",token),("token",token),("target","rootfs_1"),("mode","dual")])[0] == 400
        assert dispatch(lib,"/firmware/write",2,[("token",token+"x"),("target","rootfs_1"),("mode","dual")])[0] == 400
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs_1"),("mode","dual\0x")])[0] == 400
        assert lib.host_erase_count_get()==0
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        token=token_from(body)
        assert dispatch(lib,"/firmware/write",2,
                        [("token",token),("target","rootfs_1"),("mode","single")])[0] == 409
        assert lib.host_erase_count_get()==0
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        token=token_from(body)
        keep[0][0] = ord("X")
        assert dispatch(lib,"/firmware/write",2,
                        [("token",token),("target","rootfs_1"),("mode","dual")])[0] == 409
        assert lib.host_erase_count_get()==0

        # Failed validate clears the previous token; duplicate and binary text fields are rejected.
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        token=token_from(body)
        assert dispatch(lib,"/firmware/validate",2,
                        [("firmware",bytes(ubi)),("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])[0] == 400
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs_1"),("mode","dual")])[0] == 409
        assert dispatch(lib,"/firmware/validate",2,
                        [("firmware",bytes(ubi)),("target",b"rootfs_1\0x"),("mode","dual")])[0] == 400
        assert lib.host_erase_count_get()==0

        # Layout fingerprint or staged-byte change consumes the token and cannot touch NAND.
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        token=token_from(body)
        lib.host_set_fingerprint(0x98765432)
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs_1"),("mode","dual")])[0] == 409
        assert lib.host_erase_count_get()==0
        lib.host_reset(1)
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        token=token_from(body)
        # A subsequent failed validate overwrites/invalidates the old staging buffer/token.
        assert dispatch(lib,"/firmware/validate",2,[("firmware",b"bad"),("target","rootfs_1"),("mode","dual")])[0] == 422
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs_1"),("mode","dual")])[0] == 409
        assert lib.host_erase_count_get()==0

        # Single layout exposes one rootfs and permits only that target; tables are written from the PTABLE page.
        lib.host_reset(2)
        code, body, _ = dispatch(lib,"/firmware/layout",1)
        obj=json.loads(body); assert code==200 and obj["mode"]=="single" and obj["ptable_write_supported"] is True
        assert len(obj["partitions"])==1 and obj["partitions"][0]["name"]=="rootfs"
        assert dispatch(lib,"/firmware/validate",2,
                        [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","single")])[0] == 409
        # OEM firmware (wifi_fw/bt_fw volumes) is refused in the single layout.
        code, body, _ = dispatch(lib,"/firmware/validate",2,
                                 [("firmware",bytes(ubi)),("target","rootfs"),("mode","single")])
        assert code==422 and json.loads(body)["error"]=="oem_firmware_needs_dual_layout"
        third_party = without_oem_volumes(ubi)
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",third_party),("target","rootfs"),("mode","single")])
        assert code==200
        single_token=token_from(body)
        assert dispatch(lib,"/firmware/validate",2,
                        [("firmware",b"bad"),("target","rootfs"),("mode","single")])[0] == 422
        assert dispatch(lib,"/firmware/write",2,
                        [("token",single_token),("target","rootfs"),("mode","single")])[0] == 409
        assert lib.host_erase_count_get()==0

        # Successful actual callback commit and readback, bounded to the selected rootfs_1 extent.
        lib.host_reset(1)
        lib.host_clear_target.argtypes=[ctypes.c_char_p]; lib.host_clear_target(b"rootfs_1")
        code, body, keep = dispatch(lib,"/firmware/validate",2,
                                    [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        token=token_from(body)
        code, body, keep2 = dispatch(lib,"/firmware/write",2,
                                     [("token",token),("target","rootfs_1"),("mode","dual")])
        result=json.loads(body)
        assert code==200 and result["verified"] is True and result["auto_reboot"] is False
        assert result["boot_select_changed"] is False and result["env_changed"] is False and result["ptable_changed"] is False
        # All-0xff pages stay erased; every other page is written once.
        data_pages=sum(1 for o in range(0,len(ubi),2048) if ubi[o:o+2048]!=b"\xff"*2048)
        assert lib.host_erase_count_get()==320 and lib.host_write_count_get()==data_pages
        assert lib.host_ff_page_writes_get()==0
        assert lib.host_window_closes_get()==1 and lib.host_window_is_open()==0 and lib.host_mtd_flags_get()==0x80
        assert lib.host_slot_invalidates_get()==1
        lib.host_touched_min_get.restype=ctypes.c_uint64; lib.host_touched_max_get.restype=ctypes.c_uint64
        assert lib.host_touched_min_get()==0x3100000
        assert lib.host_touched_max_get()==0x3100000+40*1024*1024
        evidence["mock_commit"] = {"http_status": code, "result": result,
            "erase_blocks": int(lib.host_erase_count_get()), "written_pages": int(lib.host_write_count_get()),
            "read_calls": int(lib.host_read_count_get()), "touched_min": int(lib.host_touched_min_get()),
            "touched_max": int(lib.host_touched_max_get()), "mtd_flags_restored": int(lib.host_mtd_flags_get()),
            "window_closed": not bool(lib.host_window_is_open()), "slot_cache_invalidations": int(lib.host_slot_invalidates_get())}
        erase_before=lib.host_erase_count_get()
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs_1"),("mode","dual")])[0]==409
        assert lib.host_erase_count_get()==erase_before

        # A full-partition image (padded with free, EC-only PEBs to 320) still fits a
        # target with one bad block: one free PEB is dropped and the bad block is skipped.
        PEB = 131072
        free_peb = bytes(ubi[:64]) + b"\xff" * (PEB - 64)
        full = bytes(ubi) + free_peb * (320 - len(ubi) // PEB)
        bad = 0x3100000 + 5 * PEB
        lib.host_reset(1); lib.host_clear_target(b"rootfs_1")
        lib.host_set_bad_block.argtypes = [ctypes.c_uint64]
        lib.host_set_bad_block(bad)
        code, body, keep = dispatch(lib, "/firmware/validate", 2,
                                    [("firmware", full), ("target", "rootfs_1"), ("mode", "dual")])
        assert code == 200, body
        token = token_from(body)
        code, body, keep2 = dispatch(lib, "/firmware/write", 2,
                                     [("token", token), ("target", "rootfs_1"), ("mode", "dual")])
        assert code == 200 and json.loads(body)["verified"] is True, body
        assert lib.host_erase_count_get() == 319
        lib.host_flash_equals.argtypes = [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_size_t]
        lib.host_flash_all_ff.argtypes = [ctypes.c_uint64, ctypes.c_size_t]
        assert lib.host_flash_equals(0x3100000, full[:5 * PEB], 5 * PEB)
        assert lib.host_flash_all_ff(bad, PEB), "bad block must not be touched"
        assert lib.host_flash_equals(bad + PEB, full[5 * PEB:319 * PEB], 314 * PEB)
        assert lib.host_ff_page_writes_get() == 0
        evidence["mock_full_image_one_bad_block"] = {"http_status": code, "erase_blocks": int(lib.host_erase_count_get())}

        # A readback mismatch returns failure, consumes token, and still closes gate/invalidates cache.
        lib.host_reset(1); lib.host_clear_target(b"rootfs_1"); lib.host_fail_readback_at(1)
        code, body, keep=dispatch(lib,"/firmware/validate",2,
                                  [("firmware",bytes(ubi)),("target","rootfs_1"),("mode","dual")])
        token=token_from(body)
        code, body, keep2=dispatch(lib,"/firmware/write",2,
                                   [("token",token),("target","rootfs_1"),("mode","dual")])
        failed=json.loads(body)
        assert code==500 and failed["target"]=="rootfs_1" and failed["mode"]=="dual"
        evidence["mock_readback_failure"] = {"http_status": code, "result": failed,
            "window_closed": not bool(lib.host_window_is_open()),
            "mtd_flags_restored": int(lib.host_mtd_flags_get()),
            "slot_cache_invalidations": int(lib.host_slot_invalidates_get()),
            "erase_blocks_before_failure": int(lib.host_erase_count_get()),
            "written_pages_before_failure": int(lib.host_write_count_get())}
        assert lib.host_window_closes_get()==1 and lib.host_window_is_open()==0
        assert lib.host_mtd_flags_get()==0x80
        assert lib.host_slot_invalidates_get()==1
        assert dispatch(lib,"/firmware/write",2,[("token",token),("target","rootfs_1"),("mode","dual")])[0]==409
    evidence["result"] = "PASS"
    evidence["negative_cases_passed"] = ["wrong_target", "wrong_mode", "staged_bytes_changed", "layout_fingerprint_changed",
        "failed_validate_invalidates_token", "duplicate_file_field", "embedded_nul_text", "duplicate_token_field",
        "malformed_trailing_token", "single_layout_wrong_target", "one_shot_replay", "readback_failure_consumes_token"]
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    print("HANDLER_HOSTTEST_PASS: GET/validate/write callbacks, 47 OEM packages (NX30/RC3000/RT3000/RW3000), unlisted H3C package error, one-shot/layout guards, commit and readback failure")
    print("EVIDENCE: " + str(EVIDENCE))


if __name__ == "__main__":
    run()
