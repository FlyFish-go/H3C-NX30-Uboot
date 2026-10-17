#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = (root / "failsafe/modules/metadata_rt3000_b.c").read_text()
sig = "static int data_ok(const void*p,u32 n)"
start = src.index(sig)
brace = src.index("{", start)
depth = 0
for end in range(brace, len(src)):
    if src[end] == "{": depth += 1
    elif src[end] == "}":
        depth -= 1
        if depth == 0:
            function = src[start:end + 1]
            break
else:
    raise AssertionError("data_ok function unterminated")
code = r'''#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
#define MD_RAM_PREFIX_MAX (64UL * 1024)
#define RT3_MD_APPSBL_SIZE 0x00140000U
static uintptr_t upload_base;
uintptr_t httpd_upload_buffer_base(void) { return upload_base; }
''' + function + r'''
#define C(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
 upload_base=0x50000000UL;
 C(data_ok((void *)(upload_base+0x9c),526916));
 C(data_ok((void *)(upload_base+0xffff),RT3_MD_APPSBL_SIZE));
 C(!data_ok((void *)(upload_base-1),1));
 C(!data_ok((void *)(upload_base+0x10000),1));
 C(!data_ok((void *)(upload_base+0x9c),RT3_MD_APPSBL_SIZE+1));
 C(!data_ok((void *)(upload_base+0xffff),RT3_MD_APPSBL_SIZE+1));
 upload_base=0x41000000UL;
 C(data_ok((void *)(upload_base+0x9c),526916));
 C(!data_ok((void *)(upload_base+0x10000),1));
 puts("HTTP metadata upload range: 512 MiB staging and bounds PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "test"
    subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-x","c","-","-o",str(exe)],input=code,text=True,check=True)
    subprocess.run([str(exe)],check=True)
