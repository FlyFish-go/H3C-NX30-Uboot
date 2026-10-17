#!/usr/bin/env python3
"""Host-compile the production named NAND writer against mocked MTD APIs."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "common/cmd_flashrw.c").read_text()
start = source.index("static int rt3_parse_hex_u32")
end = source.index("\n#endif\n\nstatic int write_to_flash", start)
production = source[start:end]
write_start = source.index("static int write_to_flash(")
erase_start = source.index("static int fl_erase(", write_start)
erase_end = source.index("\n#ifdef IPQ_UBI_VOL_WRITE_SUPPORT", erase_start)
legacy_helpers = source[write_start:erase_end]
do_start = source.index("static int do_flash(")
do_end = source.index("\n#ifdef CONFIG_IPQ_MIBIB_RELOAD", do_start)
do_flash = source[do_start:do_end]
fixture = r"""
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#define ARCH_DMA_MINALIGN 64
#define MTD_WRITEABLE 0x400U
#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#define CONFIG_CMD_NAND 1
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define CMD_RET_SUCCESS 0
#define CMD_RET_FAILURE 1
#define CMD_RET_USAGE 2
#define SMEM_BOOT_NAND_FLASH 2
#define SMEM_BOOT_QSPI_NAND_FLASH 11
#define SMEM_BOOT_SPI_FLASH 6
#define SMEM_BOOT_MMC_FLASH 5
#define IPQ_NAND_ROOTFS_SIZE 0x100000
typedef unsigned char u8;
typedef unsigned long ulong;
typedef uint32_t u32;
#define U32_MAX ((u32)~0U)
typedef unsigned long long u64;
struct mtd_info { u64 size; unsigned int erasesize, writesize, flags; };
struct erase_info { struct mtd_info *mtd; loff_t addr; u64 len; };
struct gd_fixture { size_t ram_size; };
static struct gd_fixture gd_value = { (size_t)-1 };
struct gd_fixture *gd = &gd_value;
#define CONFIG_SYS_SDRAM_BASE 0
static struct mtd_info mtd = { 0x10000, 0x1000, 0x100, 0x20 };
static struct mtd_info nand_info[1] = {{ 0x10000, 0x1000, 0x100, 0x20 }};
static unsigned char flash[0x10000], input[0x3000];
static int open_calls, close_calls, erase_calls, write_calls, read_calls;
static int bad_at = -1, erase_error_at = -1, write_error_at = -1;
static int short_write, read_error_at = -1, short_read, tamper_at = -1;
static int read_euclean, active, fail_memory, fail_address;
static size_t alloc_size;
static int alias_scratch;
static unsigned char scratch_area[0x200] __attribute__((aligned(64)));
static void test_free(void *p) { if (p != scratch_area) free(p); }
#define free test_free
static unsigned old_flags;
static struct mtd_info *window_mtd;
static u64 expected_offset=0x1000, expected_size=0x2000;
static const char *opened_name;
static int legacy_run_calls;
static u32 smem_start=1, smem_blocks=2;
static const char *const names[] = {
 "0:SBL1", "0:MIBIB", "0:BOOTCONFIG", "0:BOOTCONFIG1", "0:QSEE",
 "0:QSEE_1", "0:DEVCFG", "0:DEVCFG_1", "0:CDT", "0:CDT_1",
 "0:APPSBLENV", "0:APPSBL", "0:APPSBL_1", "0:ART", "0:TRAINING",
 "rootfs", "rootfs_1", "pdt_data", "pdt_data_1", "exp", "plugin"
};
static int allowed_name(const char *name) {
 unsigned i; for (i=0;i<sizeof(names)/sizeof(names[0]);i++) if(!strcmp(name,names[i])) return 1; return 0;
}
void *memalign(size_t align, size_t size) { void *p=NULL; alloc_size=size; if(alias_scratch) return scratch_area; if(posix_memalign(&p,align,size)) return NULL; return p; }
int is_load_addr_valid(uintptr_t addr) { return !fail_address && (addr==(uintptr_t)input || (alias_scratch && addr==(uintptr_t)scratch_area)); }
int is_memory_region_available(uintptr_t addr, size_t size) { return !fail_memory && (addr==(uintptr_t)input || (alias_scratch && addr==(uintptr_t)scratch_area)) && size<=sizeof(input); }
int rt3_named_nand_write_window_open(struct mtd_info *m,const char *name,u64 off,u64 size) {
 open_calls++; if(active || (m!=&mtd && m!=&nand_info[0]) || !allowed_name(name) || off!=expected_offset || size!=expected_size) return -EROFS;
 active=1; opened_name=name; window_mtd=m; old_flags=m->flags; m->flags|=MTD_WRITEABLE; return 0;
}
void rt3_fw_write_window_close(void) { if(active) { window_mtd->flags=old_flags; active=0; close_calls++; } }
int mtd_block_isbad(struct mtd_info *m, loff_t off) { (void)m; return (int)off==bad_at ? 1 : 0; }
int mtd_erase(struct mtd_info *m, struct erase_info *e) {
 (void)m; erase_calls++; if((int)e->addr==erase_error_at) return -EIO;
 memset(flash+e->addr,0xff,e->len); return 0;
}
int mtd_write(struct mtd_info *m, loff_t off, size_t len, size_t *done, const u8 *buf) {
 (void)m; write_calls++; if((int)off==write_error_at) return -EIO;
 if(short_write) { short_write=0; *done=len-1; return 0; }
 memcpy(flash+off,buf,len); *done=len; return 0;
}
int mtd_read(struct mtd_info *m, loff_t off, size_t len, size_t *done, u8 *buf) {
 (void)m; read_calls++; if((int)off==read_error_at) return -EBADMSG;
 if(short_read) { short_read=0; *done=len-1; return 0; }
 memcpy(buf,flash+off,len); if((int)off==tamper_at) buf[0]^=1; *done=len;
 if(read_euclean) { read_euclean=0; return -EUCLEAN; } return 0;
}
typedef struct { u64 offset; } rootfs_fixture_t;
typedef struct { u32 flash_type, flash_block_size, flash_secondary_type; rootfs_fixture_t rootfs; } qca_smem_flash_info_t;
static qca_smem_flash_info_t qca_smem_flash_info;
typedef struct { unsigned int blksz; } disk_partition_t;
typedef struct cmd_tbl cmd_tbl_t;
typedef struct mtd_info nand_info_t;
static uint32_t flash_type_new = -1;
static const char *flash_type_to_string(uint32_t type) { (void)type; return "nand"; }
static unsigned long simple_strtoul(const char *s, char **end, unsigned int base) { return strtoul(s,end,base); }
static int smem_getpart(char *name, uint32_t *start, uint32_t *count) {
 if (!allowed_name(name)) return -ENOENT;
 *start=smem_start; *count=smem_blocks; return 0;
}
static int getpart_offset_size(char *name, uint32_t *off, uint32_t *size) { (void)name;(void)off;(void)size; return -ENOENT; }
static int get_which_flash_param(char *name) { (void)name; return 0; }
static int smem_bootconfig_info(void) { return -1; }
static unsigned int get_rootfs_active_partition(void) { return 0; }
int run_command(const char *cmd,int flag) { (void)cmd;(void)flag; legacy_run_calls++; return 0; }
#define C(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
"""
command_fixture = r"""
static void setup_command(uint32_t flash_type) {
 qca_smem_flash_info.flash_type=flash_type;
 qca_smem_flash_info.flash_block_size=0x1000;
 qca_smem_flash_info.flash_secondary_type=0;
 qca_smem_flash_info.rootfs.offset=0;
 smem_start=1; smem_blocks=2;
 expected_offset=0x1000; expected_size=0x2000;
 nand_info[0]=mtd;
}
"""
main = r"""
int main(void) {
 u32 value; unsigned i; int before; char addr[32];
 memset(flash,0x00,sizeof(flash)); for(i=0;i<sizeof(input);i++) input[i]=(u8)(i*13+7);
 C(rt3_parse_hex_u32("0xAbCd",&value)==0 && value==0xabcd);
 C(rt3_parse_hex_u32("123xyz",&value)!=0); C(rt3_parse_hex_u32("0x",&value)!=0);
 C(rt3_parse_hex_u32("-1",&value)!=0); C(rt3_parse_hex_u32("100000000",&value)!=0);
 /* Every current SMEM NAND0 name reaches the same exact-window writer. */
 for(i=0;i<sizeof(names)/sizeof(names[0]);i++) {
  memset(flash,0x00,sizeof(flash)); read_euclean=(i==0);
  C(rt3_named_nand_write_partition(&mtd,names[i],0x1000,0x2000,(uintptr_t)input,0x123,1)==0);
  C(opened_name==names[i] && close_calls==(int)i+1 && !active && mtd.flags==0x20);
  C(!memcmp(flash+0x1000,input,0x123));
  for(unsigned j=0x123;j<0x2000;j++) C(flash[0x1000+j]==0xff);
  C(alloc_size==2*mtd.writesize);
 }
 { int eb=erase_calls, wb=write_calls, rb=read_calls;
 C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,sizeof(input),1)==-EINVAL);
 C(erase_calls==eb && write_calls==wb && read_calls==rb && !active); }
 { int eb=erase_calls, wb=write_calls, rb=read_calls;
 fail_address=1; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EINVAL); fail_address=0;
 fail_memory=1; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EINVAL); fail_memory=0;
 C(erase_calls==eb && write_calls==wb && read_calls==rb && !active); }
 /* Scratch memory overlapping the source is rejected before erase. */
 { int eb=erase_calls, wb=write_calls, rb=read_calls, cb=close_calls;
 alias_scratch=1; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)scratch_area,1,1)==-EINVAL); alias_scratch=0;
 C(erase_calls==eb && write_calls==wb && read_calls==rb && close_calls==cb && !active); }
 /* A pre-existing window is rejected and is never closed by this command. */
 { int eb=erase_calls, wb=write_calls, rb=read_calls, cb=close_calls;
 active=1; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EROFS);
 C(active && close_calls==cb && erase_calls==eb && write_calls==wb && read_calls==rb); active=0; }
 before=erase_calls; bad_at=0x2000;
 C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EIO);
 C(erase_calls==before && !active); bad_at=-1;
 erase_error_at=0x2000; before=write_calls;
 C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EIO);
 C(write_calls==before && !active && mtd.flags==0x20); erase_error_at=-1;
 short_write=1; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EIO);
 C(!active && mtd.flags==0x20);
 read_error_at=0x1000; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EBADMSG); read_error_at=-1;
 short_read=1; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EIO);
 tamper_at=0x1000; C(rt3_named_nand_write_partition(&mtd,"0:ART",0x1000,0x2000,(uintptr_t)input,1,1)==-EIO); tamper_at=-1;
 /* Erase-only command also verifies all bytes are 0xff and closes the window. */
 memset(flash,0x00,sizeof(flash));
 C(rt3_named_nand_write_partition(&mtd,"0:SBL1",0x1000,0x2000,0,0,0)==0);
 for(i=0x1000;i<0x3000;i++) C(flash[i]==0xff);
 C(!active && mtd.flags==0x20);
 /* Exercise real do_flash dispatch for flash types 11 and 2. */
 {
  char size[32];
  char *argv4[]={(char *)"flash",(char *)"0:APPSBLENV",addr,size};
  int before_e=erase_calls, before_w=write_calls, before_r=read_calls;
  setup_command(SMEM_BOOT_QSPI_NAND_FLASH);
  snprintf(addr,sizeof(addr),"%lx",(ulong)(uintptr_t)input);
  strcpy(size,"123");
  memset(flash,0x00,sizeof(flash));
  C(do_flash(NULL,0,4,argv4)==CMD_RET_SUCCESS);
  C(!strcmp(opened_name,"0:APPSBLENV") && open_calls>0 && close_calls>0);
  C(erase_calls==before_e+2 && write_calls>before_w && read_calls>before_r);
  C(!legacy_run_calls);
  C(!memcmp(flash+0x1000,input,0x123));
 }
 {
  char *argv2[]={(char *)"flash",(char *)"0:ART"};
  int before_e=erase_calls, before_w=write_calls, before_r=read_calls;
  setup_command(SMEM_BOOT_NAND_FLASH);
  snprintf(addr,sizeof(addr),"%lx",(ulong)(uintptr_t)input);
  C(setenv("fileaddr",addr,1)==0 && setenv("filesize","321",1)==0);
  memset(flash,0x00,sizeof(flash));
  C(do_flash(NULL,0,2,argv2)==CMD_RET_SUCCESS);
  C(!strcmp(opened_name,"0:ART") && erase_calls==before_e+2);
  C(write_calls>before_w && read_calls>before_r);
  C(!legacy_run_calls);
 }
 {
  char *argv2[]={(char *)"flasherase",(char *)"0:MIBIB"};
  int before_e=erase_calls, before_w=write_calls, before_r=read_calls;
  setup_command(SMEM_BOOT_QSPI_NAND_FLASH);
  memset(flash,0x00,sizeof(flash));
  C(do_flash(NULL,0,2,argv2)==CMD_RET_SUCCESS);
  C(!strcmp(opened_name,"0:MIBIB") && erase_calls==before_e+2);
  C(write_calls==before_w && read_calls>before_r);
  C(!legacy_run_calls);
 }
 {
  char *argv2[]={(char *)"flash",(char *)"0:ART"};
  int before_e=erase_calls, before_o=open_calls;
  setup_command(SMEM_BOOT_NAND_FLASH);
  unsetenv("fileaddr"); setenv("filesize","10",1);
  C(do_flash(NULL,0,2,argv2)==CMD_RET_USAGE);
  C(erase_calls==before_e && open_calls==before_o);
  setenv("fileaddr",addr,1); setenv("filesize","10junk",1);
  C(do_flash(NULL,0,2,argv2)==CMD_RET_USAGE);
  C(erase_calls==before_e && open_calls==before_o);
  {
   char *argv4[]={(char *)"flash",(char *)"0:ART",addr,(char *)"100000000"};
   C(do_flash(NULL,0,4,argv4)==CMD_RET_USAGE);
   C(erase_calls==before_e && open_calls==before_o);
  }
  {
   char *argv4[]={(char *)"flash",(char *)"0:NOT_A_PART",addr,(char *)"10"};
   C(do_flash(NULL,0,4,argv4)!=CMD_RET_SUCCESS);
   C(erase_calls==before_e && open_calls==before_o);
  }
 }
 {
  char *argv4[]={(char *)"flash",(char *)"0:ART",addr,(char *)"10"};
  int before_e=erase_calls, before_o=open_calls;
  setup_command(SMEM_BOOT_NAND_FLASH);
  smem_start=0x100000;
  C(do_flash(NULL,0,4,argv4)==CMD_RET_FAILURE);
  C(erase_calls==before_e && open_calls==before_o);
 }
 puts("production named NAND writer and do_flash entry host test: PASS"); return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="named-nand-") as tmp:
    cfile = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    cfile.write_text(fixture + production + legacy_helpers + command_fixture + do_flash + main)
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-Wno-unused-parameter", "-no-pie", str(cfile), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
