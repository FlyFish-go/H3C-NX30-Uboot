#!/usr/bin/env python3
"""Failure-injection tests for the production persistent-default HTTP callback."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UBOOT = ROOT / "u-boot-2016"
STUBS = {
    "common.h": r'''#ifndef HOST_COMMON_H
#define HOST_COMMON_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64; typedef unsigned long ulong;
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define ARCH_DMA_MINALIGN 64
#define MTD_NANDFLASH 4
#define strlcpy(d,s,n) snprintf((d),(n),"%s",(s))
#endif
''',
    "malloc.h": "#include <stdlib.h>\n",
    "linux/types.h": "#include <stdint.h>\ntypedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;\n",
    "linux/stddef.h": "#include <stddef.h>\n",
    "linux/mtd/mtd.h": r'''#ifndef HOST_LINUX_MTD_H
#define HOST_LINUX_MTD_H
#include <stdint.h>
struct mtd_info;
struct erase_info { struct mtd_info *mtd; uint64_t addr,len; };
int mtd_read(struct mtd_info*,uint64_t,size_t,size_t*,uint8_t*);
int mtd_write(struct mtd_info*,uint64_t,size_t,size_t*,const uint8_t*);
int mtd_erase(struct mtd_info*,struct erase_info*);
#endif
''',
    "nand.h": r'''#ifndef HOST_NAND_H
#define HOST_NAND_H
#include <stdint.h>
struct mtd_info { uint8_t type; uint64_t size; uint32_t erasesize,writesize,oobsize,flags; };
typedef struct mtd_info nand_info_t; extern nand_info_t nand_info[2];
int nand_block_isbad(struct mtd_info*,uint64_t);
#endif
''',
    "ipq_api.h": "int getpart_offset_size(char*,unsigned int*,unsigned int*);\n",
    "rt3000_b_fw_write.h": r'''#include <stdint.h>
struct mtd_info;
int rt3_named_nand_write_window_open(struct mtd_info*,const char*,uint64_t,uint64_t);
int rt3_bootconfig_write_window_open(struct mtd_info*,const char*,uint64_t,uint64_t);
void rt3_fw_write_window_close(void);
int rt3_fw_write_window_close_status(void);
''',
    "asm/errno.h": "#include_next <asm/errno.h>\n",
    "net/httpd.h": r'''#ifndef HOST_HTTPD_H
#define HOST_HTTPD_H
#include <stdint.h>
typedef uint32_t u32;
enum httpd_request_method { HTTP_GET, HTTP_POST };
enum httpd_uri_handler_status { HTTP_CB_NEW, HTTP_CB_RESPONDING, HTTP_CB_CLOSED };
enum httpd_response_status { HTTP_RESP_NONE, HTTP_RESP_STD, HTTP_RESP_CUSTOM };
struct httpd_form_value { const char *name,*data,*filename; size_t size; };
struct httpd_form_values { u32 count; struct httpd_form_value values[5]; };
struct httpd_uri_handler; struct httpd_instance { int unused; };
struct httpd_request { enum httpd_request_method method; const struct httpd_uri_handler *urih; struct httpd_form_values form; };
struct http_response_info { u32 code; const char *content_type; int content_length,connection_close; };
struct httpd_response { enum httpd_response_status status; struct http_response_info info; const char *data; u32 size; void *session_data,*gunzip_buffer; };
typedef void(*httpd_uri_handler_cb)(enum httpd_uri_handler_status,struct httpd_request*,struct httpd_response*);
int httpd_register_uri_handler(struct httpd_instance*,const char*,httpd_uri_handler_cb,struct httpd_uri_handler**);
#endif
''',
}


def main():
    c_source = Path(__file__).with_name("boot_default_transaction_harness.c")
    production = (UBOOT / "failsafe/modules/boot_default_rt3000_b.c").read_text()
    assert "rt3_bootconfig_write_window_open(m, tmpl->name, e->off, e->size)" in production
    assert "rt3_named_nand_write_window_open(m, tmpl->name, e->off, e->size)" in production
    assert "!strcmp(tmpl->name, \"0:BOOTCONFIG\") && off != 0x100000" in production
    assert "!strcmp(tmpl->name, \"0:BOOTCONFIG1\") && off != 0x140000" in production
    assert "answer(response, 403" in production and "readonly_profile" in production
    assert r'\"can_change\":false' in production
    with tempfile.TemporaryDirectory(prefix="boot-default-hosttest-") as temp:
        stub_root = Path(temp) / "stubs"
        for rel, content in STUBS.items():
            path = stub_root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        profiles = {
            "B": ["-DCONFIG_H3C_DEFAULT_WEB"],
            "C_PRODUCTION": ["-DCONFIG_H3C_HW_C_PRODUCTION_PORT"],
            "C_READONLY": ["-DCONFIG_H3C_HW_C_RAM_READONLY"],
            "D_PRODUCTION": ["-DCONFIG_H3C_HW_D_PRODUCTION_PORT", "-DCONFIG_H3C_DEFAULT_WEB"],
        }
        for profile, defines in profiles.items():
            exe = Path(temp) / f"boot-default-{profile.lower()}"
            subprocess.run([
                "cc", "-std=c11", "-D_GNU_SOURCE", "-DRT3_BOOTCFG_HOST_TEST", *defines,
                "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                "-include", "stdint.h",
                "-I", str(stub_root), "-I", str(UBOOT / "test/rt3000_b_layout_stubs"),
                "-I", str(UBOOT / "test"), "-idirafter", str(UBOOT / "include"),
                "-I", str(UBOOT / "failsafe/modules"), str(c_source), "-o", str(exe)
            ], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
