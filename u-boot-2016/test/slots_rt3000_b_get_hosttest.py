#!/usr/bin/env python3
"""Exercise the production /boot/slots GET cache decision."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "u-boot-2016/failsafe/modules/slots_rt3000_b.c"


def main():
    source = SOURCE.read_text()
    compare = source[source.index("static int same_layout("):source.index("static void scan_slots(")]
    handler = source[source.index("static void boot_slots_handler("):source.index("void slots_rt3000_b_register_uri_handlers(")]
    assert "rt3_layout_read(&layout)" in handler
    assert "same_layout(&scanned_layout, &layout)" in handler
    c = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SLOT_N 2
#define RT3_LAYOUT_MAX_PARTITIONS 2
#define RT3_LAYOUT_DUAL 1
#define RT3_LAYOUT_UNSUPPORTED 0
#define HTTP_CB_NEW 1
#define HTTP_RESP_STD 1
#define HTTP_GET 1
typedef uint64_t u64; typedef uint32_t u32;
struct part { char name[16]; unsigned int display_index; u64 offset,size; };
struct rt3_layout { int mode; unsigned int count; struct part parts[2]; u32 fingerprint; };
struct slot_info { int unused; };
struct httpd_request { int method; };
struct ri { int code,connection_close; const char *content_type; };
struct httpd_response { int status; struct ri info; const char *data; size_t size; };
enum httpd_uri_handler_status { OTHER=0 };
static struct rt3_layout scanned_layout,live_layout;
static struct slot_info slots[2];
static char slots_json[2048];
static int slots_ready,reads,scans,live_ret;
static int rt3_layout_read(struct rt3_layout *p) { reads++; *p=live_layout; return live_ret; }
static void set_error(struct slot_info *s,const char *e) { (void)s;(void)e; }
static void format_slots(const struct rt3_layout *p) {
 snprintf(slots_json,sizeof(slots_json),"{\"count\":%u,\"fingerprint\":%u,\"name\":\"%s\",\"offset\":\"0x%llx\"}",
 p->count,p->fingerprint,p->count?p->parts[0].name:"",(unsigned long long)(p->count?p->parts[0].offset:0));
}
static void scan_slots(const struct rt3_layout *p) { scans++; format_slots(p); slots_ready=1; }
"""
    c += compare + handler + r"""
static void get(struct httpd_response *r) {
 struct httpd_request q={HTTP_GET}; memset(r,0,sizeof(*r));
 boot_slots_handler(HTTP_CB_NEW,&q,r);
 if(r->info.code!=200 || r->size>=sizeof(slots_json)) exit(20);
}
static void valid(unsigned int fp,u64 off,u64 size) {
 memset(&live_layout,0,sizeof(live_layout));live_ret=0;
 live_layout.mode=RT3_LAYOUT_DUAL;live_layout.count=2;live_layout.fingerprint=fp;
 strcpy(live_layout.parts[0].name,"rootfs");live_layout.parts[0].display_index=16;
 live_layout.parts[0].offset=off;live_layout.parts[0].size=size;
 strcpy(live_layout.parts[1].name,"rootfs_1");live_layout.parts[1].display_index=17;
 live_layout.parts[1].offset=0x3100000;live_layout.parts[1].size=0x2800000;
}
int main(void) {
 struct httpd_response r;
 valid(10,0x900000,0x2800000);get(&r);
 if(reads!=1||scans!=1||!strstr(r.data,"0x900000"))return 1;
 get(&r);if(reads!=2||scans!=1)return 2;
 valid(11,0x3100000,0x2800000);get(&r);
 if(reads!=3||scans!=2||!strstr(r.data,"0x3100000"))return 3;
 valid(11,0x3100000,0x2700000);get(&r);
 if(reads!=4||scans!=3)return 4;
 live_ret=-22;live_layout.mode=RT3_LAYOUT_UNSUPPORTED;live_layout.count=0;get(&r);
 if(reads!=5||scans!=3||!strstr(r.data,"\"count\":0")||strstr(r.data,"0x3100000"))return 5;
 valid(11,0x3100000,0x2700000);get(&r);if(reads!=6||scans!=4||!strstr(r.data,"0x3100000"))return 6;
 puts("GET layout cache PASS");return 0;
}
"""
    with tempfile.TemporaryDirectory() as d:
        f,exe=Path(d)/"get.c",Path(d)/"get";f.write_text(c)
        subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror",str(f),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


if __name__ == "__main__":
    main()
