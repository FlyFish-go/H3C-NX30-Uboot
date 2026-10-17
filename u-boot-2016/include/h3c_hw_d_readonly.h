/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __H3C_HW_D_READONLY_H
#define __H3C_HW_D_READONLY_H
#ifdef CONFIG_H3C_HW_D_RAM_READONLY
#include <errno.h>
static inline int h3c_hw_d_nand_write_rejected(void) { return -EROFS; }
#endif
#endif
