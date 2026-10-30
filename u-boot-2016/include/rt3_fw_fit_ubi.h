/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3_FW_FIT_UBI_H__
#define __RT3_FW_FIT_UBI_H__
#include <stddef.h>

/* Bytes a vendor may append after the FIT (e.g. a 70-byte signature footer). */
#define RT3_FW_FIT_TRAILER_MAX 4096U

/*
 * Find the UBI image in a QSDK FIT upgrade package: a FIT whose /images hold
 * a flash script plus exactly one image whose data starts with "UBI#". Only
 * locates it; the caller validates the UBI as for a bare image and never runs
 * the package's script. Returns 0, -EINVAL (not a sane FIT), -ENOENT (no UBI)
 * or -EEXIST (more than one UBI).
 */
int rt3_fw_fit_ubi(const void *file, size_t size, const void **ubi,
		   size_t *ubi_size);
#endif
