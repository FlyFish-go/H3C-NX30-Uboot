#!/usr/bin/env python3
"""Keep firmware qualification independent of forbidden APPSBL writes."""
import argparse
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output-dir', type=Path)
a = p.parse_args()
u = Path(__file__).resolve().parents[1]
s = (u / 'lib/nx30_d_firmware.c').read_text()
start = s.index('static int nx30_d_mtd_ready(')
end = s.index('\nstatic int nx30_d_rebuild(', start)
function = s[start:end]
out = a.output_dir.resolve() if a.output_dir else Path(tempfile.mkdtemp(prefix='h3c-fw-ecc-scope-'))
out.mkdir(parents=True, exist_ok=True)
source = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#define MTD_NANDFLASH 4
#define NX30_D_ERASE_SIZE 0x20000U
#define NX30_D_UBI_PAGE_SIZE 2048U
#define NX30_D_SLOT_BYTES 0x2800000U
struct mtd_info { int type; uint64_t size; unsigned int erasesize,writesize,ecc_strength; };
static unsigned int calls, selects, denied;
static int h3c_nand_ecc_select_mode(unsigned int mode)
{ selects++; return mode == 4 ? 0 : -EINVAL; }
static int h3c_nand_ecc_write_allowed(struct mtd_info *m, uint64_t off,uint64_t size)
{
 (void)m; calls++;
 /* Mimic the corrected D policy: APPSBL locked, native system paths usable. */
 if (size != NX30_D_SLOT_BYTES || (off != 0x900000 && off != 0x3100000)) return 0;
 return denied != (off == 0x900000 ? 1U : 2U);
}
'''
source += function
source += r'''
int main(void)
{
 struct mtd_info m={MTD_NANDFLASH,0x8000000,0x20000,2048,4};
 assert(!nx30_d_mtd_ready(&m)); assert(calls==2 && selects==1);
 denied=1; assert(nx30_d_mtd_ready(&m)==-EPERM);
 denied=2; assert(nx30_d_mtd_ready(&m)==-EPERM);
 denied=0; m.ecc_strength=8; assert(nx30_d_mtd_ready(&m)==-ENODEV);
 assert(nx30_d_mtd_ready(NULL)==-ENODEV);
 puts("Firmware ECC scope: system qualification survives APPSBL lock; bad system/ECC refused");
 return 0;
}
'''
(out/'scope.c').write_text(source)
subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(out/'scope.c'),'-o',str(out/'scope')],check=True)
subprocess.run([str(out/'scope')],check=True)
