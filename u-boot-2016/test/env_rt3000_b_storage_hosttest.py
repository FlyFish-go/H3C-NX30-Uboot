#!/usr/bin/env python3
# Production B ENV save helper exercised against a bounded mock NAND partition.
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
src = (ROOT / "common/env_nand.c").read_text()

def c_function(signature):
    start = src.index(signature)
    brace = src.index("{", start)
    depth = 0
    for end in range(brace, len(src)):
        if src[end] == "{": depth += 1
        elif src[end] == "}":
            depth -= 1
            if depth == 0: return src[start:end + 1]
    raise AssertionError("unterminated function " + signature)

functions = "\n".join(c_function(x) for x in [
    "static int rt3_env_target(",
    "static int rt3_env_nand_read(",
    "static int rt3_env_nand_write(",
    "static int rt3_env_erase(",
    "int rt3_nand_env_tail_matches(",
    "static int rt3_nand_saveenv(",
])

c = r'''#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
typedef unsigned char u_char;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct mtd_info { const char *name; uint64_t size; uint32_t erasesize, writesize; unsigned flags; } nand_info_t;
typedef struct { uint32_t flash_block_size; } qca_smem_flash_info_t;
#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#define RT3_ENV_BOARD_LABEL "H3C HW-B"
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define CONFIG_ENV_SIZE_MAX (256U << 10)
#define CONFIG_ENV_SIZE CONFIG_ENV_SIZE_MAX
#define CONFIG_ENV_RANGE board_env_range
#define CONFIG_ENV_OFFSET board_env_offset
#define ENV_SIZE (CONFIG_ENV_RANGE - sizeof(uint32_t))
#define ARCH_DMA_MINALIGN 64
#define MTD_WRITEABLE 0x80
#define SMEM_BOOT_QSPI_NAND_FLASH 9
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
typedef struct { uint32_t crc; unsigned char data[CONFIG_ENV_SIZE_MAX-sizeof(uint32_t)]; } env_t;
typedef struct { loff_t offset, length; } nand_erase_options_t;
static unsigned char flash[0x8000000];
static nand_info_t nand_info[1];
qca_smem_flash_info_t qca_smem_flash_info = { 0x20000 };
static int nand_env_device = 0;
static loff_t board_env_offset = 0x480000, board_env_size = 0x80000, board_env_range = 0x40000;
static int bad_block, fail_alloc_at, alloc_calls, erase_calls, write_calls, read_calls, open_calls, close_calls;
static int fail_erase_call, fail_write_call, fail_read_call, fail_open;
static int window_open;
static unsigned old_flags;
static uint32_t crc32(uint32_t seed, const unsigned char *data, size_t len)
{ uint32_t c = ~seed; size_t i; int b; for(i=0;i<len;i++){ c ^= data[i]; for(b=0;b<8;b++) c=(c>>1)^((uint32_t)-(int)(c&1)&0xedb88320U); } return ~c; }
static void *memalign(size_t a,size_t n)
{ void *p=NULL; (void)a; alloc_calls++; if (fail_alloc_at && alloc_calls==fail_alloc_at) return NULL; if (posix_memalign(&p,64,n)) return NULL; return p; }
static int smem_getpart(const char *name,uint32_t *start,uint32_t *count)
{ if(strcmp(name,"0:APPSBLENV")) return -1; *start=36; *count=4; return 0; }
static int nand_block_isbad(nand_info_t *n,loff_t off)
{ (void)n; return bad_block && off==board_env_offset ? 1:0; }
static int nand_read(nand_info_t *n,loff_t off,size_t *len,u_char *out)
{ read_calls++; if(fail_read_call==read_calls) return -EIO; if(off<0 || (uint64_t)off+*len>n->size) return -EINVAL; memcpy(out,flash+off,*len); return 0; }
static int nand_write(nand_info_t *n,loff_t off,size_t *len,u_char *in)
{ write_calls++; if(off<0 || (uint64_t)off+*len>n->size) return -EINVAL; if(fail_write_call==write_calls){ size_t partial=*len>4096?4096:*len; memcpy(flash+off,in,partial); *len=partial; return -EIO; } memcpy(flash+off,in,*len); return 0; }
static int nand_erase_opts(nand_info_t *n,const nand_erase_options_t *o)
{ (void)n; erase_calls++; if(o->offset!=board_env_offset || o->length!=board_env_size) return -EINVAL; if(fail_erase_call==erase_calls){ memset(flash+o->offset,0xff,4096); return -EIO; } memset(flash+o->offset,0xff,o->length); return 0; }
static int env_export(env_t *e)
{ memset(e,0,CONFIG_ENV_SIZE); memcpy(e->data,"alpha=two\0\0",11); e->crc=crc32(0,e->data,ENV_SIZE); return 0; }
static int rt3_metadata_write_window_open(nand_info_t *n,const char *name,uint64_t off,uint64_t size)
{ open_calls++; if(fail_open || strcmp(name,"0:APPSBLENV") || n!=&nand_info[0] || (loff_t)off!=board_env_offset || (loff_t)size!=board_env_size) return -EROFS; old_flags=n->flags; n->flags|=MTD_WRITEABLE; window_open=1; return 0; }
static void rt3_fw_write_window_close(void)
{ close_calls++; if(window_open) nand_info[0].flags=old_flags; window_open=0; }
''' + functions + r'''
static void setup(void)
{ memset(flash,0x5a,sizeof(flash)); nand_info[0]=(nand_info_t){"nand0",sizeof(flash),0x20000,2048,0}; alloc_calls=erase_calls=write_calls=read_calls=open_calls=close_calls=0; fail_alloc_at=fail_erase_call=fail_write_call=fail_read_call=fail_open=bad_block=0; window_open=0; }
static int check_saved(void)
{ env_t *want=malloc(CONFIG_ENV_SIZE); unsigned char *old=malloc(board_env_size); int ok; env_export(want); memset(old,0x5a,board_env_size); ok=!memcmp(flash+board_env_offset,want,CONFIG_ENV_SIZE) && !memcmp(flash+board_env_offset+CONFIG_ENV_SIZE,old+CONFIG_ENV_SIZE,board_env_size-CONFIG_ENV_SIZE); free(old); free(want); return ok; }
int main(void)
{
 unsigned char old[0x80000]; int r;
 setup(); memcpy(old,flash+board_env_offset,sizeof(old));
 if(rt3_nand_env_tail_matches(flash+board_env_offset+CONFIG_ENV_SIZE,CONFIG_ENV_SIZE)) return 12;
 { unsigned char tail[CONFIG_ENV_SIZE]; memset(tail,0,sizeof(tail)); if(rt3_nand_env_tail_matches(tail,sizeof(tail))!=-EINVAL) return 13; if(rt3_nand_env_tail_matches(tail,sizeof(tail)-1)!=-EINVAL) return 14; }
 r=rt3_nand_saveenv(); if(r || !check_saved() || erase_calls!=1 || write_calls!=1 || read_calls!=4 || open_calls!=1 || close_calls!=1 || window_open || nand_info[0].flags) return 1;
 setup(); board_env_size=0x40000; if(!rt3_nand_saveenv() || erase_calls || open_calls) return 2; board_env_size=0x80000;
 setup(); board_env_offset=0x4c0000; if(!rt3_nand_saveenv() || erase_calls || open_calls) return 9; board_env_offset=0x480000;
 setup(); nand_info[0].size-=0x20000; if(!rt3_nand_saveenv() || erase_calls || open_calls) return 10;
 setup(); nand_info[0].writesize=4096; if(!rt3_nand_saveenv() || erase_calls || open_calls) return 11;
 setup(); bad_block=1; if(!rt3_nand_saveenv() || erase_calls || open_calls) return 3;
 setup(); fail_alloc_at=3; if(!rt3_nand_saveenv() || erase_calls || open_calls || memcmp(flash+board_env_offset,old,sizeof(old))) return 4;
 setup(); fail_open=1; if(!rt3_nand_saveenv() || erase_calls || window_open) return 5;
 setup(); memcpy(old,flash+board_env_offset,sizeof(old)); fail_erase_call=1; r=rt3_nand_saveenv(); if(!r || memcmp(flash+board_env_offset,old,sizeof(old)) || erase_calls!=2 || close_calls!=1) return 6;
 setup(); memcpy(old,flash+board_env_offset,sizeof(old)); fail_write_call=1; r=rt3_nand_saveenv(); if(!r || memcmp(flash+board_env_offset,old,sizeof(old)) || write_calls!=2 || erase_calls!=2) return 7;
 setup(); memcpy(old,flash+board_env_offset,sizeof(old)); fail_read_call=2; r=rt3_nand_saveenv(); if(!r || memcmp(flash+board_env_offset,old,sizeof(old)) || write_calls!=2 || erase_calls!=2) return 8;
 puts("ENV_STORAGE_PASS: OEM 256KiB CRC image + preserved 256KiB tail, exact 512KiB APPSBLENV, tail matcher, fixed geometry/offset, badblock/alloc/open guards, rollback on erase/write/readback failure");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="rt3-env-storage-") as td:
    cfile=pathlib.Path(td)/"test.c"; exe=pathlib.Path(td)/"test"
    cfile.write_text(c)
    subprocess.run(["gcc","-std=gnu99","-Wall","-Wextra","-Werror","-Wno-unused-function","-Wno-sign-compare",str(cfile),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print("ENV_STORAGE_SOURCE_PASS")
