#ifndef __RTL8367S_UBOOT_H__
#define __RTL8367S_UBOOT_H__
int rtl8367s_switch_init(void);
#ifdef CONFIG_TARGET_IPQ5018_H3C_HW_C
int rtl8367s_sgmii_link_down(void);
int rtl8367s_sgmii_link_up(void);
#endif
#endif
