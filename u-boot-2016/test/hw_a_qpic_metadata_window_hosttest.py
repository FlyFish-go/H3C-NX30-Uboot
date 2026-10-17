#!/usr/bin/env python3
"""Exercise the actual A-production metadata aperture from QPIC."""
import subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/"drivers/mtd/nand/qpic_nand.c").read_text()
a=s.index("int rt3_metadata_write_window_open("); b=s.index("{",a); d=0
for e in range(b,len(s)):
    if s[e]=="{": d+=1
    elif s[e]=="}":
        d-=1
        if not d: fn=s[a:e+1]; break
pre=r"""
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define CONFIG_H3C_HW_A_PRODUCTION_PORT 1
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define RT3_LAYOUT_PARTITION_NAME_LEN 32
#define RT3_LAYOUT_UNSUPPORTED 2
#define MTD_WRITEABLE 1
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef uint32_t u32; typedef uint64_t u64; typedef long long loff_t;
struct mtd_info {u64 size;u32 erasesize,writesize,flags;};
static struct mtd_info nand_info[1];
static struct {u32 flash_block_size;} qca_smem_flash_info;
static loff_t board_env_offset=0x480000, board_env_size=0x80000;
static struct {struct mtd_info*mtd;u64 start,end;u32 old_flags;int open;} rt3_fw_window;
struct rt3_layout {int mode;unsigned count;};
static int rt3_layout_read(struct rt3_layout*l){l->mode=0;l->count=1;return 0;}
static int smem_getpart(char*n,u32*o,u32*z){
 if(!strcmp(n,"0:APPSBLENV")){*o=36;*z=4;return 0;}
 if(!strcmp(n,"0:APPSBL")){*o=40;*z=10;return 0;}
 if(!strcmp(n,"0:ART")){*o=60;*z=8;return 0;}
 if(!strcmp(n,"0:CDT")){*o=32;*z=2;return 0;}
 if(!strcmp(n,"0:CDT_1")){*o=34;*z=2;return 0;}return -1;
}
""" + fn + r"""
#define C(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static void close_win(void){if(rt3_fw_window.open)rt3_fw_window.mtd->flags=rt3_fw_window.old_flags;memset(&rt3_fw_window,0,sizeof(rt3_fw_window));}
int main(void){
 struct mtd_info*m=&nand_info[0],other;
 m->size=128ULL<<20;m->erasesize=128U<<10;m->writesize=2048;m->flags=0x80;qca_smem_flash_info.flash_block_size=m->erasesize;
 C(rt3_metadata_write_window_open(m,"0:APPSBL",0x500000,0x140000)==0);close_win();
 C(rt3_metadata_write_window_open(m,"0:APPSBL",0x640000,0x140000)==-EROFS);
 C(rt3_metadata_write_window_open(m,"0:APPSBL_1",0x640000,0x140000)==-EROFS);
 C(rt3_metadata_write_window_open(m,"0:ART",0x780000,0x100000)==0);close_win();
 C(rt3_metadata_write_window_open(m,"0:CDT",0x400000,0x40000)==0);close_win();
 C(rt3_metadata_write_window_open(m,"0:CDT_1",0x440000,0x40000)==0);close_win();
 C(rt3_metadata_write_window_open(m,"0:APPSBLENV",0x480000,0x80000)==0);close_win();
 C(m->flags==0x80);
 C(rt3_metadata_write_window_open(m,"0:ART",0x780000,0x80000)==-EROFS);
 C(rt3_metadata_write_window_open(m,"0:NOT_A_PART",0,0x20000)==-EROFS);
 other=*m;C(rt3_metadata_write_window_open(&other,"0:ART",0x780000,0x100000)==-EINVAL);
 puts("HW-A production primary-only QPIC metadata aperture: PASS");return 0;
}
"""
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/"t.c";x=Path(td)/"t";c.write_text(pre)
 subprocess.run(["cc","-std=gnu99","-Wall","-Wextra","-Werror","-Wno-unused-parameter",str(c),"-o",str(x)],check=True)
 subprocess.run([str(x)],check=True)
