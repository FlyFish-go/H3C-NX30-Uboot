from pathlib import Path
r=Path(__file__).resolve().parents[1];u=r
def t(p): return (r/p).read_text()
a=t("board/ipq5018/Kconfig")
assert "config TARGET_IPQ5018_H3C_HW_C" in a and "config H3C_HW_C_RAM_READONLY" in a and "config H3C_HW_C_PRODUCTION_PORT" in a
for n,profile,save in [("ipq5018_h3c_hw_c_defconfig","CONFIG_H3C_HW_C_RAM_READONLY=y","# CONFIG_CMD_SAVEENV is not set"),("ipq5018_h3c_hw_c_production_defconfig","CONFIG_H3C_HW_C_PRODUCTION_PORT=y","CONFIG_CMD_SAVEENV=y")]:
 s=(u/"configs"/n).read_text()
 assert "CONFIG_TARGET_IPQ5018_H3C_HW_C=y" in s and profile in s and save in s
 assert 'CONFIG_DEFAULT_DEVICE_TREE="ipq5018-h3c-hw-c"' in s and "HW-A" not in s
m=t("failsafe/modules/rt3000_b_metadata_core.c")
for x in ["machid[4]={1,1,4,8}",'has(load,size,"h3c,hw-c",8)','has(load,size,"H3C HW-C",8)', "b_identity[5]={2,8,4,1,1}"]: assert x in m

fw=t("failsafe/modules/firmware_rt3000_b.c")
assert '#include "firmware_oem_rt3000_b.h"' in fw
q=t("drivers/mtd/nand/qpic_nand.c")
assert q.count("h3c_hw_c_nand_write_rejected()")>=5 and "CONFIG_H3C_HW_C_PRODUCTION_PORT" in q
assert "W25N01GWZEIG" in q
b=t("failsafe/modules/buttons_h3c_hw_c.c")
assert "H3C_HW_C_RESET_GPIO 27" in b and "H3C_HW_C_WPS_GPIO 38" in b
assert 'active_low' in b and "GPIO_PULL_UP" in b and "led_" not in b
h=t("net/httpd.c")
assert "h3c_hw_c_request_rejected" in h and "HW-C RAM-only preview" in h
c=t("board/qca/arm/common/failsafe.c")
i=c.index("case WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS:")
assert "#if defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY)" in c[i-120:i]
boot=t("lib/bootconfig.c"); assert "H3C_BOOTCFG_POLICY_LABEL \"H3C HW-C\"" in boot
cmd=t("board/qca/arm/common/cmd_bootqca.c"); assert "RT3_BOOT_BOARD_LABEL \"H3C HW-C\"" in cmd
assert "CONFIG_H3C_HW_C_PRODUCTION_PORT" in t("common/autoboot.c") and "setenv(\"bootdelay\"" not in t("common/autoboot.c")
print("HW-C profile, identity, NAND, HTTP gate, buttons, OEM catalog, runtime labels, AUTO ENV preservation, and FIT isolation: PASS")
