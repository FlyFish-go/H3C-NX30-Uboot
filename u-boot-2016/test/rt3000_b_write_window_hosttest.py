#!/usr/bin/env python3
"""Compile production QPIC aperture functions and the production layout resolver."""
import subprocess
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/"drivers/mtd/nand/qpic_nand.c").read_text()
a=s.index("static struct {\n\tstruct mtd_info *mtd;",s.index("rt3000_b_fw_write.h"))
b=s.index("\n#endif\nstruct bam_desc qpic_cmd_desc_fifo",a)+len("\n#endif")
window="#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)\n"+s[a:b]
c=r"""
#define main layout_fixture_main
#include "rt3000_b_layout_hosttest.c"
#undef main
#define RT3_FW_HOST_TEST 1
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#define MTD_NANDFLASH 1
#define MTD_WRITEABLE 0x400U
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef long long loff_t;
loff_t board_env_offset=0x480000, board_env_size=0x80000;
static uint32_t smem_override_start, smem_override_size;
static int smem_force_fail, smem_mutate_layout;
static const char *smem_override_name;
int smem_getpart(const char *name, uint32_t *start, uint32_t *count) {
 unsigned i; if(smem_force_fail)return -1;
 for(i=0;i<T()->len;i++)if(!strcmp(T()->parts[i].name,name)){*start=T()->parts[i].start;*count=T()->parts[i].size;break;}
 if(i==T()->len)return -1;
 if(smem_override_name&&!strcmp(name,smem_override_name)){*start=smem_override_start;if(smem_override_size)*count=smem_override_size;}
 if(smem_mutate_layout){unsigned a=0,b=0;for(unsigned j=0;j<T()->len;j++){if(!strcmp(T()->parts[j].name,"0:QSEE"))a=j;if(!strcmp(T()->parts[j].name,"0:QSEE_1"))b=j;}uint32_t t=T()->parts[a].start;T()->parts[a].start=T()->parts[b].start;T()->parts[b].start=t;smem_mutate_layout=0;}
 return 0;
}
static void swap_extent(const char *a,const char *b){unsigned i,ia=0,ib=0;for(i=0;i<T()->len;i++){if(!strcmp(T()->parts[i].name,a))ia=i;if(!strcmp(T()->parts[i].name,b))ib=i;}uint32_t t=T()->parts[ia].start;T()->parts[ia].start=T()->parts[ib].start;T()->parts[ib].start=t;}
#include "../failsafe/modules/firmware_rt3000_b_core.c"
"""+window+r"""
int main(void){struct mtd_info *m=&nand_info[0];uint32_t old=0x800;
 fixture(0);m->flags=old;
 C(!rt3_fw_write_window_allows(m,0x900000,2048));
 C(rt3_fw_write_window_open(m,0x780000,0x100000)==-EROFS);C(m->flags==old);
 C(rt3_fw_write_window_open(m,0x900000,0x2800000)==0);C(m->flags==(old|MTD_WRITEABLE));
 C(rt3_fw_write_window_allows(m,0x900000,2048));C(rt3_fw_write_window_allows(m,0x30ff800,2048));
 C(!rt3_fw_write_window_allows(m,0x8ff800,2048));C(!rt3_fw_write_window_allows(m,0x3100000,2048));
 C(!rt3_fw_write_window_allows(&nand_info[1],0x900000,2048));
 C(rt3_fw_write_window_open(m,0x3100000,0x2800000)==-EINVAL);
 rt3_fw_write_window_close();C(m->flags==old);C(!rt3_fw_write_window_allows(m,0x900000,2048));
 rt3_fw_write_window_close();C(m->flags==old);
 C(rt3_fw_write_window_open(m,0x3100000,0x2800000)==0);rt3_fw_write_window_close();C(m->flags==old);
 fixture(0);qca_smem_flash_info.flash_block_size=0x20000;
 C(rt3_metadata_write_window_open(m,"0:CDT",0x400000,0x40000)==0);rt3_fw_write_window_close();
 C(rt3_metadata_write_window_open(m,"0:CDT_1",0x440000,0x40000)==0);rt3_fw_write_window_close();
 C(rt3_metadata_write_window_open(m,"0:APPSBLENV",0x480000,0x80000)==0);rt3_fw_write_window_close();
 C(rt3_metadata_write_window_open(m,"0:APPSBL",0x500000,0x140000)==0);rt3_fw_write_window_close();
 { int z=rt3_metadata_write_window_open(m,"0:ART",0x780000,0x100000); if(z){fprintf(stderr,"valid ART returned %d\n",z);return 1;} }rt3_fw_write_window_close();
  fixture(0);qca_smem_flash_info.flash_block_size=0x20000;swap_extent("0:CDT","0:CDT_1");swap_extent("0:APPSBL","0:APPSBL_1");
  C(rt3_metadata_write_window_open(m,"0:CDT",0x440000,0x40000)==0);rt3_fw_write_window_close();
  C(rt3_metadata_write_window_open(m,"0:CDT_1",0x400000,0x40000)==0);rt3_fw_write_window_close();
  C(rt3_metadata_write_window_open(m,"0:APPSBL",0x640000,0x140000)==0);rt3_fw_write_window_close();
  C(rt3_metadata_write_window_open(m,"0:APPSBL",0x500000,0x140000)==-EROFS);
  C(rt3_metadata_write_window_open(m,"0:APPSBL_1",0x500000,0x140000)==0);rt3_fw_write_window_close();
  C(rt3_metadata_write_window_open(m,"0:APPSBL_1",0x640000,0x140000)==-EROFS);
  C(rt3_metadata_write_window_open(m,"0:CDT",0x400000,0x40000)==-EROFS);
  fixture(0);qca_smem_flash_info.flash_block_size=0x20000;
 C(rt3_metadata_write_window_open(m,"0:APPSBL_1",0x640000,0x140000)==0);rt3_fw_write_window_close();
 C(rt3_metadata_write_window_open(m,"0:APPSBL_1",0x500000,0x140000)==-EROFS);
 C(rt3_metadata_write_window_open(m,"0:APPSBL_1",0x640000,0x120000)==-EROFS);
 C(rt3_metadata_write_window_open(m,"0:APPSBLENV",0x4c0000,0x80000)==-EROFS);
 C(rt3_metadata_write_window_open(m,"0:ART",0x7c0000,0x100000)==-EROFS);
 smem_override_name="0:APPSBLENV";smem_override_start=0x4c0000/0x20000;
 C(rt3_metadata_write_window_open(m,"0:APPSBLENV",0x4c0000,0x80000)==-EROFS);
 smem_override_name=NULL;
 m->size=0x8000000-0x20000;C(rt3_metadata_write_window_open(m,"0:ART",0x780000,0x100000)==-EROFS);m->size=0x8000000;
 m->writesize=4096;C(rt3_metadata_write_window_open(m,"0:ART",0x780000,0x100000)==-EROFS);m->writesize=2048;
 m->erasesize=0x10000;C(rt3_metadata_write_window_open(m,"0:ART",0x780000,0x100000)!=0);m->erasesize=0x20000;
 fixture(1);C(rt3_fw_write_window_open(m,0x900000,0x5000000)==0);rt3_fw_write_window_close();C(m->flags==old);
 C(rt3_fw_write_window_open(m,0x3100000,0x2800000)==-EROFS);C(m->flags==old);
 fixture(0);m->erasesize=0;C(rt3_fw_write_window_open(m,0x900000,0x2800000)==-EINVAL);C(m->flags==old);
 fixture(0);T()->parts[15].size=1;C(rt3_fw_write_window_open(m,0x900000,0x2800000)==-EROFS);C(m->flags==old);

 /* Every named partition is accepted at its exact full extent. */
 fixture(0);m->flags=old;
 for(unsigned i=0;i<T()->len;i++){
  const char *n=T()->parts[i].name;uint64_t o=(uint64_t)T()->parts[i].start*ERASE,z=(uint64_t)T()->parts[i].size*ERASE;
  C(rt3_named_nand_write_window_open(m,n,o,z)==0);C(m->flags==(old|MTD_WRITEABLE));
  C(rt3_fw_write_window_allows(m,o,z));rt3_fw_write_window_close();C(m->flags==old);
 }
 /* Names stay bound to their table rows when known paired extents swap. */
 fixture(0);swap_extent("0:QSEE","0:QSEE_1");swap_extent("0:DEVCFG","0:DEVCFG_1");swap_extent("0:CDT","0:CDT_1");swap_extent("0:APPSBL","0:APPSBL_1");swap_extent("rootfs","rootfs_1");
 for(unsigned i=0;i<T()->len;i++){
  const char *n=T()->parts[i].name;uint64_t o=(uint64_t)T()->parts[i].start*ERASE,z=(uint64_t)T()->parts[i].size*ERASE;
  C(rt3_named_nand_write_window_open(m,n,o,z)==0);rt3_fw_write_window_close();C(m->flags==old);
 }
 fixture(1);
 for(unsigned i=0;i<T()->len;i++){
  const char *n=T()->parts[i].name;uint64_t o=(uint64_t)T()->parts[i].start*ERASE,z=(uint64_t)T()->parts[i].size*ERASE;
  C(rt3_named_nand_write_window_open(m,n,o,z)==0);rt3_fw_write_window_close();C(m->flags==old);
 }
 C(rt3_named_nand_write_window_open(m,"rootfs_1",0x3100000,0x2800000)==-EROFS);
 C(rt3_named_nand_write_window_open(m,"0:NOT_A_PART",0,ERASE)==-EROFS);
 C(rt3_named_nand_write_window_open(m,"0:SBL1-extra-name",0,ERASE)==-EINVAL);
 C(rt3_named_nand_write_window_open(m,"0:SBL1",0x20000,0x80000)==-EROFS);
 C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x40000)==-EROFS);
 C(rt3_named_nand_write_window_open(&nand_info[1],"0:SBL1",0,0x80000)==-EINVAL);
 C(rt3_named_nand_write_window_open(m,"0:SBL1",m->size-ERASE,2*ERASE)==-EINVAL);
 smem_override_name="0:SBL1";smem_override_start=1;smem_override_size=0;
 C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EROFS);
 smem_override_start=0;smem_override_size=3;C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EROFS);
 smem_override_name=NULL;smem_override_size=0;
 smem_force_fail=1;C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EROFS);smem_force_fail=0;
 fixture(0);smem_mutate_layout=1;
 C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EROFS);C(m->flags==old);
 fixture(0);T()->len--;
 C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EROFS);
 fixture(0);strcpy(T()->parts[1].name,"0:SBL1");
 C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EROFS);
 fixture(0);T()->parts[0].start=0xffffffffU;
 C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EROFS);
 fixture(0);m->size-=ERASE;C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EINVAL);m->size+=ERASE;
 fixture(0);m->writesize=4096;C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EINVAL);m->writesize=2048;
 fixture(0);m->erasesize=0;C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==-EINVAL);m->erasesize=ERASE;
 fixture(0);C(rt3_named_nand_write_window_open(m,"0:SBL1",0,0x80000)==0);
 C(rt3_named_nand_write_window_open(m,"0:MIBIB",0x80000,0x80000)==-EINVAL);
 rt3_fw_write_window_close();C(m->flags==old);
 fixture(0);m->flags=old;
 C(rt3_bootconfig_write_window_open(m,"0:BOOTCONFIG",0x100000,0x40000)==-EROFS);
 puts("RT3000-B production QPIC firmware and exact metadata window host tests: PASS");return 0;}
"""
with tempfile.TemporaryDirectory(prefix="rt3-aperture-") as t:
 p=Path(t)/"test.c";x=Path(t)/"test";p.write_text(c)
 subprocess.run(["gcc","-std=gnu99","-Wall","-Wextra","-I"+str(root/"test/rt3000_b_layout_stubs"),"-I"+str(root/"include"),"-I"+str(root/"test"),str(p),"-o",str(x)],check=True)
 subprocess.run([str(x)],check=True)
