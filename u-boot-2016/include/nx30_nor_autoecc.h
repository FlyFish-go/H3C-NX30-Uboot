/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NX30_NOR_AUTOECC_H__
#define __NX30_NOR_AUTOECC_H__

/* Read-only sampled NAND ECC detection CLI; returns 0 only on unique ECC4/8. */
int nx30_nor_nand_autoecc_cli(void);
/* Cached sampled system format; zero means unknown, mixed or failed. */
unsigned int nx30_nor_nand_autoecc_mode(void);

#endif
