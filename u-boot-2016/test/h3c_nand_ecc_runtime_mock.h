/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __H3C_NAND_ECC_RUNTIME_MOCK_H__
#define __H3C_NAND_ECC_RUNTIME_MOCK_H__
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
struct mtd_info {
 unsigned int tag, type, erasesize, writesize, ecc_strength, flags;
 uint64_t size;
};
#endif
