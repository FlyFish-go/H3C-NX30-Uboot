#ifndef __NX30_D_FDT_NOR_GUARD_H__
#define __NX30_D_FDT_NOR_GUARD_H__

/* Narrow pure policy used only by the NX30 NAND service FDT handoff. */
#include <stddef.h>
#ifdef __UBOOT__
#include <linux/string.h>
#include <asm/errno.h>
#else
#include <string.h>
#include <errno.h>
#endif

static int nx30_d_fdt_stringlist_has(const void *data, int length,
                                    const char *needle)
{
	const char *bytes = data;
	int offset = 0;
	size_t want;
	int found = 0;

	if (!bytes || length <= 0 || !needle || !needle[0])
		return -EINVAL;
	want = strlen(needle);
	while (offset < length) {
		const char *end = memchr(bytes + offset, '\0', length - offset);
		size_t current;

		if (!end)
			return -EINVAL;
		current = end - (bytes + offset);
		if (!current)
			return -EINVAL;
		if (current == want && !memcmp(bytes + offset, needle, want))
			found = 1;
		offset += current + 1;
	}
	return found;
}

static int nx30_d_fdt_nor_guard_validate(int handoff_verified, int nand_boot,
	const void *root_compat, int root_len, int exact_path_count,
	int known_flash_node_count, const void *parent_compat, int parent_len,
	const void *chip_compat, int chip_len, const void *reg, int reg_len,
	int *disable)
{
	const unsigned char *reg_bytes = reg;
	int root_ok, parent_ok, child_n25, child_micron;

	if (!disable)
		return -EINVAL;
	*disable = 0;
	if (!handoff_verified)
		return 0;
	if (!nand_boot)
		return -EINVAL;
	root_ok = nx30_d_fdt_stringlist_has(root_compat, root_len,
					    "qcom,ipq5018-mp02.1");
	parent_ok = nx30_d_fdt_stringlist_has(parent_compat, parent_len,
					      "qcom,spi-qup-v2.2.1");
	child_n25 = nx30_d_fdt_stringlist_has(chip_compat, chip_len,
					      "n25q128a11");
	child_micron = nx30_d_fdt_stringlist_has(chip_compat, chip_len,
					        "micron,n25q128a11");
	if (root_ok != 1 || parent_ok != 1 ||
	    (child_n25 == child_micron) ||
	    exact_path_count != 1 || known_flash_node_count != 1 ||
	    !reg_bytes || reg_len != 4 || reg_bytes[0] || reg_bytes[1] ||
	    reg_bytes[2] || reg_bytes[3])
		return -EINVAL;
	*disable = 1;
	return 0;
}

#endif /* __NX30_D_FDT_NOR_GUARD_H__ */
