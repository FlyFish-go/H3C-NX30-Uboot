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
	char configs[160];	/* FIT /configurations names that fit, comma-separated */
	unsigned int count;	/* all configurations in the FIT */
	char arch[16];		/* "arch" of the default config's kernel image */
	char match[32];		/* first of the board's names the FIT has, or "" */
};

/*
 * Fill *p from an already validated UBI image. board lists the config names
 * bootipq tries on this board, in order. Names are reduced to
 * [A-Za-z0-9@._+-] so they can be put in JSON as is. Returns 0, -EINVAL
 * (no usable kernel FIT), -ENOENT (no configurations) or -ENOMEM.
 */
int rt3_fw_ubi_platform(const void *ubi, size_t size, const char *const *board,
			unsigned int board_count, struct rt3_fw_platform *p);
#endif
