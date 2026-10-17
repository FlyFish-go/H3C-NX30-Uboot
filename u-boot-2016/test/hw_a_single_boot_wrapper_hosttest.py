#!/usr/bin/env python3
"""Compile the exact production boot-slot wrapper and test single-layout selector bypass."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/"board/qca/arm/common/cmd_bootqca.c").read_text()
name="static int rt3000b_prepare_boot_slot"
start=source.index(name); brace=source.index("{",start); depth=0; end=None
for i in range(brace,len(source)):
    if source[i]=="{": depth+=1
    elif source[i]=="}":
        depth-=1
        if depth==0: end=i+1; break
assert end
wrapper=source[start:end]
c=r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#define CMD_RET_USAGE 2
#define CMD_RET_FAILURE 1
#define SMEM_BOOT_NAND_FLASH 11
#define SMEM_BOOT_QSPI_NAND_FLASH 12
#define RT3_LAYOUT_SINGLE 2
#define RT3_LAYOUT_DUAL 1
#define RT3_LAYOUT_UNSUPPORTED 0
#define RT3000B_ROOTFS_BOOTARGS "ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs"
#define RT3000B_ROOTFS1_BOOTARGS "ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs"
struct rt3_partition { char name[16]; unsigned int display_index; unsigned long long offset,size; };
struct rt3_layout { int mode,count; struct rt3_partition parts[2]; };
struct flash_info { int flash_type; struct { unsigned long long offset,size; } rootfs; } flash_data,*sfi=&flash_data;
static int selector_calls,default_calls,env_calls;
static char saved_args[128];
static int get_bootconfig(const char *name){selector_calls++;(void)name;return 1;}
static int rt3_layout_read(struct rt3_layout *l){
 memset(l,0,sizeof(*l));l->mode=RT3_LAYOUT_SINGLE;l->count=1;
 strcpy(l->parts[0].name,"rootfs");l->parts[0].display_index=16;
 l->parts[0].offset=0x900000;l->parts[0].size=0x5000000;
 return 0;
}
static int rt3_layout_find(const struct rt3_layout*l,const char*n,const struct rt3_partition**p){
 for(int i=0;i<l->count;i++)if(!strcmp(l->parts[i].name,n)){*p=&l->parts[i];return 0;}
 *p=NULL;return -ENOENT;
}
static int rt3_layout_default_target(const struct rt3_layout*l,int selector,const struct rt3_partition**p){
 (void)l;(void)selector;(void)p;default_calls++;return -EINVAL;
}
static int mock_setenv(const char*k,const char*v){(void)k;env_calls++;snprintf(saved_args,sizeof(saved_args),"%s",v);return 0;}
#define setenv(k,v) mock_setenv(k,v)
''' + wrapper + r'''
#define C(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x);return 1;} } while(0)
int main(void){
 char *bootipq[]={"bootipq"};
 flash_data.flash_type=SMEM_BOOT_QSPI_NAND_FLASH;
 C(rt3000b_prepare_boot_slot(1,bootipq)==0);
 C(selector_calls==0 && default_calls==0);
 C(!strcmp(saved_args,"ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs"));
 C(env_calls==1 && flash_data.rootfs.offset==0x900000 && flash_data.rootfs.size==0x5000000);
 puts("HW-A production boot wrapper single-layout selector=1 host test: PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="hwa-single-boot-") as td:
    src=Path(td)/"test.c";exe=Path(td)/"test";src.write_text(c)
    subprocess.run(["gcc","-std=gnu11","-Wall","-Wextra","-Werror","-o",str(exe),str(src)],check=True)
    subprocess.run([str(exe)],check=True)
