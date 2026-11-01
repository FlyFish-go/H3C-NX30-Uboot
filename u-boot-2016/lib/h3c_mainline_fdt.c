// SPDX-License-Identifier: GPL-2.0+
/*
 * Handoff helpers for upstream (mainline OpenWrt) kernels.
 *
 * Upstream qualcommax DTs name the NAND partitions from the SMEM table
 * ("qcom,smem-part") and ignore mtdparts=, so ubi.mtd= must carry the
 * SMEM name of the booted extent rather than the QSDK alias. They also
 * need the NAND ECC strength in the DT, which only the bootloader knows
 * per unit.
 */
#include <common.h>
#include <errno.h>
#include <libfdt.h>
#include <h3c_mainline_fdt.h>

int h3c_fdt_uses_smem_parts(const void *fdt)
{
	int node;

	if (!fdt || fdt_check_header(fdt))
		return -EINVAL;
	node = fdt_node_offset_by_compatible(fdt, -1, "qcom,smem-part");
	if (node >= 0)
		return 1;
	return node == -FDT_ERR_NOTFOUND ? 0 : -EINVAL;
}

int h3c_ubi_mtd_rewrite(const char *cmdline, const char *name,
			char *out, size_t cap)
{
	const char *p = cmdline ? cmdline : "";
	size_t used = 0, n;

	if (!name || !*name || strchr(name, ' ') || !out || !cap)
		return -EINVAL;
	while (*p) {
		const char *start, *end;

		while (*p == ' ')
			p++;
		if (!*p)
			break;
		start = p;
		while (*p && *p != ' ')
			p++;
		end = p;
		n = end - start;
		if (n >= 8 && !memcmp(start, "ubi.mtd=", 8))
			continue;
		if (used + (used ? 1 : 0) + n >= cap)
			return -ENOSPC;
		if (used)
			out[used++] = ' ';
		memcpy(out + used, start, n);
		used += n;
	}
	n = strlen(name);
	if (used + (used ? 1 : 0) + 8 + n >= cap)
		return -ENOSPC;
	if (used)
		out[used++] = ' ';
	memcpy(out + used, "ubi.mtd=", 8);
	used += 8;
	memcpy(out + used, name, n);
	used += n;
	out[used] = '\0';
	return 0;
}

int h3c_fdt_set_snand_ecc(void *fdt, unsigned int strength)
{
	int node, parent, changed = 0, ret;

	if (!fdt || fdt_check_header(fdt) || (strength != 4 && strength != 8))
		return -EINVAL;
	for (node = fdt_node_offset_by_compatible(fdt, -1, "spi-nand");
	     node >= 0;
	     node = fdt_node_offset_by_compatible(fdt, node, "spi-nand")) {
		parent = fdt_parent_offset(fdt, node);
		if (parent < 0 ||
		    (fdt_node_check_compatible(fdt, parent, "qcom,ipq9574-snand") &&
		     fdt_node_check_compatible(fdt, parent, "qcom,ipq5018-snand")))
			continue;
		ret = fdt_setprop_u32(fdt, node, "nand-ecc-strength", strength);
		if (!ret)
			ret = fdt_setprop_u32(fdt, node, "nand-ecc-step-size", 512);
		if (ret)
			return -EINVAL;
		changed++;
	}
	if (node != -FDT_ERR_NOTFOUND)
		return -EINVAL;
	return changed;
}
