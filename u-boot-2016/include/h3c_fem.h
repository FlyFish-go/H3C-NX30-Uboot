/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __H3C_FEM_H__
#define __H3C_FEM_H__
#include <linux/types.h>

/* MP02.1 (HW-B / HW-D) 5 GHz FEM check from the factory calibration block,
 * ART + 0x26800: header 01 00 04 04, +0x3a/+0x45 = 60/09 with FEMs (HW-D),
 * 50/01 without (HW-B; also a D-PCB without FEMs such as RC3000 B0).
 * Same rule as BootBridge2 and the QSDK HW-D image. */
#define H3C_FEM_ART_OFFSET	0x26800
#define H3C_FEM_BYTES		0x46

enum h3c_fem_state {
	H3C_FEM_NA = 0,		/* not an MP02.1 build (HW-A / HW-C) */
	H3C_FEM_PRESENT,
	H3C_FEM_ABSENT,
	H3C_FEM_UNKNOWN,	/* unreadable ART or unrecognised flags */
};

enum h3c_fem_state h3c_fem_classify(const u8 *blk, size_t len);
/* Board group the flags call for: "D", "B", or NULL when not decided. */
const char *h3c_fem_group(enum h3c_fem_state state);
const char *h3c_fem_name(enum h3c_fem_state state);
/* This unit, read once from 0:ART and cached. */
enum h3c_fem_state h3c_fem_probe(void);
#endif
