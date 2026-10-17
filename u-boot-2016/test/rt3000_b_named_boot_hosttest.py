#!/usr/bin/env python3
"""Compile the actual named boot helper with the production layout resolver."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
text = (root / "board/qca/arm/common/cmd_bootqca.c").read_text()
start = text.index("/* Named selection is resolved")
end = text.index("\n#endif /* CONFIG_TARGET_IPQ5018_H3C_RT3000_B */", start)
helper = text[start:end]
source = r"""
#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#define RT3_BOOT_BOARD_LABEL "H3C HW-B"
#define main layout_fixture_main
#include "rt3000_b_layout_hosttest.c"
#undef main
#define CMD_RET_USAGE -1
#define CMD_RET_FAILURE 1
struct boot_flash { unsigned flash_type; struct { uint64_t offset,size; } rootfs; } boot_flash;
static struct boot_flash *sfi = &boot_flash;
static int setenv_failed, setenv_calls;
static char envkey[64], envvalue[256];
static int selector_value, selector_calls;
static int mock_get_bootconfig(const char *name) {
 selector_calls++;
 if (strcmp(name, "rootfs")) return -ENOENT;
 return selector_value;
}
#define get_bootconfig mock_get_bootconfig
static int mock_setenv(const char *key, const char *value) {
 setenv_calls++; snprintf(envkey,sizeof(envkey),"%s",key);
 snprintf(envvalue,sizeof(envvalue),"%s",value); return setenv_failed;
}
#define setenv mock_setenv
""" + helper + r"""
int main(void) {
 char *a[] = {"bootipq","rootfs",0};
 char *automatic[] = {"bootipq",0};
 uint64_t old_offset;
 int old_setenv_calls, old_selector_calls;
 fixture(0);sfi->flash_type=11;
 C(rt3000b_prepare_boot_slot(2,a)==0);
 C(sfi->rootfs.offset==0x900000 && sfi->rootfs.size==0x2800000);
 C(!strcmp(envkey,"fsbootargs") && strstr(envvalue,"ubi.mtd=rootfs "));
 a[1]="rootfs_1";C(rt3000b_prepare_boot_slot(2,a)==0);
 C(sfi->rootfs.offset==0x3100000 && strstr(envvalue,"ubi.mtd=rootfs_1 "));
 a[1]="mtd15";C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_USAGE);
 a[1]="mtd16";C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_USAGE);
 a[1]="mtd17";C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_USAGE);
 a[1]="rootfs;saveenv";C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_USAGE);
 C(selector_calls==0); /* Explicit targets are unchanged and need no BOOTCONFIG. */

 fixture(0);sfi->flash_type=11;selector_value=0;
 C(rt3000b_prepare_boot_slot(1,automatic)==0);
 C(selector_calls==1 && sfi->rootfs.offset==0x900000);
 C(strstr(envvalue,"ubi.mtd=rootfs ")!=NULL);
 fixture(0);sfi->flash_type=11;selector_value=1;
 C(rt3000b_prepare_boot_slot(1,automatic)==0);
 C(selector_calls==2 && sfi->rootfs.offset==0x3100000);
 C(strstr(envvalue,"ubi.mtd=rootfs_1 ")!=NULL);

 /* Selector identifies physical slot even when SMEM logical names are swapped. */
 for (selector_value=0;selector_value<=1;selector_value++) {
  const char *logical = selector_value ? "rootfs" : "rootfs_1";
  const char *expected_args = selector_value ?
   "ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs" :
   "ubi.mtd=rootfs_1 root=mtd:ubi_rootfs rootfstype=squashfs";
  uint64_t physical = selector_value ? 0x3100000 : 0x900000;
  fixture(0);sfi->flash_type=11;
  {
   char saved[RT3_LAYOUT_PARTITION_NAME_LEN];
   snprintf(saved,sizeof(saved),"%s",T()->parts[15].name);
   snprintf(T()->parts[15].name,sizeof(T()->parts[15].name),"%s",T()->parts[16].name);
   snprintf(T()->parts[16].name,sizeof(T()->parts[16].name),"%s",saved);
  }
  C(rt3000b_prepare_boot_slot(1,automatic)==0);
  C(sfi->rootfs.offset==physical && sfi->rootfs.size==0x2800000);
  C(!strcmp(envvalue,expected_args));
  C(strstr(envvalue,logical) != NULL);
 }

 /* Explicit names remain logical aliases when their physical rows are swapped. */
 fixture(0);sfi->flash_type=11;
 {
  char saved[RT3_LAYOUT_PARTITION_NAME_LEN];
  snprintf(saved,sizeof(saved),"%s",T()->parts[15].name);
  snprintf(T()->parts[15].name,sizeof(T()->parts[15].name),"%s",T()->parts[16].name);
  snprintf(T()->parts[16].name,sizeof(T()->parts[16].name),"%s",saved);
 }
 a[1]="rootfs";C(rt3000b_prepare_boot_slot(2,a)==0);
 C(sfi->rootfs.offset==0x3100000 && strstr(envvalue,"ubi.mtd=rootfs ")!=NULL);
 a[1]="rootfs_1";C(rt3000b_prepare_boot_slot(2,a)==0);
 C(sfi->rootfs.offset==0x900000 && strstr(envvalue,"ubi.mtd=rootfs_1 ")!=NULL);
 C(selector_calls==4);

 /* Invalid or unavailable selectors fail closed before changing the target. */
 selector_value=-EINVAL;fixture(0);sfi->flash_type=11;
 old_offset=sfi->rootfs.offset;old_setenv_calls=setenv_calls;
 C(rt3000b_prepare_boot_slot(1,automatic)==CMD_RET_FAILURE);
 C(sfi->rootfs.offset==old_offset && setenv_calls==old_setenv_calls);
 selector_value=2;fixture(0);sfi->flash_type=11;
 old_offset=sfi->rootfs.offset;old_setenv_calls=setenv_calls;
 C(rt3000b_prepare_boot_slot(1,automatic)==CMD_RET_FAILURE);
 C(sfi->rootfs.offset==old_offset && setenv_calls==old_setenv_calls);
 selector_value=-ENOENT;fixture(0);sfi->flash_type=11;
 old_offset=sfi->rootfs.offset;old_setenv_calls=setenv_calls;
 C(rt3000b_prepare_boot_slot(1,automatic)==CMD_RET_FAILURE);
 C(sfi->rootfs.offset==old_offset && setenv_calls==old_setenv_calls);

 /* Single-slot automatic boot never consults BOOTCONFIG. */
 selector_value=-EINVAL;fixture(1);sfi->flash_type=11;
 old_selector_calls=selector_calls;
 C(rt3000b_prepare_boot_slot(1,automatic)==0);
 C(selector_calls==old_selector_calls && sfi->rootfs.offset==0x900000);
 a[1]="rootfs_1";C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_USAGE);
 a[1]="rootfs";C(rt3000b_prepare_boot_slot(2,a)==0 && sfi->rootfs.size==0x5000000);
 C(selector_calls==old_selector_calls);
 C(rt3000b_prepare_boot_slot(1,a)==0); /* Explicit argument remains accepted. */
 C(selector_calls==old_selector_calls);

 sfi->flash_type=6;C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_FAILURE);
 sfi->flash_type=11;T()->parts[15].size=1;C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_FAILURE);
 fixture(0);setenv_failed=1;C(rt3000b_prepare_boot_slot(2,a)==CMD_RET_FAILURE);
 puts("RT3000-B named boot helper host tests: PASS");return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="rt3-named-boot-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(source)
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-I" + str(root / "test/rt3000_b_layout_stubs"), "-I" + str(root / "include"), "-I" + str(root / "test"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
