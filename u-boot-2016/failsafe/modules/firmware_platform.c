// SPDX-License-Identifier: GPL-2.0
/* Read the target platform of a UBI system image from its kernel FIT. */
#ifdef RT3_FW_HOST_TEST
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>
#else
#include <common.h>
#include <malloc.h>
#include <asm/errno.h>
#include <libfdt.h>
#endif
#include <rt3_fw_platform.h>

#define PEB_SIZE	0x20000U
#define VID_OFFSET	2048U
#define DATA_OFFSET	4096U
#define LEB_SIZE	(PEB_SIZE - DATA_OFFSET)
#define LAYOUT_ID	(0x7fffffffU - 4096U)
#define VID_MAGIC	0x55424921U
#define VTBL_RECS	128U
#define VTBL_REC_SIZE	172U
#define FIT_MAX		(16U << 20)

static unsigned int be32(const unsigned char *p)
{
	return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
	       ((unsigned int)p[2] << 8) | p[3];
}

/* Append name to a comma-separated list, keeping only safe characters. */
static void append_name(char *list, size_t cap, const char *name)
{
	size_t len = strlen(list);

	if (len && len + 1 < cap)
		list[len++] = ',';
	for (; *name && len + 1 < cap; name++) {
		char c = *name;
		int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			 (c >= '0' && c <= '9') || strchr("@._+-", c);
		list[len++] = ok ? c : '?';
	}
	list[len] = '\0';
}

int rt3_fw_platform_has_config(const char *list, const char *name)
{
	size_t n = strlen(name);
	const char *p = list;

	if (!n)
		return 0;
	while ((p = strstr(p, name))) {
		if ((p == list || p[-1] == ',') && (p[n] == ',' || !p[n]))
			return 1;
		p += n;
	}
	return 0;
}

static int kernel_volume_id(const unsigned char *ubi, size_t pebs, unsigned int *id)
{
	size_t i;
	unsigned int r;

	for (i = 0; i < pebs; i++) {
		const unsigned char *peb = ubi + i * PEB_SIZE;
		const unsigned char *vid = peb + VID_OFFSET;
		if (be32(vid) != VID_MAGIC || be32(vid + 8) != LAYOUT_ID)
			continue;
		for (r = 0; r < VTBL_RECS; r++) {
			const unsigned char *rec = peb + DATA_OFFSET + r * VTBL_REC_SIZE;
			unsigned int name_len = ((unsigned int)rec[14] << 8) | rec[15];
			if (be32(rec) && name_len == 6 && !memcmp(rec + 16, "kernel", 6)) {
				*id = r;
				return 0;
			}
		}
		return -EINVAL;
	}
	return -EINVAL;
}

/* Copy the kernel volume's LEBs, in order, into one buffer holding its FIT. */
static int kernel_fit(const unsigned char *ubi, size_t pebs, unsigned int id,
		      unsigned char **out)
{
	unsigned char *fit = NULL;
	unsigned int total = 0, lebs = 0, copied = 0;
	size_t i;

	for (i = 0; i < pebs; i++) {
		const unsigned char *peb = ubi + i * PEB_SIZE;
		if (be32(peb + VID_OFFSET) == VID_MAGIC && be32(peb + VID_OFFSET + 8) == id &&
		    !be32(peb + VID_OFFSET + 12)) {
			total = be32(peb + DATA_OFFSET + 4);
			break;
		}
	}
	if (i == pebs || be32(ubi + i * PEB_SIZE + DATA_OFFSET) != 0xd00dfeedU ||
	    total < sizeof(struct fdt_header) || total > FIT_MAX)
		return -EINVAL;
	lebs = (total + LEB_SIZE - 1) / LEB_SIZE;
	fit = malloc(total);
	if (!fit)
		return -ENOMEM;
	for (i = 0; i < pebs; i++) {
		const unsigned char *peb = ubi + i * PEB_SIZE;
		unsigned int lnum, len;
		if (be32(peb + VID_OFFSET) != VID_MAGIC || be32(peb + VID_OFFSET + 8) != id)
			continue;
		lnum = be32(peb + VID_OFFSET + 12);
		if (lnum >= lebs)
			continue;
		len = total - lnum * LEB_SIZE;
		if (len > LEB_SIZE)
			len = LEB_SIZE;
		memcpy(fit + (size_t)lnum * LEB_SIZE, peb + DATA_OFFSET, len);
		copied++;
	}
	if (copied != lebs || fdt_check_header(fit) || fdt_totalsize(fit) != total) {
		free(fit);
		return -EINVAL;
	}
	*out = fit;
	return 0;
}

/* A string property, only when it is NUL-terminated inside its value. */
static const char *str_prop(const void *fit, int node, const char *name)
{
	int len;
	const char *v = fdt_getprop(fit, node, name, &len);

	return v && len > 0 && !v[len - 1] ? v : NULL;
}

int rt3_fw_ubi_platform(const void *ubi, size_t size, struct rt3_fw_platform *p)
{
	unsigned char *fit = NULL;
	unsigned int id;
	const char *def, *kernel, *arch;
	int confs, node, images, ret;

	if (!ubi || !p || size < PEB_SIZE || size % PEB_SIZE)
		return -EINVAL;
	memset(p, 0, sizeof(*p));
	ret = kernel_volume_id(ubi, size / PEB_SIZE, &id);
	if (!ret)
		ret = kernel_fit(ubi, size / PEB_SIZE, id, &fit);
	if (ret)
		return ret;
	ret = -ENOENT;
	confs = fdt_path_offset(fit, "/configurations");
	if (confs < 0)
		goto out;
	for (node = fdt_first_subnode(fit, confs); node >= 0;
	     node = fdt_next_subnode(fit, node))
		append_name(p->configs, sizeof(p->configs), fdt_get_name(fit, node, NULL));
	if (!p->configs[0])
		goto out;
	ret = 0;
	/* Arch of the kernel the default (else first) config would boot. */
	def = str_prop(fit, confs, "default");
	node = def ? fdt_subnode_offset(fit, confs, def) : fdt_first_subnode(fit, confs);
	kernel = node >= 0 ? str_prop(fit, node, "kernel") : NULL;
	images = fdt_path_offset(fit, "/images");
	node = kernel && images >= 0 ? fdt_subnode_offset(fit, images, kernel) : -1;
	arch = node >= 0 ? str_prop(fit, node, "arch") : NULL;
	if (arch)
		append_name(p->arch, sizeof(p->arch), arch);
out:
	free(fit);
	return ret;
}
