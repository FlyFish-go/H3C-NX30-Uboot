#!/usr/bin/env python3
"""Exercise real D QPIC window-open function with real ECC runtime policy.
Only QPIC register IO and NAND data providers are mocked. No device access.
"""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output-dir', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (root / 'drivers/mtd/nand/qpic_nand.c').read_text()
start = source.index('int nx30_d_qpic_write_window_open(')
end = source.index('\nvoid nx30_d_qpic_write_window_close(', start)
function = source[start:end]
harness = r'''
#define main existing_ecc_runtime_tests
#include "h3c_nand_ecc_runtime_hosttest.c"
#undef main
#define NX30_D_WINDOW_HOST_TEST
#include <nx30_d_qpic_window_core.h>
#include <nx30_d_protect_core.h>
#define CONFIG_QPIC_NAND_NAND_INFO_IDX 0
#define MTD_NANDFLASH 4
#define MTD_WRITEABLE 0x400
#define FLASH_SPI_NAND_BLK_PROCT_ENABLE 0x7c
static struct mtd_info nand_info[1];
static struct {
 struct mtd_info *mtd;
 u32 old_flags;
 struct nx30_d_qpic_window_core core;
 struct nx30_d_protect_state protection;
} nx30_d_nand_window;
static int nx30_d_window_close_error;
static unsigned int protect_reads, protect_writes, protect_value = 0x7c;
static int nx30_d_protect_get(void *context, unsigned int *value)
{ (void)context; protect_reads++; *value=protect_value; return 0; }
static int nx30_d_protect_set(void *context, unsigned int value)
{ (void)context; protect_writes++; protect_value=value; return 0; }
'''
harness += function
harness += r'''
static void geometry(void)
{
 struct mtd_info *mtd=&nand_info[0];
 mtd->type=MTD_NANDFLASH; mtd->size=MTD_SIZE;
 mtd->erasesize=0x20000; mtd->writesize=0x800; mtd->ecc_strength=4;
}
int main(void)
{
 struct mock m;
 unsigned int i;
 for(i=0;i<H3C_NAND_CATALOG_PROFILE_COUNT;i++) {
  const struct h3c_nand_catalog_profile *p=&h3c_nand_catalog_profiles[i];
  setup(&m,&nand_info[0],H3C_NAND_ECC_TEST_BOARD_D);
  geometry();
  m.profile.profile=100+i; m.profile.catalog_profile=p;
  m.profile.physical_spare=p->qpic_raw_spare_bytes;
  m.profile.nand_size=p->capacity_bytes;
  m.profile.page_size=p->page_bytes; m.profile.erase_size=p->erase_bytes;
  h3c_nand_ecc_init();
  assert(nx30_d_qpic_write_window_open(&nand_info[0],APPSBL1,APPSBL_LEN,4)<0);
  assert(!nx30_d_nand_window.core.open);
  assert(!protect_reads && !protect_writes);
  assert(!(nand_info[0].flags&MTD_WRITEABLE));
  cleanup(&m);
 }
 /* Inconsistent ENV/system and unknown profile both stop before A0 IO. */
 setup(&m,&nand_info[0],H3C_NAND_ECC_TEST_BOARD_D); geometry();
 m.sys_mode=8; h3c_nand_ecc_init();
 assert(nx30_d_qpic_write_window_open(&nand_info[0],APPSBL1,APPSBL_LEN,4)<0);
 assert(!protect_reads && !protect_writes && !nx30_d_nand_window.core.open);
 m.sys_mode=4; m.profile.profile=999; h3c_nand_ecc_init();
 assert(nx30_d_qpic_write_window_open(&nand_info[0],APPSBL1,APPSBL_LEN,4)<0);
 assert(!protect_reads && !protect_writes && !nx30_d_nand_window.core.open);
 cleanup(&m);
 /* Control: exact native ECC4 system extent still opens the real aperture. */
 setup(&m,&nand_info[0],H3C_NAND_ECC_TEST_BOARD_D); geometry();
 h3c_nand_ecc_init();
 assert(!nx30_d_qpic_write_window_open(&nand_info[0],0x900000,0x2800000,4));
 assert(nx30_d_nand_window.core.open);
 assert(protect_reads && protect_writes);
 assert(nand_info[0].flags&MTD_WRITEABLE);
 cleanup(&m);
 puts("Real QPIC open + real ECC gate: all catalog APPSBL denied before protection/window; system control PASS");
 return 0;
}
'''
test = out / 'qpic-gate.c'
test.write_text(harness)
exe = out / 'qpic-gate'
subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
 '-DH3C_NAND_ECC_HOST_TEST', '-DH3C_NAND_ECC_RUNTIME_HOST_TEST',
 '-idirafter', str(root/'include'), '-I'+str(root/'test'),
 str(root/'lib/h3c_nand_ecc_core.c'), str(root/'lib/h3c_nand_ecc.c'),
 str(test), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
