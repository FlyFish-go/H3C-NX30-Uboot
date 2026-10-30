// SPDX-License-Identifier: GPL-2.0
/* Unwrap the UBI image from a QSDK FIT upgrade package. */
#ifdef RT3_FW_HOST_TEST
#include <errno.h>
#include <string.h>
#include <libfdt.h>
#else
#include <common.h>
#include <asm/errno.h>
#include <libfdt.h>
#endif
#include <rt3_fw_fit_ubi.h>

static int fit_block_ok(unsigned int off, unsigned int len, unsigned int total)
{
	return off <= total && len <= total - off;
}

int rt3_fw_fit_ubi(const void *file, size_t size, const void **ubi,
		   size_t *ubi_size)
{
	const void *data, *found = NULL;
	unsigned int total;
	int images, node, len, found_len = 0;

	if (!file || !ubi || !ubi_size || size < sizeof(struct fdt_header) ||
	    fdt_check_header(file))
		return -EINVAL;
	/* libfdt here trusts the header's block offsets; bound them first. */
	total = fdt_totalsize(file);
	if (total < sizeof(struct fdt_header) || total > size ||
	    size - total > RT3_FW_FIT_TRAILER_MAX ||
	    !fit_block_ok(fdt_off_dt_struct(file), fdt_size_dt_struct(file), total) ||
	    !fit_block_ok(fdt_off_dt_strings(file), fdt_size_dt_strings(file), total))
		return -EINVAL;
	images = fdt_path_offset(file, "/images");
	if (images < 0)
		return -ENOENT;
	for (node = fdt_first_subnode(file, images); node >= 0;
	     node = fdt_next_subnode(file, node)) {
		data = fdt_getprop(file, node, "data", &len);
		if (!data || len < 4 || memcmp(data, "UBI#", 4))
			continue;
		if (found)
			return -EEXIST;
		found = data;
		found_len = len;
	}
	if (!found)
		return -ENOENT;
	*ubi = found;
	*ubi_size = found_len;
	return 0;
}
