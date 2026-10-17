#!/usr/bin/env python3
"""Compile the candidate fdtedit implementation against a bounded FDT mock."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path


HARNESS = r"""
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t host_strlcpy(char *d, const char *s, size_t n) {
    size_t len = strlen(s), take = n && len >= n ? n - 1 : len;
    if (n) { memcpy(d, s, take); d[take] = 0; }
    return len;
}
static size_t host_strlcat(char *d, const char *s, size_t n) {
    size_t used = strnlen(d, n);
    return used == n ? n + strlen(s) : used + host_strlcpy(d + used, s, n - used);
}
#define strlcpy host_strlcpy
#define strlcat host_strlcat
#define abort() do { fprintf(stderr, "assertion failed at %s:%d\n", __FILE__, __LINE__); exit(1); } while (0)
typedef uint32_t u32;
typedef uint64_t u64;
#define FDT_EDIT "fdtedit"
#define NUM_BUF_SIZE 3
#define FDT_ERR_NOTFOUND 1
#define FDT_ERR_BADMAGIC 9
#define FDT_ERR_NOSPACE 3
#define CONFIG_IPQ_FDT_FIXUP 1
#define cpu_to_fdt32(v) htonl((u32)(v))
static u64 cpu_to_fdt64(u64 v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((u64)htonl((u32)v) << 32) | htonl((u32)(v >> 32));
#else
    return v;
#endif
}
struct property { char name[64]; unsigned char data[8192]; int len; int used; };
struct mock_fdt { u32 magic, totalsize; struct property props[24]; };
static const u32 MAGIC = 0x4644544d;
static char *env_names[104], *env_values[104];
static int alloc_fail_after = -1, prop_fail_after = -1, alloc_outstanding;
static void *test_malloc(size_t n) {
    if (alloc_fail_after == 0) return NULL;
    if (alloc_fail_after > 0) alloc_fail_after--;
    { void *p=malloc(n); if(p) alloc_outstanding++; return p; }
}
static void test_free(void *p) { if(p) alloc_outstanding--; free(p); }
#define malloc test_malloc
#define free test_free
static char *host_getenv(const char *name) {
    int i;
    for (i=0;i<104;i++) if (env_names[i] && !strcmp(env_names[i],name)) return env_values[i];
    return NULL;
}
#define getenv host_getenv
static int env_set(const char *name, const char *value) {
    int i, empty=-1;
    for (i=0;i<104;i++) {
        if (env_names[i] && !strcmp(env_names[i],name)) break;
        if (!env_names[i] && empty<0) empty=i;
    }
    if (i==104) i=empty;
    if (i<0) return -1;
    env_names[i]=(char *)name; env_values[i]=(char *)value; return 0;
}
static int fdt_check_header(const void *blob) {
    const struct mock_fdt *f=blob; return f && f->magic==MAGIC ? 0 : -FDT_ERR_BADMAGIC;
}
static u32 fdt_totalsize(const void *blob) { return ((const struct mock_fdt *)blob)->totalsize; }
static int fdt_path_offset(const void *blob,const char *path) {
    (void)blob; return !strcmp(path,"/soc/test") ? 1 : -FDT_ERR_NOTFOUND;
}
static struct property *find_prop(struct mock_fdt *f,const char *name) {
    int i; for(i=0;i<24;i++) if(f->props[i].used && !strcmp(f->props[i].name,name)) return &f->props[i];
    return NULL;
}
static int fdt_setprop(void *blob,int node,const char *name,const void *data,int len) {
    struct mock_fdt *f=blob; struct property *p; int i;
    (void)node;
    if(prop_fail_after==0) return -FDT_ERR_NOSPACE;
    if(prop_fail_after>0) prop_fail_after--;
    if(!name || !*name || strlen(name)>=sizeof(f->props[0].name) || len<0 || len>(int)sizeof(f->props[0].data)) return -FDT_ERR_NOSPACE;
    p=find_prop(f,name);
    if(!p) for(i=0;i<24;i++) if(!f->props[i].used) { p=&f->props[i]; break; }
    if(!p) return -FDT_ERR_NOSPACE;
    strcpy(p->name,name); if(len) memcpy(p->data,data,len); p->len=len; p->used=1; return 0;
}
static int fdt_setprop_u32(void *blob,int node,const char *name,u32 value) {
    u32 encoded=cpu_to_fdt32(value); return fdt_setprop(blob,node,name,&encoded,sizeof(encoded));
}
static int fdt_delprop(void *blob,int node,const char *name) {
    struct property *p=find_prop(blob,name); (void)node;
    if(!p) return -FDT_ERR_NOTFOUND; memset(p,0,sizeof(*p)); return 0;
}
static int fdtedit_parse_uint(const char *,unsigned int,u64,u64 *);
static int fdtedit_parse_array(char *,char *,void *,int);
int parse_fdt_fixup(const char *,void *);
int ipq_fdt_fixup(void *);
/* Candidate production functions are inserted here. */
@@FUNCTIONS@@
static void reset_env(void) { memset(env_names,0,sizeof(env_names)); memset(env_values,0,sizeof(env_values)); }
static void init_fdt(struct mock_fdt *f) { memset(f,0,sizeof(*f)); f->magic=MAGIC; f->totalsize=sizeof(*f); }
static void expect_prop_u32(struct mock_fdt *f,const char *name,u32 expected) {
    struct property *p=find_prop(f,name); u32 actual;
    if(!p || p->len!=4) abort(); memcpy(&actual,p->data,4); if(ntohl(actual)!=expected) abort();
}
static void set_edit(int i,const char *value) {
    static char names[100][16]; snprintf(names[i],sizeof(names[i]),"fdtedit%d",i); env_set(names[i],value);
}
int main(void) {
    struct mock_fdt f, before;
    struct property *p;
    char mutable_edit[]="/soc/test%dec%0x10";
    int ret;
    init_fdt(&f);
    if(parse_fdt_fixup(mutable_edit,&f)) abort();
    if(strcmp(mutable_edit,"/soc/test%dec%0x10")) abort();
    expect_prop_u32(&f,"dec",16);
    if(parse_fdt_fixup("/soc/test%text%?hello",&f)) abort();
    p=find_prop(&f,"text"); if(!p || p->len!=6 || memcmp(p->data,"hello\0",6)) abort();
    if(parse_fdt_fixup("/soc/test%32?4?reg%0x8AF8600?0x200?0x8A00000?0xcb00",&f)) abort();
    p=find_prop(&f,"reg"); if(!p || p->len!=16) abort();
    { u32 v[4]; memcpy(v,p->data,sizeof(v)); if(ntohl(v[0])!=0x08af8600 || ntohl(v[1])!=0x200 || ntohl(v[2])!=0x08a00000 || ntohl(v[3])!=0xcb00) abort(); }
    if(parse_fdt_fixup("/soc/test%64?2?wide%0x49A00000?0x500000",&f)) abort();
    p=find_prop(&f,"wide"); if(!p || p->len!=16) abort();
    { u64 v[2]; memcpy(v,p->data,sizeof(v)); if(cpu_to_fdt64(0x49a00000ULL)!=v[0] || cpu_to_fdt64(0x500000ULL)!=v[1]) abort(); }
    if(parse_fdt_fixup("/soc/test%delete%?text",&f) || find_prop(&f,"text")) abort();
    if(parse_fdt_fixup("/soc/test%delete%?text",&f)) abort(); /* repeated delete is stable */

    { const char *bad[]={"", "soc/test%v%1", "/soc/test%%1", "/soc/test%v", "/soc/test%v%1%tail",
        "/soc/missing%v%1", "/soc/test%v%-1", "/soc/test%v%0x", "/soc/test%v%0x100000000",
        "/soc/test%32?0?arr%", "/soc/test%32?2?arr%1", "/soc/test%32?1?arr%1?2",
        "/soc/test%32?2?arr%1?2?", "/soc/test%32?2?arr%1?xyz", "/soc/test%33?1?arr%1",
        "/soc/test%delete%text"};
      unsigned i; for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++) if(parse_fdt_fixup(bad[i],&f)>=0) { fprintf(stderr,"accepted invalid case %u\n",i); abort(); } }

    /* Invalid second edit rolls back the earlier valid edit as a whole. */
    reset_env(); init_fdt(&f); before=f;
    env_set("fdteditnum","1"); set_edit(0,"/soc/test%first%0x44"); set_edit(1,"/soc/missing%bad%1");
    ret=ipq_fdt_fixup(&f); if(ret>=0 || memcmp(&f,&before,sizeof(f))) abort();
    /* A libfdt write failure after one staged mutation also leaves caller FDT untouched. */
    reset_env(); init_fdt(&f); before=f;
    env_set("fdteditnum","1"); set_edit(0,"/soc/test%first%1"); set_edit(1,"/soc/test%second%2");
    prop_fail_after=1; ret=ipq_fdt_fixup(&f); prop_fail_after=-1;
    if(ret>=0 || memcmp(&f,&before,sizeof(f))) abort();
    /* Environment buffers remain unchanged and a successful transaction is repeatable. */
    reset_env(); init_fdt(&f);
    env_set("fdteditnum","2"); set_edit(0,"/soc/test%value%0xdeadbeef");
    set_edit(1,"/soc/test%text%?stable"); set_edit(2,"/soc/test%delete%?already_missing");
    if(ipq_fdt_fixup(&f)) abort(); before=f;
    if(ipq_fdt_fixup(&f) || memcmp(&f,&before,sizeof(f))) abort();
    expect_prop_u32(&f,"value",0xdeadbeef);
    /* Strict index parsing and upper bound. */
    reset_env(); init_fdt(&f); before=f; env_set("fdteditnum","100");
    if(ipq_fdt_fixup(&f)>=0 || memcmp(&f,&before,sizeof(f))) abort();
    reset_env(); env_set("fdteditnum","-1"); if(ipq_fdt_fixup(&f)>=0) abort();
    reset_env(); env_set("fdteditnum","99"); if(ipq_fdt_fixup(&f)) abort();
    /* Allocation failure for staged tree or per-edit copy is propagated safely. */
    reset_env(); init_fdt(&f); env_set("fdteditnum","0"); set_edit(0,"/soc/test%x%1");
    alloc_fail_after=0; ret=ipq_fdt_fixup(&f); alloc_fail_after=-1;
    if(ret>=0) abort();
    reset_env(); init_fdt(&f); before=f; env_set("fdteditnum","0"); set_edit(0,"/soc/test%x%1");
    alloc_fail_after=1; ret=ipq_fdt_fixup(&f); alloc_fail_after=-1;
    if(ret>=0 || memcmp(&f,&before,sizeof(f))) abort();
    if(alloc_outstanding) abort();
    puts("FDTEDIT_HOSTTEST_PASS");
    return 0;
}
"""


def extract_functions(source):
    start = source.index("static int fdtedit_parse_uint(")
    end = source.index("__weak void fdt_fixup_sd_ldo_gpios_toggle", start)
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="candidate fdt_fixup.c")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve().read_text()
    generated = HARNESS.replace("@@FUNCTIONS@@", extract_functions(source))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fdtedit-hosttest-") as td:
        cfile = Path(td) / "fdtedit_hosttest.c"
        binary = Path(td) / "fdtedit_hosttest"
        cfile.write_text(generated)
        subprocess.run(["gcc", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-fno-sanitize-recover=all",
                        str(cfile), "-o", str(binary)], check=True)
        test_env = os.environ.copy()
        test_env["ASAN_OPTIONS"] = "detect_leaks=0"
        completed = subprocess.run([str(binary)], capture_output=True, text=True, env=test_env)
        if completed.returncode:
            raise SystemExit(completed.stderr or f"host test exited {completed.returncode}")
    report = args.output_dir / "fdtedit-hosttest.txt"
    report.write_text(completed.stdout)
    print(completed.stdout.strip())
    print(f"ASAN_UBSAN=PASS output={report}")


if __name__ == "__main__":
    main()
