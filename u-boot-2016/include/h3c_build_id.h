/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __H3C_BUILD_ID_H__
#define __H3C_BUILD_ID_H__
#include <linux/types.h>

/* This build's identity, e.g.
 * "H3C-UBOOT-ID:v1;board=A;profile=h3c-oem-efba-128m-2k-sp64;mask=0x1;ecc=4;". */
const char *h3c_build_id(void);

/* 0 when image carries exactly one identity string and it equals this build's;
 * -ENOENT none, -EEXIST more than one, -EXDEV a different board/NAND build. */
int h3c_build_id_check(const u8 *image, size_t size);
#endif
