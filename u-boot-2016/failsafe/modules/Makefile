# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_H3C_HW_D_NOR_RESCUE),y)
# The NOR rescue image serves only the rescue UI, read-only diagnostics,
# network information, and the NOR-specific upgrade endpoints.
obj-y += misc.o network.o sysinfo.o upgrade.o
obj-y += nor_upgrade.o nor_upgrade_core.o
obj-y += nor_nand_autoecc.o nor_nand_autoecc_core.o
else
obj-y += backup.o
obj-y += env.o
ifeq ($(CONFIG_H3C_IPQ5018_COMMON),y)
obj-y += firmware_rt3000_b.o firmware_rt3000_b_core.o
obj-y += metadata_rt3000_b.o rt3000_b_metadata_core.o
endif
obj-y += helpers.o
obj-y += mibib.o
obj-y += misc.o
obj-y += network.o
obj-y += sysinfo.o
obj-y += syslog.o
ifneq ($(filter y,$(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) $(CONFIG_H3C_HW_D_PRODUCTION_PORT)),)
obj-y += buttons_rt3000_b.o
endif
obj-$(CONFIG_TARGET_IPQ5018_H3C_HW_A) += buttons_h3c_hw_a.o
obj-$(CONFIG_TARGET_IPQ5018_H3C_HW_C) += buttons_h3c_hw_c.o
obj-$(CONFIG_H3C_IPQ5018_COMMON) += slots_rt3000_b.o
ifneq ($(filter y,$(CONFIG_H3C_HW_D_RAM_AUTOECC) $(CONFIG_H3C_NAND_ECC_COMPAT)),)
obj-y += nor_nand_autoecc.o nor_nand_autoecc_core.o
endif
ifneq ($(filter y,$(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) $(CONFIG_TARGET_IPQ5018_H3C_HW_C) $(CONFIG_H3C_HW_D_PRODUCTION_PORT)),)
obj-y += boot_default_rt3000_b.o boot_default_rt3000_b_core.o
endif
obj-y += upgrade.o
obj-$(CONFIG_H3C_HW_D_NOR_RESCUE) += nor_upgrade.o nor_upgrade_core.o
obj-y += webterm.o
ifneq ($(filter y,$(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) $(CONFIG_H3C_HW_D_PRODUCTION_PORT)),)
obj-$(CONFIG_FAILSAFE_MAC_MANAGEMENT) += mac_rt3000_b.o
else ifeq ($(CONFIG_TARGET_IPQ5018_H3C_HW_A),y)
obj-$(CONFIG_FAILSAFE_MAC_MANAGEMENT) += mac_h3c_hw_a.o
else ifeq ($(CONFIG_TARGET_IPQ5018_H3C_HW_C),y)
obj-$(CONFIG_FAILSAFE_MAC_MANAGEMENT) += mac_h3c_hw_c.o
else
obj-$(CONFIG_FAILSAFE_MAC_MANAGEMENT) += mac.o
endif

obj-$(CONFIG_H3C_IPQ5018_COMMON) += squashfs_release.o xz/

ccflags-$(CONFIG_H3C_IPQ5018_COMMON) += -DXZ_UBOOT_BUILD -DXZ_INTERNAL_CRC32=1
endif

ifneq ($(CONFIG_H3C_HW_D_PRODUCTION_PORT),y)
obj-$(CONFIG_H3C_HW_D_NAND_SERVICE) += service_hwd.o
endif
