#!/usr/bin/env python3
"""Compile the production FIT validator body against host libfdt and real FIT data."""
from pathlib import Path
import os, subprocess, tempfile, sys
root = Path(__file__).resolve().parents[1]
source = (root / "board/qca/arm/common/failsafe.c").read_text()
fit_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(os.environ.get("HW_A_RECOVERY_FIT", ""))
if not str(fit_path): raise SystemExit("supply FIT path as argv[1] or HW_A_RECOVERY_FIT")
assert fit_path.is_file(), fit_path

def extract(name):
    at=source.index(name)
    brace=source.index("{", at)
    depth=0
    for i in range(brace,len(source)):
        if source[i]=="{": depth+=1
        elif source[i]=="}":
            depth-=1
            if depth==0:return source[at:i+1]
    raise AssertionError(name)

prod = "\n".join(extract(n) for n in (
    "static int recovery_fit_sha256_required",
    "static int recovery_fit_memory_matches",
    "static int recovery_newc_hex8",
    "static int recovery_newc_name_is",
    "static int recovery_newc_path_safe",
    "static int recovery_newc_validate",
    "static int recovery_fit_validate_blob",
    "static int recovery_upload_range_valid",
    "static int recovery_upload_range_valid",
))
prod = "\n".join(extract(n) for n in (
    "static int recovery_fit_sha256_required",
    "static int recovery_fit_memory_matches",
    "static int recovery_newc_hex8",
    "static int recovery_newc_name_is",
    "static int recovery_newc_path_safe",
    "static int recovery_newc_validate",
    "static int recovery_fit_validate_blob",
    "static int recovery_upload_range_valid",
))
harness = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include <openssl/sha.h>
#include <libfdt.h>
int fdt_check_full(const void *fdt, size_t bufsize);
#define be32_to_cpu(x) __builtin_bswap32(x)
#define cpu_to_be32(x) __builtin_bswap32(x)
typedef unsigned long ulong;
typedef unsigned char u8;
typedef uint32_t u32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
struct { unsigned long ram_size; } gd_storage;
#define gd (&gd_storage)
#define SHA256_SUM_LEN 32
#define SZ_1M 0x100000UL
#define SZ_64K 0x10000UL
#define H3C_RECOVERY_FIT_MAX_SIZE 0x01000000UL
#define H3C_RECOVERY_FIT_CONFIG "config@mp03.1"
#define H3C_RECOVERY_KERNEL_LOAD 0x41208000UL
#define H3C_RECOVERY_RAMDISK_LOAD 0x4c000000UL
static unsigned char host_ramdisk_scratch[16 * 1024 * 1024];
#define H3C_RECOVERY_RAMDISK_BUFFER ((void *)host_ramdisk_scratch)
#define IH_TYPE_KERNEL 2
#define IH_TYPE_RAMDISK 3
#define IH_TYPE_FLATDT 8
#define IH_OS_LINUX 5
#define IH_ARCH_ARM 2
#define IH_COMP_NONE 0
#define IH_COMP_GZIP 1
#define IH_COMP_LZMA 3
static int enum_prop(const void *fit,int node,const char *prop,const char **vals,size_t n)
{
 const char *v; int len; size_t i;
 v=fdt_getprop(fit,node,prop,&len); if(!v||len<1)return -1;
 for(i=0;i<n;i++)if((int)strlen(vals[i])+1==len&&!memcmp(v,vals[i],len))return (int)i;
 return -1;
}
int fit_check_format(const void *fit, ulong size) {
 return fdt_check_full(fit,size) || fdt_path_offset(fit,"/images")<0 || fdt_path_offset(fit,"/configurations")<0;
}
int fit_conf_get_node(const void *fit,const char *name) {
 char path[128]; snprintf(path,sizeof(path),"/configurations/%s",name); return fdt_path_offset(fit,path);
}
int fit_conf_get_prop_node(const void *fit,int conf,const char *prop) {
 const char *name; int len; char path[128];
 name=fdt_getprop(fit,conf,prop,&len); if(!name||len<2||name[len-1])return -1;
 snprintf(path,sizeof(path),"/images/%s",name); return fdt_path_offset(fit,path);
}
static int get_enum(const void *fit,int node,const char *property,const char **table,size_t n,uint8_t *out) {
 int v=enum_prop(fit,node,property,table,n); if(v<0)return -1; *out=(uint8_t)v; return 0;
}
static const char *types[]={"invalid","standalone","kernel","ramdisk","multi","firmware","script","filesystem","flat_dt"};
static const char *oses[]={"invalid","openbsd","netbsd","freebsd","4_4bsd","linux"};
static const char *arches[]={"invalid","alpha","arm","i386","ia64"};
static const char *comps[]={"none","gzip","bzip2","lzma","lzo","lz4"};
int fit_image_get_type(const void*f,int n,uint8_t*v){return get_enum(f,n,"type",types,sizeof(types)/sizeof(types[0]),v);}
int fit_image_get_os(const void*f,int n,uint8_t*v){return get_enum(f,n,"os",oses,sizeof(oses)/sizeof(oses[0]),v);}
int fit_image_get_arch(const void*f,int n,uint8_t*v){return get_enum(f,n,"arch",arches,sizeof(arches)/sizeof(arches[0]),v);}
int fit_image_get_comp(const void*f,int n,uint8_t*v){return get_enum(f,n,"compression",comps,sizeof(comps)/sizeof(comps[0]),v);}
static int get_addr(const void*f,int n,const char*p,ulong*out) {
 const fdt32_t *v; int len; v=fdt_getprop(f,n,p,&len); if(!v||(len!=4&&len!=8))return -1;
 *out=len==8?(((uint64_t)fdt32_to_cpu(v[0])<<32)|fdt32_to_cpu(v[1])):fdt32_to_cpu(v[0]); return 0;
}
int fit_image_get_load(const void*f,int n,ulong*v){return get_addr(f,n,"load",v);}
int fit_image_get_entry(const void*f,int n,ulong*v){return get_addr(f,n,"entry",v);}
int fit_image_get_data(const void*f,int n,const void **data,size_t *size) {
 int len; const void *p=fdt_getprop(f,n,"data",&len); if(!p||len<0)return -1; *data=p; *size=(size_t)len; return 0;
}
int fit_image_verify(const void *fit,int image) {
 int node, found=0; const void *data; size_t size; unsigned char digest[SHA256_DIGEST_LENGTH];
 if(fit_image_get_data(fit,image,&data,&size))return 0;
 SHA256(data,size,digest);
 fdt_for_each_subnode(fit,node,image) {
  const char *nm=fdt_get_name(fit,node,NULL),*algo; const void *val; int len;
  if(!nm||strncmp(nm,"hash",4))continue;
  if(fdt_getprop(fit,node,"ignore",NULL))return 0;
  algo=fdt_getprop(fit,node,"algo",&len);
  if(!algo||len!=7||memcmp(algo,"sha256",7))continue;
  val=fdt_getprop(fit,node,"value",&len); if(!val||len!=32||memcmp(val,digest,32))return 0; found++;
 }
 return found>0;
}
int gunzip(void *dst,unsigned long max,unsigned char *src,unsigned long *size) {
 z_stream z; int ret;
 memset(&z,0,sizeof(z)); z.next_in=src; z.avail_in=(uInt)*size; z.next_out=dst; z.avail_out=(uInt)max;
 if(inflateInit2(&z,MAX_WBITS+16)!=Z_OK)return -1;
 ret=inflate(&z,Z_FINISH); if(ret!=Z_STREAM_END){inflateEnd(&z);return -1;}
 *size=z.total_out; inflateEnd(&z);return 0;
}
''' + prod + r'''
static void *readfile(const char *path,size_t *size) {
 FILE *f=fopen(path,"rb"); long n; void *p; if(!f)return NULL; fseek(f,0,SEEK_END);n=ftell(f);rewind(f);p=malloc(n);if(!p||fread(p,1,n,f)!=(size_t)n){fclose(f);free(p);return NULL;}fclose(f);*size=n;return p;
}
static void *clonefit(const void *orig,size_t sz,size_t *outsz) {
 void *p=calloc(1,sz+8192); if(!p)return NULL;
 if(fdt_open_into(orig,p,sz+8192)){free(p);return NULL;}
 *outsz=sz+8192;return p;
}
static int validate(void *p,size_t *sz) { fdt_pack(p);*sz=fdt_totalsize(p); return recovery_fit_validate_blob(p,*sz); }
static int prop_node(void *p,const char *path) { return fdt_path_offset(p,path); }
static unsigned cpio_hex(const unsigned char *p) {
 char v[9]; memcpy(v,p,8); v[8]=0; return (unsigned)strtoul(v,NULL,16);
}
static void cpio_puthex(unsigned char *p,unsigned v) {
 static const char h[]="0123456789abcdef"; int i;
 for(i=7;i>=0;i--){p[i]=h[v&15];v>>=4;}
}
static int cpio_find(const unsigned char *p,size_t size,const char *want,
                     size_t *hdr,size_t *body) {
 size_t off=0;
 while(off+110<=size && !memcmp(p+off,"070701",6)) {
  unsigned ns=cpio_hex(p+off+94), fs=cpio_hex(p+off+54);
  size_t name=off+110, data=(name+ns+3)&~(size_t)3;
  size_t next=(data+fs+3)&~(size_t)3;
  if(ns<1 || name+ns>size || next>size)return -1;
  if(!strcmp((const char *)p+name,want) ||
     ((p[name]=='.'&&p[name+1]=='/')&&!strcmp((const char *)p+name+2,want))) {
   *hdr=off;*body=data;return 0;
  }
  if(!strcmp((const char *)p+name,"TRAILER!!!"))return -1;
  off=next;
 }
 return -1;
}
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)
int main(int argc,char **argv) {
 size_t original_size, size; void *original=readfile(argv[1],&original_size),*p; int n; fdt32_t cells[2]; 
 CHECK(argc==2&&original); gd->ram_size=0x20000000UL;
 CHECK(recovery_fit_validate_blob(original,original_size)==0);
 CHECK(recovery_upload_range_valid(0x50000000UL,0x01000000UL,0x50000000UL,0x60000000UL));
 CHECK(recovery_upload_range_valid(0x5000ffffUL,0x01000000UL,0x50000000UL,0x60000000UL));
 CHECK(!recovery_upload_range_valid(0x50010000UL,1,0x50000000UL,0x60000000UL));
 CHECK(!recovery_upload_range_valid(0x4fffffffUL,1,0x50000000UL,0x60000000UL));
 CHECK(!recovery_upload_range_valid(~0UL-4,8,0x50000000UL,~0UL));
 CHECK(!recovery_upload_range_valid(0x50000000UL,0x01000000UL,0x50000000UL,0x50ffffffUL));
 {
  int conf=fit_conf_get_node(original,H3C_RECOVERY_FIT_CONFIG);
  int rn=fit_conf_get_prop_node(original,conf,"ramdisk");
  uint8_t rc; const void *rd; size_t rds; unsigned char *bad;
  size_t h,b;
  CHECK(conf>=0&&rn>=0&&fit_image_get_comp(original,rn,&rc)==0);
  CHECK(rc==IH_COMP_NONE||rc==IH_COMP_GZIP);
  CHECK(fit_image_get_data(original,rn,&rd,&rds)==0&&rd&&rds>0);
  if(rc==IH_COMP_GZIP) {
   unsigned long expanded=rds;
   CHECK(gunzip(host_ramdisk_scratch,sizeof(host_ramdisk_scratch),(unsigned char *)rd,&expanded)==0);
   rd=host_ramdisk_scratch;rds=expanded;
  }
  CHECK(recovery_newc_validate(rd,rds)==0);
  CHECK(recovery_newc_validate(rd,rds-600)!=0);
  bad=malloc(rds);CHECK(bad);
  memcpy(bad,rd,rds);
  CHECK(cpio_find(bad,rds,"bin/busybox",&h,&b)==0);
  cpio_puthex(bad+h+54,0xffffffffU);
  CHECK(recovery_newc_validate(bad,rds)!=0);
  memcpy(bad,rd,rds);
  CHECK(cpio_find(bad,rds,"TRAILER!!!",&h,&b)==0);
  bad[h]='X';
  CHECK(recovery_newc_validate(bad,rds)!=0);
  memcpy(bad,rd,rds);
  CHECK(cpio_find(bad,rds,"init",&h,&b)==0);
  memcpy(bad+h+110,"../i",4);
  CHECK(recovery_newc_validate(bad,rds)!=0);
  memcpy(bad,rd,rds);
  CHECK(cpio_find(bad,rds,"init",&h,&b)==0);
  cpio_puthex(bad+h+14,0x000081a4U);
  CHECK(recovery_newc_validate(bad,rds)!=0);
  memcpy(bad,rd,rds);
  CHECK(cpio_find(bad,rds,"lib/ld-musl-arm.so.1",&h,&b)==0);
  bad[b]='x';
  CHECK(recovery_newc_validate(bad,rds)!=0);
  memcpy(bad,rd,rds);
  CHECK(cpio_find(bad,rds,"bin/sh",&h,&b)==0);
  bad[b+6]='X';
  CHECK(recovery_newc_validate(bad,rds)!=0);
  free(bad);
 }
 p=clonefit(original,original_size,&size); n=prop_node(p,"/images/kernel@1/hash@1"); CHECK(n>=0);
 {const void *v;int l;v=fdt_getprop(p,n,"value",&l);CHECK(v&&l==32);((char*)v)[0]^=1;} CHECK(validate(p,&size)!=0);free(p);
 p=clonefit(original,original_size,&size); n=prop_node(p,"/images/kernel@1/hash@1");CHECK(n>=0);CHECK(fdt_del_node(p,n)==0);CHECK(validate(p,&size)!=0);free(p);
 p=clonefit(original,original_size,&size); n=prop_node(p,"/images/kernel@1/hash@1");CHECK(n>=0);CHECK(fdt_setprop(p,n,"ignore",NULL,0)==0);CHECK(validate(p,&size)!=0);free(p);
 p=clonefit(original,original_size,&size); n=prop_node(p,"/images/kernel@1");CHECK(n>=0);CHECK(fdt_setprop_string(p,n,"arch","i386")==0);CHECK(validate(p,&size)!=0);free(p);
 p=clonefit(original,original_size,&size); n=prop_node(p,"/images/kernel@1");CHECK(n>=0);cells[0]=cpu_to_fdt32(0x41209000);CHECK(fdt_setprop(p,n,"load",cells,sizeof(cells[0]))==0);CHECK(validate(p,&size)!=0);free(p);
 p=clonefit(original,original_size,&size);n=prop_node(p,"/configurations");CHECK(n>=0);CHECK(fdt_setprop_string(p,n,"default","bad-config")==0);CHECK(validate(p,&size)!=0);free(p);
 p=clonefit(original,original_size,&size);n=prop_node(p,"/images/fdt@mp03.1");CHECK(n>=0);{const void*d;int l;d=fdt_getprop(p,n,"data",&l);CHECK(d&&l>0);void *dt=malloc(l+4096);CHECK(dt);memcpy(dt,d,l);CHECK(fdt_open_into(dt,dt,l+4096)==0);int m=fdt_path_offset(dt,"/memory");CHECK(m>=0);fdt32_t reg[4]={0,cpu_to_fdt32(0x40000000),0,cpu_to_fdt32(0x10000000)};CHECK(fdt_setprop(dt,m,"reg",reg,sizeof(reg))==0);fdt_pack(dt);CHECK(fdt_setprop(p,n,"data",dt,fdt_totalsize(dt))==0);free(dt);
 int hn=prop_node(p,"/images/fdt@mp03.1/hash@1");CHECK(hn>=0);
 const void *newdata;int newlen;unsigned char digest[32];
 newdata=fdt_getprop(p,n,"data",&newlen);CHECK(newdata&&newlen>0);
 SHA256(newdata,newlen,digest);CHECK(fdt_setprop(p,hn,"value",digest,sizeof(digest))==0);
 }CHECK(validate(p,&size)!=0);free(p);
 gd->ram_size=0x10000000UL;CHECK(recovery_fit_validate_blob(original,original_size)!=0);gd->ram_size=0x20000000UL;
 CHECK(recovery_fit_validate_blob(original,original_size)==0);
 free(original); puts("HW-A production FIT validator C harness: PASS (gzip/raw newc, bounds/trailer/path/init mode/loader links plus FIT hash/arch/load/config/memory)");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="hwa-fit-c-") as td:
    c=Path(td)/"test.c"; exe=Path(td)/"test"
    c.write_text(harness)
    inc=root/"include"
    subprocess.run(["gcc","-std=gnu11","-O2","-Wall","-Werror","-idirafter",str(inc),"-idirafter",str(root/"arch/arm/include"),str(c),"-o",str(exe),"-Wl,-l:libfdt.so.1","-lcrypto","-lz"],check=True)
    subprocess.run([str(exe),str(fit_path)],check=True)
