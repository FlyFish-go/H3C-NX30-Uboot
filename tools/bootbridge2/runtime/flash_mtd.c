// SPDX-License-Identifier: GPL-2.0-or-later
/* Linux MTD backend: partitions are found by name or physical offset via
 * /sys/class/mtd, and accessed through /dev/mtdN. */
#include "flash_mtd.h"

#include <errno.h>
#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct mtd_ctx {
	int fd;
};

static int sys_read(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	size_t n;

	if (!f)
		return -1;
	n = fread(buf, 1, len - 1, f);
	fclose(f);
	buf[n] = 0;
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

static int sys_u32(int idx, const char *attr, uint32_t *out)
{
	char path[96], buf[32], *end;
	unsigned long v;

	snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/%s", idx, attr);
	if (sys_read(path, buf, sizeof(buf)))
		return -1;
	errno = 0;
	v = strtoul(buf, &end, 0);
	if (errno || end == buf || *end || v > UINT32_MAX)
		return -1;
	*out = v;
	return 0;
}

static int mtd_read(void *ctx, uint32_t off, void *buf, size_t len)
{
	struct mtd_ctx *c = ctx;
	size_t done = 0;

	while (done < len) {
		/* One page per read. The OEM 4.4 NAND driver returns 0xff for
		 * the rest of an eraseblock when a multi-page read starts on the
		 * page it has cached (seen on HW-A, 2026-10-25). */
		size_t want = len - done > 2048 ? 2048 : len - done;
		ssize_t n = pread(c->fd, (uint8_t *)buf + done, want, off + done);

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return -errno;	/* EBADMSG: uncorrectable ECC */
		if (n == 0)
			return -EIO;
		done += n;
	}
	return 0;
}

static int mtd_is_bad(void *ctx, uint32_t block_off)
{
	struct mtd_ctx *c = ctx;
	loff_t o = block_off;
	int r = ioctl(c->fd, MEMGETBADBLOCK, &o);

	return r < 0 ? -errno : r > 0;
}

static int mtd_erase(void *ctx, uint32_t block_off)
{
	struct mtd_ctx *c = ctx;
	struct erase_info_user e = { .start = block_off };
	struct mtd_info_user info;

	if (ioctl(c->fd, MEMGETINFO, &info))
		return -errno;
	e.length = info.erasesize;
	return ioctl(c->fd, MEMERASE, &e) ? -errno : 0;
}

static int mtd_write_page(void *ctx, uint32_t off, const void *buf)
{
	struct mtd_ctx *c = ctx;
	struct mtd_info_user info;
	ssize_t n;

	if (ioctl(c->fd, MEMGETINFO, &info))
		return -errno;
	n = pwrite(c->fd, buf, info.writesize, off);
	if (n < 0)
		return -errno;
	return (size_t)n == info.writesize ? 0 : -EIO;
}

static void mtd_close(void *ctx)
{
	struct mtd_ctx *c = ctx;

	close(c->fd);
	free(c);
}

int bb_mtd_find(const char *name, uint32_t offset)
{
	char path[96], buf[64];

	for (int i = 0; i < 64; i++) {
		uint32_t off;

		/* Skip gluebi and other non-NAND devices. */
		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/type", i);
		if (sys_read(path, buf, sizeof(buf)) || strcmp(buf, "nand"))
			continue;
		snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/name", i);
		if (sys_read(path, buf, sizeof(buf)))
			continue;
		if (name && !strcmp(buf, name))
			return i;
		if (!name && !sys_u32(i, "offset", &off) && off == offset)
			return i;
	}
	return -1;
}

int bb_mtd_open(int idx, int writable, struct bb_flash *f)
{
	struct mtd_info_user info;
	struct mtd_ctx *c;
	char path[32], name[64];

	memset(f, 0, sizeof(*f));
	snprintf(path, sizeof(path), "/sys/class/mtd/mtd%d/name", idx);
	if (sys_read(path, name, sizeof(name)) || sys_u32(idx, "offset", &f->offset))
		return -ENOENT;
	snprintf(f->name, sizeof(f->name), "%.*s", (int)sizeof(f->name) - 1, name);
	c = malloc(sizeof(*c));
	if (!c)
		return -ENOMEM;
	snprintf(path, sizeof(path), "/dev/mtd%d", idx);
	c->fd = open(path, (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC);
	if (c->fd < 0 || ioctl(c->fd, MEMGETINFO, &info) || info.type != MTD_NANDFLASH) {
		int err = c->fd < 0 ? errno : EINVAL;

		if (c->fd >= 0)
			close(c->fd);
		free(c);
		return -err;
	}
	f->ctx = c;
	f->size = info.size;
	f->erase_size = info.erasesize;
	f->page_size = info.writesize;
	f->read = mtd_read;
	f->is_bad = mtd_is_bad;
	f->erase = mtd_erase;
	f->write_page = mtd_write_page;
	f->close = mtd_close;
	return 0;
}
