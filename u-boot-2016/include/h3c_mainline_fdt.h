/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef H3C_MAINLINE_FDT_H
#define H3C_MAINLINE_FDT_H

#include <linux/types.h>

/* 1 when the kernel FDT names its NAND partitions from the SMEM table
 * (upstream OpenWrt "qcom,smem-part"), 0 when not, <0 on a bad FDT. */
int h3c_fdt_uses_smem_parts(const void *fdt);

/* Copy cmdline to out with every ubi.mtd= token removed and one
 * "ubi.mtd=<name>" appended. 0 or -ENOSPC/-EINVAL. */
int h3c_ubi_mtd_rewrite(const char *cmdline, const char *name,
			char *out, size_t cap);

/* Set nand-ecc-strength/nand-ecc-step-size on every "spi-nand" child of
 * a QPIC SPI-NAND controller (upstream binding). Returns the number of
 * nodes changed or <0. Bootloader-owned: the per-unit ECC is only known
 * here. */
int h3c_fdt_set_snand_ecc(void *fdt, unsigned int strength);

#endif
