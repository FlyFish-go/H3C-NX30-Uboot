/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3_FW_PLATFORM_H__
#define __RT3_FW_PLATFORM_H__
#include <stddef.h>

/*
 * What a UBI system image was built for, read from the FIT in its "kernel"
 * volume. Informational only: the rescue page shows it next to the board's
 * own config names but never refuses a write over it.
 */
struct rt3_fw_platform {
	char configs[160];	/* FIT /configurations names, comma-separated */
	char arch[16];		/* "arch" of the default config's kernel image */
};

/*
 * Fill *p from an already validated UBI image. Names are reduced to
 * [A-Za-z0-9@._+-] so they can be put in JSON as is. Returns 0, -EINVAL
 * (no usable kernel FIT), -ENOENT (no configurations) or -ENOMEM.
 */
int rt3_fw_ubi_platform(const void *ubi, size_t size, struct rt3_fw_platform *p);

/* Nonzero when name is one of the comma-separated entries in list. */
int rt3_fw_platform_has_config(const char *list, const char *name);
#endif
