// SPDX-License-Identifier: GPL-2.0-or-later
#include "flash.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int bb_flash_read(const struct bb_flash *f, uint32_t off, void *buf, size_t len)
{
	if (off > f->size || len > f->size - off)
		return -EINVAL;
	return f->read(f->ctx, off, buf, len);
}

static int all_ff(const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != 0xff)
			return 0;
	return 1;
}

int bb_flash_rewrite_block(const struct bb_flash *f, uint32_t block_off,
			   const uint8_t *data)
{
	uint8_t *back;
	int ret;

	if (block_off % f->erase_size || block_off >= f->size)
		return -EINVAL;
	ret = f->is_bad(f->ctx, block_off);
	if (ret)
		return ret < 0 ? ret : -EIO;
	ret = f->erase(f->ctx, block_off);
	if (ret)
		return ret;
	/* Programming an all-0xff page stores ECC for "empty" data; a later
	 * write to that page would then fail ECC. Leave such pages erased. */
	for (uint32_t p = 0; p < f->erase_size; p += f->page_size) {
		if (all_ff(data + p, f->page_size))
			continue;
		ret = f->write_page(f->ctx, block_off + p, data + p);
		if (ret)
			return ret;
	}
	back = malloc(f->erase_size);
	if (!back)
		return -ENOMEM;
	ret = f->read(f->ctx, block_off, back, f->erase_size);
	if (!ret && memcmp(back, data, f->erase_size))
		ret = -EIO;
	free(back);
	return ret;
}
