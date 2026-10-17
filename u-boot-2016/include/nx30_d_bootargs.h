/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_BOOTARGS_H__
#define __NX30_D_BOOTARGS_H__
#ifdef NX30_D_BOOTARGS_HOST_TEST
#include <stddef.h>
#else
#include <linux/types.h>
#endif
/* Reject conflicting overrides; emit exactly one physical-spare/ECC pair. */
int nx30_d_ecc_bootargs(const char *input, char *output, size_t capacity,
                      unsigned int physical_spare, unsigned int ecc_bits);
#endif
