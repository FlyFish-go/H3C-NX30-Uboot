#!/usr/bin/env python3
"""Test the exact U-Boot UBIFS volume-name parser with stub UBI open calls."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "u-boot-2016/fs/ubifs/super.c"


def main():
    source = SOURCE.read_text()
    begin = source.index("static struct ubi_volume_desc *open_ubi(")
    end = source.index("static struct ubifs_info *alloc_ubifs_info", begin)
    parser = source[begin:end]
    harness = r'''
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __UBOOT__ 1
#define UBI_READONLY 1
#define ERR_PTR(e) ((struct ubi_volume_desc *)(intptr_t)(e))
#define IS_ERR(p) ((intptr_t)(p) < 0)
#define PTR_ERR(p) ((int)(intptr_t)(p))
struct ubi_volume_desc { int marker; };
static struct ubi_volume_desc opened;
static unsigned int seen_dev, seen_mode;
static char seen_name[64];
static unsigned int opens;
static unsigned long simple_strtoul(const char *s, char **end, unsigned int base) {
 return strtoul(s, end, base);
}
static struct ubi_volume_desc *ubi_open_volume_nm(int dev, const char *name, int mode) {
 seen_dev=dev; seen_mode=mode; opens++;
 strncpy(seen_name,name,sizeof(seen_name)-1); return &opened;
}
static struct ubi_volume_desc *ubi_open_volume(int dev, int vol, int mode) {
 seen_dev=dev; seen_mode=mode; opens++;
 snprintf(seen_name,sizeof(seen_name),"vol:%d",vol); return &opened;
}
''' + parser + r'''
int main(void) {
 struct ubi_volume_desc *p;
 p=open_ubi("rootfs_data",UBI_READONLY);
 if (!IS_ERR(p) || PTR_ERR(p)!=-EINVAL || opens) return 1;
 p=open_ubi("ubi0:rootfs_data",UBI_READONLY);
 if (p!=&opened || seen_dev!=0 || seen_mode!=UBI_READONLY ||
     strcmp(seen_name,"rootfs_data") || opens!=1) return 2;
 p=open_ubi("ubi:rootfs_data",UBI_READONLY);
 if (p!=&opened || seen_dev!=0 || seen_mode!=UBI_READONLY ||
     strcmp(seen_name,"rootfs_data") || opens!=2) return 3;
 p=open_ubi("ubi2:rootfs_data",UBI_READONLY);
 if (p!=&opened || seen_dev!=2 || seen_mode!=UBI_READONLY ||
     strcmp(seen_name,"rootfs_data") || opens!=3) return 4;
 puts("PASS: production open_ubi parser rejects bare volume names and opens ubi0:NAME/ubi:NAME/ubiX:NAME read-only with the expected device and volume");
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="ubifs-open-name-hosttest-") as tmp:
        cfile, exe = Path(tmp) / "test.c", Path(tmp) / "test"
        cfile.write_text(harness)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        str(cfile), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
