/* SPDX-License-Identifier: GPL-2.0 */
#ifdef NX30_NOR_HOST_TEST
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#else
#include <common.h>
#include <asm/errno.h>
#include <malloc.h>
#endif
#include "nor_upgrade_core.h"

#define PT_LOAD 1U
#define PF_X 1U

static u32 get_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u16 get_le16(const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

static int marker_valid(const u8 *data, u32 size)
{
	u32 i, found = 0;

	if (size < NX30_NOR_MARKER_SIZE)
		return 0;
	for (i = 0; i <= size - NX30_NOR_MARKER_SIZE; i++) {
		if (!memcmp(data + i, NX30_NOR_MARKER, NX30_NOR_MARKER_SIZE))
			found++;
	}
	return found == 1;
}

static int elf_valid(const u8 *data, u32 size)
{
	u32 entry, phoff, phnum, phentsize, i;
	int entry_in_exec = 0;
	const u32 ram_start = 0x4a920000U;
	const u32 ram_end = 0x4aa00000U;

	if (size < 52 || memcmp(data, "\177ELF", 4) || data[4] != 1 ||
	    data[5] != 1 || data[6] != 1 || (get_le16(data + 16) != 2 && get_le16(data + 16) != 3) ||
	    get_le16(data + 18) != 40 || get_le32(data + 20) != 1 ||
	    get_le16(data + 40) < 52)
		return -EINVAL;
	entry = get_le32(data + 24) & ~1U;
	phoff = get_le32(data + 28);
	phentsize = get_le16(data + 42);
	phnum = get_le16(data + 44);
	if (phentsize < 32 || !phnum || phoff > size ||
	    phnum > (size - phoff) / phentsize)
		return -EINVAL;

	for (i = 0; i < phnum; i++) {
		const u8 *ph = data + phoff + i * phentsize;
		u32 type = get_le32(ph), offset, vaddr, paddr, filesz, memsz, flags;
		u64 end;

		if (type != PT_LOAD)
			continue;
		offset = get_le32(ph + 4);
		vaddr = get_le32(ph + 8);
		paddr = get_le32(ph + 12);
		filesz = get_le32(ph + 16);
		memsz = get_le32(ph + 20);
		flags = get_le32(ph + 24);
		if (filesz > memsz || offset > size || filesz > size - offset)
			return -EINVAL;
		if (!vaddr && !memsz && !filesz)
			continue;
		if (!vaddr) {
			if ((flags & PF_X) || paddr)
				return -EINVAL;
			continue;
		}
		if (paddr != vaddr)
			return -EINVAL;
		end = (u64)vaddr + memsz;
		if (vaddr < ram_start || end > ram_end || end <= vaddr)
			return -EINVAL;
		if ((flags & PF_X) && entry >= vaddr && entry < end)
			entry_in_exec = 1;
	}
	return entry_in_exec ? 0 : -EINVAL;
}

int nx30_nor_validate_package(const void *package, u32 package_size,
			      nx30_nor_digest_fn digest,
			      struct nx30_nor_image *image)
{
	const u8 *p = package;
	u32 payload_size, nor_size, i;
	u8 calculated[32];

	if (!p || !digest || !image || package_size < NX30_NOR_WRAP_HEADER_SIZE)
		return -EINVAL;
	nor_size = get_le32(p + 36);
	if (memcmp(p, "NX30NOR1", 8) || get_le32(p + 8) != 1 ||
	    get_le32(p + 12) != NX30_NOR_WRAP_HEADER_SIZE ||
	    get_le32(p + 20) != NX30_NOR_LAYOUT_ID ||
	    get_le32(p + 24) != NX30_NOR_PRIMARY_OFFSET ||
	    get_le32(p + 28) != NX30_NOR_BACKUP_OFFSET ||
	    get_le32(p + 32) != NX30_NOR_SLOT_SIZE ||
	    (nor_size != NX30_NOR_FLASH_SIZE_8M &&
	     nor_size != NX30_NOR_FLASH_SIZE_16M) ||
	    NX30_NOR_BACKUP_OFFSET + NX30_NOR_SLOT_SIZE > nor_size)
		return -EINVAL;
	for (i = 72; i < NX30_NOR_WRAP_HEADER_SIZE; i++)
		if (p[i])
			return -EINVAL;
	payload_size = get_le32(p + 16);
	if (!payload_size || payload_size > NX30_NOR_SLOT_SIZE ||
	    package_size != NX30_NOR_WRAP_HEADER_SIZE + payload_size)
		return -EFBIG;
	p += NX30_NOR_WRAP_HEADER_SIZE;
	digest(p, payload_size, calculated);
	if (memcmp(calculated, ((const u8 *)package) + 40, sizeof(calculated)))
		return -EBADMSG;
	if (!marker_valid(p, payload_size) || elf_valid(p, payload_size))
		return -EINVAL;
	image->payload = p;
	image->payload_size = payload_size;
	image->nor_size = nor_size;
	return 0;
}

static int write_slot(const struct nx30_nor_image *image,
		      const struct nx30_nor_flash_ops *ops, void *ctx,
		      u32 offset)
{
	u8 check[4096];
	u32 done = 0, count, i;
	int ret;

	ret = ops->erase(ctx, offset, NX30_NOR_SLOT_SIZE);
	if (ret)
		return ret;
	ret = ops->write(ctx, offset, image->payload, image->payload_size);
	if (ret)
		return ret;
	while (done < NX30_NOR_SLOT_SIZE) {
		count = min_t(u32, sizeof(check), NX30_NOR_SLOT_SIZE - done);
		ret = ops->read(ctx, offset + done, check, count);
		if (ret)
			return ret;
		for (i = 0; i < count; i++) {
			u32 pos = done + i;
			u8 expected = pos < image->payload_size ? image->payload[pos] : 0xff;
			if (check[i] != expected)
				return -EIO;
		}
		done += count;
	}
	return 0;
}

int nx30_nor_write_both(const struct nx30_nor_image *image,
			const struct nx30_nor_flash_ops *ops, void *ctx)
{
	int ret;

	if (!image || !image->payload || !image->payload_size ||
	    image->payload_size > NX30_NOR_SLOT_SIZE ||
	    (image->nor_size != NX30_NOR_FLASH_SIZE_8M &&
	     image->nor_size != NX30_NOR_FLASH_SIZE_16M) ||
	    NX30_NOR_BACKUP_OFFSET + NX30_NOR_SLOT_SIZE > image->nor_size || !ops ||
	    !ops->verify_layout_and_bootconfig || !ops->erase || !ops->write ||
	    !ops->read)
		return -EINVAL;
	ret = ops->verify_layout_and_bootconfig(ctx, image->nor_size);
	if (ret)
		return ret;
	ret = write_slot(image, ops, ctx, NX30_NOR_BACKUP_OFFSET);
	if (ret)
		return ret;
	return write_slot(image, ops, ctx, NX30_NOR_PRIMARY_OFFSET);
}
