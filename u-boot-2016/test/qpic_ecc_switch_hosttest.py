#!/usr/bin/env python3
"""ASan/UBSan test of the candidate QPIC ECC switch and real snapshot helpers."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path


HARNESS = r"""
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NX30_NOR_NAND_ECC4 4U
#define NX30_NOR_NAND_ECC8 8U
#define NAND_CW_SIZE_4_BIT_ECC 528U
#define NAND_CW_SIZE_8_BIT_ECC 532U
#define CONFIG_QPIC_NAND_NAND_INFO_IDX 0
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#define MTD_NAND_CHIP(mtd) ((struct nand_chip *)(mtd)->priv)
#define abort() do { fprintf(stderr,"assertion failed at %s:%d\n",__FILE__,__LINE__); exit(1); } while (0)

struct nand_ecclayout { uint32_t marker, eccbytes, eccpos[4], oobavail; };
struct nand_chip { struct { struct nand_ecclayout *layout; } ecc; };
struct mtd_info {
    const char *name; void *priv;
    uint64_t size; uint32_t writesize, erasesize, oobsize, oobavail;
    uint32_t ecc_strength, writebufsize, bitflip_threshold, ecc_step_size;
    void *_read, *_read_oob;
};
struct qpic_nand_dev {
    uint32_t density, page_size, spare_size, cws_per_page, cw_size;
    uint32_t bad_blk_loc, ecc_bytes_hw, spare_bytes, bbm_size, oob_per_page;
    uint32_t cfg0,cfg1,cfg0_raw,cfg1_raw,ecc_bch_cfg;
    void *buffers,*pad_dat,*pad_oob,*zero_page,*zero_oob,*tmp_datbuf,*tmp_oobbuf;
};
static struct mtd_info nand_info[1];
static struct qpic_nand_dev qpic_nand_dev;
static struct nand_chip chip;
static struct nand_ecclayout fake_ecc_layout;
static struct qpic_serial_nand_params { unsigned char id[2]; const char *name; } serial;
static struct qpic_serial_nand_params *serial_params=&serial;
static unsigned int physical_spare=128;
static int save_config_ret, init_once_calls, save_config_calls;
static int nx30_nor_nand_init_once(void) { init_once_calls++; return 0; }
static unsigned int h3c_qpic_nand_physical_spare(void) { return physical_spare; }

static int qpic_nand_save_config(struct mtd_info *mtd) {
    struct qpic_nand_dev *d=&qpic_nand_dev;
    struct nand_chip *c=MTD_NAND_CHIP(mtd);
    save_config_calls++;
    d->cws_per_page=4;
    d->cw_size=mtd->ecc_strength==4 ? NAND_CW_SIZE_4_BIT_ECC : NAND_CW_SIZE_8_BIT_ECC;
    d->bad_blk_loc=0x1234; d->ecc_bytes_hw=mtd->ecc_strength==4?7:13;
    d->spare_bytes=mtd->ecc_strength==4?4:2; d->bbm_size=1;
    d->oob_per_page=16; d->cfg0=0x10+d->cw_size; d->cfg1=0x20+d->cw_size;
    d->cfg0_raw=0x30+d->cw_size; d->cfg1_raw=0x40+d->cw_size;
    d->ecc_bch_cfg=0x50+d->cw_size;
    mtd->oobsize=d->cw_size*d->cws_per_page-mtd->writesize;
    mtd->oobavail=16; mtd->writebufsize=mtd->writesize;
    mtd->ecc_step_size=777; /* Simulate a config routine that fails after metadata mutation. */
    c->ecc.layout=&fake_ecc_layout;
    memset(&fake_ecc_layout,0,sizeof(fake_ecc_layout));
    fake_ecc_layout.marker=mtd->ecc_strength; fake_ecc_layout.eccbytes=d->ecc_bytes_hw;
    fake_ecc_layout.oobavail=mtd->oobavail;
    return save_config_ret;
}

/* Exact production snapshot, validation, restore and set_read_ecc code. */
@@FUNCTIONS@@

static void profile_reset(unsigned int bits) {
    memset(&qpic_nand_dev,0,sizeof(qpic_nand_dev));
    memset(&chip,0,sizeof(chip)); memset(&fake_ecc_layout,0,sizeof(fake_ecc_layout));
    memset(&nand_info[0],0,sizeof(nand_info[0]));
    serial.id[0]=0xc8; serial.id[1]=0x41; serial.name="GD5F1GQ5REYIG";
    physical_spare=128; save_config_ret=0; save_config_calls=0; init_once_calls=0;
    qpic_nand_dev.density=0x08000000; qpic_nand_dev.page_size=2048;
    qpic_nand_dev.spare_size=128; qpic_nand_dev.cws_per_page=4;
    qpic_nand_dev.cw_size=bits==4?NAND_CW_SIZE_4_BIT_ECC:NAND_CW_SIZE_8_BIT_ECC;
    qpic_nand_dev.ecc_bytes_hw=bits==4?7:13; qpic_nand_dev.oob_per_page=16;
    qpic_nand_dev.buffers=&qpic_nand_dev; qpic_nand_dev.pad_dat=&qpic_nand_dev;
    qpic_nand_dev.pad_oob=&qpic_nand_dev; qpic_nand_dev.zero_page=&qpic_nand_dev;
    qpic_nand_dev.zero_oob=&qpic_nand_dev; qpic_nand_dev.tmp_datbuf=&qpic_nand_dev;
    qpic_nand_dev.tmp_oobbuf=&qpic_nand_dev;
    fake_ecc_layout.marker=bits; fake_ecc_layout.eccbytes=qpic_nand_dev.ecc_bytes_hw;
    fake_ecc_layout.oobavail=16; chip.ecc.layout=&fake_ecc_layout;
    nand_info[0].name="nand0"; nand_info[0].priv=&chip;
    nand_info[0].size=0x08000000; nand_info[0].writesize=2048;
    nand_info[0].erasesize=0x20000; nand_info[0].oobsize=bits==4?64:80;
    nand_info[0].oobavail=16; nand_info[0].ecc_strength=bits;
    nand_info[0].writebufsize=2048; nand_info[0].bitflip_threshold=bits*3/4;
    nand_info[0].ecc_step_size=0; nand_info[0]._read=(void *)1;
    nand_info[0]._read_oob=(void *)1;
}

static void expect_consistent(unsigned int bits) {
    unsigned int cw=bits==4?NAND_CW_SIZE_4_BIT_ECC:NAND_CW_SIZE_8_BIT_ECC;
    if (nand_info[0].ecc_strength!=bits || nand_info[0].ecc_step_size!=512 ||
        qpic_nand_dev.cws_per_page!=4 || qpic_nand_dev.cw_size!=cw ||
        nand_info[0].oobsize!=cw*4-2048 || nand_info[0].oobavail!=16 ||
        nand_info[0].writebufsize!=2048 || !chip.ecc.layout ||
        chip.ecc.layout->marker!=bits || chip.ecc.layout->oobavail!=16)
        abort();
}

int main(void) {
    struct mtd_info old_mtd; struct qpic_nand_dev old_dev;
    struct nand_ecclayout old_layout; struct nand_ecclayout *old_layout_ptr;
    int calls;

    /* Same-mode call establishes step size without reconfiguring QPIC. */
    profile_reset(4);
    if (nx30_nor_nand_set_read_ecc(4)) abort();
    expect_consistent(4); if (save_config_calls) abort();

    /* Real setter and mocked QPIC config exercise both transitions. */
    if (nx30_nor_nand_set_read_ecc(8)) abort();
    expect_consistent(8);
    if (nx30_nor_nand_set_read_ecc(4)) abort();
    expect_consistent(4);
    if (save_config_calls!=2) abort();

    /* Same-mode state is rejected when layout or codeword geometry is invalid. */
    profile_reset(4); chip.ecc.layout=NULL; calls=save_config_calls;
    if (nx30_nor_nand_set_read_ecc(4)!=-EIO || save_config_calls!=calls) abort();
    profile_reset(4); qpic_nand_dev.cws_per_page=3;
    if (nx30_nor_nand_set_read_ecc(4)!=-EIO) abort();

    /* Unknown strength is rejected before init/configuration. */
    profile_reset(4);
    if (nx30_nor_nand_set_read_ecc(5)!=-EINVAL || init_once_calls || save_config_calls) abort();

    /* Exact-profile bounds: ID, name, spare, density, page and erase must match. */
    profile_reset(4); serial.id[0]=0xef;
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();
    profile_reset(4); serial.name="other-chip";
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();
    profile_reset(4); physical_spare=64;
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();
    profile_reset(4); qpic_nand_dev.density=0x10000000;
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();
    profile_reset(4); nand_info[0].size=0x10000000;
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();
    profile_reset(4); nand_info[0].writesize=4096;
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();
    profile_reset(4); nand_info[0].erasesize=0x10000;
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();
    profile_reset(4); nand_info[0].oobsize=96;
    if (nx30_nor_nand_set_read_ecc(8)!=-ENODEV) abort();

    /* Failed QPIC rebuild must restore every dev/mtd field and old layout bytes. */
    profile_reset(4); old_mtd=nand_info[0]; old_dev=qpic_nand_dev;
    old_layout=*chip.ecc.layout; old_layout_ptr=chip.ecc.layout;
    save_config_ret=-EIO;
    if (nx30_nor_nand_set_read_ecc(8)!=-EIO) abort();
    if (memcmp(&nand_info[0],&old_mtd,sizeof(old_mtd)) ||
        memcmp(&qpic_nand_dev,&old_dev,sizeof(old_dev)) ||
        chip.ecc.layout!=old_layout_ptr || memcmp(old_layout_ptr,&old_layout,sizeof(old_layout))) abort();

    puts("QPIC_ECC_SWITCH_HOSTTEST_PASS");
    return 0;
}
"""


def extract_functions(source):
    start = source.index("#define NX30_NOR_NAND_EXPECTED_SIZE")
    end = source.index("int nx30_nor_nand_restore4(void)", start)
    code = source[start:end]
    init_start = code.index("int nx30_nor_nand_init_once(void)")
    init_end = code.index("static void nx30_nor_nand_save_cfg", init_start)
    return code[:init_start] + code[init_end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    generated = HARNESS.replace("@@FUNCTIONS@@", extract_functions(args.source.resolve().read_text()))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="qpic-ecc-switch-hosttest-") as td:
        cfile=Path(td)/"qpic_ecc_switch_hosttest.c"
        binary=Path(td)/"qpic_ecc_switch_hosttest"
        cfile.write_text(generated)
        subprocess.run(["gcc","-std=gnu11","-O1","-g","-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer","-fno-sanitize-recover=all",
                        str(cfile),"-o",str(binary)],check=True)
        env=os.environ.copy(); env["ASAN_OPTIONS"]="detect_leaks=0"
        completed=subprocess.run([str(binary)],capture_output=True,text=True,env=env)
    if completed.returncode:
        raise SystemExit(completed.stderr or f"host test exited {completed.returncode}")
    report=args.output_dir/"qpic-ecc-switch-hosttest.txt"
    report.write_text(completed.stdout)
    print(completed.stdout.strip())
    print(f"ASAN_UBSAN=PASS output={report}")


if __name__ == "__main__":
    main()
