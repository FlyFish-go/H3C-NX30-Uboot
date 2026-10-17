/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_B_ENV_H__
#define __RT3000_B_ENV_H__
#include <linux/types.h>
/* Compare a full-partition backup's reserved tail with current APPSBLENV. */
int rt3_nand_env_tail_matches(const void *tail, size_t tail_size);
#endif
