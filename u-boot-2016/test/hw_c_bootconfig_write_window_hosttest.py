#!/usr/bin/env python3
import subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/"drivers/mtd/nand/qpic_nand.c").read_text()
m=src.index("int rt3_bootconfig_write_window_open")
b=src.index("{",m);d=0
for e in range(b,len(src)):
    if src[e]=="{":d+=1
    elif src[e]=="}":
        d-=1
        if not d: fn=src[m:e+1];break
pre=r"""
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;typedef uint64_t u64;
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define MTD_NANDFLASH 4
#define MTD_WRITEABLE 1
struct mtd_info{int type;u64 size;u32 erasesize,writesize,flags;};
static struct mtd_info nand_info[1];
static struct{struct mtd_info*mtd;u64 start,end;u32 old_flags;int open;}rt3_fw_window;
static int smem_getpart(char*n,u32*o,u32*s){
 if(!strcmp(n,"0:BOOTCONFIG")){*o=8;*s=2;return 0;}
 if(!strcmp(n,"0:BOOTCONFIG1")){*o=10;*s=2;return 0;}return -1;
}
"""+fn+r"""
int main(void){struct mtd_info*m=&nand_info[0];m->type=MTD_NANDFLASH;m->size=128ULL<<20;m->erasesize=128U<<10;m->writesize=2048;
#ifdef CONFIG_H3C_HW_C_PRODUCTION_PORT
 if(rt3_bootconfig_write_window_open(m,"0:BOOTCONFIG",0x100000,0x40000)||!rt3_fw_window.open)return 1;
 rt3_fw_window.open=0;m->flags=0;
 if(rt3_bootconfig_write_window_open(m,"0:BOOTCONFIG1",0x140000,0x40000)||rt3_fw_window.start!=0x140000)return 2;
 rt3_fw_window.open=0;
 if(rt3_bootconfig_write_window_open(m,"0:APPSBL",0x500000,0x40000)>=0)return 3;
 if(rt3_bootconfig_write_window_open(m,"0:BOOTCONFIG",0x140000,0x40000)>=0)return 4;
 if(rt3_bootconfig_write_window_open(m,"0:BOOTCONFIG",0x100000,0x80000)>=0)return 5;
#else
 if(rt3_bootconfig_write_window_open(m,"0:BOOTCONFIG",0x100000,0x40000)!=-EROFS)return 6;
#endif
 puts("HW-C QPIC BOOTCONFIG aperture: PASS");return 0;}
"""
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/"t.c";c.write_text(pre)
 for prof,macro in (("c","CONFIG_H3C_HW_C_PRODUCTION_PORT"),("other","CONFIG_TARGET_IPQ5018_H3C_RT3000_B")):
  x=Path(td)/prof
  subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-Wno-unused-parameter","-Wno-unused-function","-Wno-unused-variable","-D"+macro,str(c),"-o",str(x)],check=True)
  subprocess.run([str(x)],check=True)
