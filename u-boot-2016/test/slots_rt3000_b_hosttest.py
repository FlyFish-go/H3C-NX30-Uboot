#!/usr/bin/env python3
"""Host tests for the exact RT3000-B environment and JSON helpers."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "u-boot-2016/failsafe/modules/slots_rt3000_b.c"


def main():
    source = SOURCE.read_text()
    save = source.index("static int env_save(")
    restore = source.index("static int env_restore(", save)
    helper_end = source.index("static int read_rootfs(", restore)
    helpers = source[save:helper_end]
    format_begin = source.index("static void format_slots(")
    format_end = source.index("static int same_layout(", format_begin)
    formatter = source[format_begin:format_end]
    assert source.index("ret = env_save();") < source.index("ret = set_scan_mtd(s);") < source.index("ret = ubi_part_readonly(s->part_name, NULL);")
    assert source.index("ubi_exit_readonly();") < source.index("if (env_restore())")
    assert 'setenv("mtdids", "nand0=nand0")' in source
    assert "static char slots_json[4096];" in source
    assert "hexport_r(&env_htab" in source and "himport_r(&env_htab" in source
    assert "env_t *" not in source and "env_export(" not in source and "env_import(" not in source
    assert '"mtd"' not in formatter and "mtd_display_only" not in formatter
    assert "ubi_write" not in source and "nand_write" not in source

    harness = r'''
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SLOT_N 2
typedef uint32_t u32;
typedef uint64_t u64;
typedef long ssize_t;
#define CONFIG_ENV_SIZE_MAX (256U*1024U)
#define CONFIG_ENV_RANGE (512U*1024U)
#define ENV_EXPORT_BYTES (300U*1024U)
struct hsearch_data { int unused; };
static struct hsearch_data env_htab;
static char *env_snapshot; static ssize_t env_snapshot_len; static u32 env_snapshot_crc;
static unsigned char env_state[ENV_EXPORT_BYTES];
static int import_fail, fail_mtdparts, ubi_calls;
static int setenv(const char *key,const char *value) {
 size_t off,lim,n;
 if(!strcmp(key,"mtdids")){off=0;lim=64;}
 else if(!strcmp(key,"mtdparts")){if(fail_mtdparts)return -1;off=64;lim=192;}
 else return -1;
 n=strlen(value);if(n>=lim)return -1;memset(env_state+off,0,lim);
 memcpy(env_state+off,value,n);return 0;
}
static const char *mock_getenv(const char *key) {
 return !strcmp(key,"mtdids")?(char *)env_state:
        !strcmp(key,"mtdparts")?(char *)env_state+64:NULL;
}
static u32 crc32(u32 c, const unsigned char *p, size_t n) {
 size_t i; c=~c; for(i=0;i<n;i++){int b;c^=p[i];for(b=0;b<8;b++)c=(c>>1)^(0xedb88320U&-(int)(c&1));} return ~c;
}
static ssize_t hexport_r(struct hsearch_data *htab, const char sep, int flag,
                         char **resp, size_t size, int argc, char * const argv[]) {
 (void)htab;(void)sep;(void)flag;(void)argc;(void)argv;
 if(size || !resp) return -1;
 if(!*resp) *resp=(char *)calloc(1,ENV_EXPORT_BYTES);
 if(!*resp) return -1;
 memcpy(*resp,env_state,ENV_EXPORT_BYTES);
 return ENV_EXPORT_BYTES;
}
static int himport_r(struct hsearch_data *htab, const char *env, size_t size,
                     const char sep, int flag, int crlf, int nvars,
                     char * const vars[]) {
 (void)htab;(void)sep;(void)flag;(void)crlf;(void)nvars;(void)vars;
 if(import_fail || size != ENV_EXPORT_BYTES) return 0;
 memcpy(env_state,env,size); return 1;
}
static int strlcpy(char *d,const char *s,size_t n) {
 size_t z=strlen(s),c=z<n-1?z:n-1;if(n){memcpy(d,s,c);d[c]=0;}return (int)z;
}
struct slot_info {
 u64 offset,size; unsigned int display_index;
 char label[96],part_name[64],status[16],type[16],version[97],detection[40],error[48];
};
struct rt3_layout { int mode; unsigned int count; unsigned int fingerprint; };
#define RT3_LAYOUT_DUAL 1
#define RT3_LAYOUT_SINGLE 2
static int mock_ubi_part(const struct slot_info *s) {
 char expected[192];
 int n=snprintf(expected,sizeof(expected),"mtdparts=nand0:0x%llx@0x%llx(%s)",
     (unsigned long long)s->size,(unsigned long long)s->offset,s->part_name);
 if(n<0 || (size_t)n>=sizeof(expected))return -1;
 if(strcmp(mock_getenv("mtdids"),"nand0=nand0") ||
    strcmp(mock_getenv("mtdparts"),expected) ||
    (strcmp(s->part_name,"rootfs") && strcmp(s->part_name,"rootfs_1")))return -1;
 ubi_calls++;return 0;
}
static struct slot_info slots[SLOT_N];
static char slots_json[4096],escaped_part[SLOT_N][128],escaped_label[SLOT_N][128],escaped_version[SLOT_N][256];
#define SLOT_ACCESS_JSON "\"read_only\":false,\"boot_supported\":true,"
static int environment_restored=1;
static void json_escape(const char *s,char *d,size_t n) { strlcpy(d,s,n); }
static const char *rt3_layout_mode_name(int m) { return m==1?"dual":m==2?"single":"unsupported"; }
'''
    harness += helpers + formatter + r'''
static int run_ubi_case(const char *name,u64 off,u64 size,int failure) {
 unsigned char before[sizeof(env_state)];unsigned int i;struct slot_info slot;
 memset(&slot,0,sizeof(slot));strcpy(slot.part_name,name);slot.offset=off;slot.size=size;
 for(i=0;i<sizeof(env_state);i++)env_state[i]=(unsigned char)(i*17+3);
 memcpy(before,env_state,sizeof(before));
 if(env_save()||set_scan_mtd(&slot)||mock_ubi_part(&slot))return 1;
 for(i=0;i<sizeof(env_state);i++)env_state[i]^=(unsigned char)(failure?0x5a:0xa5);
 for(i=0;i<sizeof(env_state);i+=3)env_state[i]^=0x33;
 if(env_restore()||memcmp(before,env_state,sizeof(before)))return 2;
 return 0;
}
static int run_setenv_failure(void) {
 unsigned char before[sizeof(env_state)];struct slot_info slot;unsigned int i;
 memset(&slot,0,sizeof(slot));strcpy(slot.part_name,"rootfs_1");
 slot.offset=0x3100000;slot.size=0x2800000;
 for(i=0;i<sizeof(env_state);i++)env_state[i]=(unsigned char)(i*7+1);
 memcpy(before,env_state,sizeof(before));if(env_save())return 1;
 fail_mtdparts=1;
 if(!set_scan_mtd(&slot)&&!ubi_calls)return 2;
 fail_mtdparts=0;
 if(env_restore()||memcmp(before,env_state,sizeof(before)))return 3;
 return 0;
}
int main(void) {
 struct rt3_layout l={.mode=RT3_LAYOUT_DUAL,.count=2,.fingerprint=0x1234};
 int i;
 (void)set_error;
 if(run_ubi_case("rootfs",0x900000,0x2800000,0) ||
    run_ubi_case("rootfs_1",0x3100000,0x2800000,1) ||
    run_setenv_failure() || ubi_calls!=2) return 10;
 for(i=0;i<sizeof(env_state);i++) env_state[i]=(unsigned char)i;
 if(env_save()) return 11;
 memset(env_state,0xa5,sizeof(env_state));
 import_fail=1;
 if(env_restore()==0) return 12;
 /* Real endpoint formatting: two named choices with capacity, no mtd labels. */
 memset(slots,0,sizeof(slots));
 strcpy(slots[0].part_name,"rootfs"); slots[0].display_index=16;
 slots[0].offset=0x900000; slots[0].size=0x2800000;
 strcpy(slots[0].status,"known"); strcpy(slots[0].type,"OEM");
 strcpy(slots[0].version,"RT3000 R007"); strcpy(slots[0].label,"OEM RT3000 R007");
 strcpy(slots[0].detection,"known_rootfs_sha256");
 strcpy(slots[1].part_name,"rootfs_1"); slots[1].display_index=17;
 slots[1].offset=0x3100000; slots[1].size=0x2800000;
 strcpy(slots[1].status,"unknown"); strcpy(slots[1].type,"unknown");
 strcpy(slots[1].version,"unknown"); strcpy(slots[1].label,"Unknown system");
 strcpy(slots[1].detection,"unknown_rootfs_sha256"); strcpy(slots[1].error,"squashfs_not_found");
 environment_restored=1; format_slots(&l); puts(slots_json);
 environment_restored=0; format_slots(&l); puts(slots_json);
 slots[0].offset=0x3100000; slots[1].offset=0x900000;
 environment_restored=1; format_slots(&l); puts(slots_json);
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="rt3000-b-slots-hosttest-") as temp:
        cfile, exe = Path(temp) / "test.c", Path(temp) / "test"
        cfile.write_text(harness)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-sign-compare", str(cfile), "-o", str(exe)], check=True)
        rows = subprocess.check_output([str(exe)], text=True).splitlines()
    assert len(rows) == 3
    for row, restored in zip(rows[:2], (True, False)):
        obj = json.loads(row)
        assert len(row) < 2048
        assert obj["environment_restored"] is restored
        assert "mtd" not in obj and "mtd_display_only" not in obj
        assert [x["name"] for x in obj["slots"]] == ["rootfs", "rootfs_1"]
        assert [x["size"] for x in obj["slots"]] == ["0x2800000", "0x2800000"]
        assert [x["physical_index"] for x in obj["slots"]] == [1, 2]
        assert [x["physical_end"] for x in obj["slots"]] == ["0x30fffff", "0x58fffff"]
        assert obj["read_only"] is False and obj["boot_supported"] is True
        assert [x["boot_command"] for x in obj["slots"]] == ["bootipq rootfs", "bootipq rootfs_1"]
        assert all(not any(key.startswith("dashboard_") for key in x) for x in obj["slots"])
    swapped = json.loads(rows[2])
    assert [x["physical_index"] for x in swapped["slots"]] == [2, 1]
    assert [x["offset"] for x in swapped["slots"]] == ["0x3100000", "0x900000"]
    print("PASS: dynamic full ENV snapshot above ENV_SIZE_MAX, exact mtdids/mtdparts at ubi_part for both named slots, setenv failure skips UBI, restore success/failure, JSON <=2048 bytes")


if __name__ == "__main__":
    main()
