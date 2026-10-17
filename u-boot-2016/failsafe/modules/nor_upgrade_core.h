/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOR_UPGRADE_CORE_H__
#define __NOR_UPGRADE_CORE_H__

/* NX30NOR1 is a compatibility/checksum wrapper, not a signed image. */

#ifdef NX30_NOR_HOST_TEST
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include <common.h>
#include <h3c_hw_d_nor_rescue.h>
#endif

#define NX30_NOR_WRAP_HEADER_SIZE 128U
#define NX30_NOR_SLOT_SIZE        0x000a0000U
#define NX30_NOR_FLASH_SIZE_8M    0x00800000U
#define NX30_NOR_FLASH_SIZE_16M   0x01000000U
#define NX30_NOR_FLASH_SIZE       NX30_NOR_FLASH_SIZE_16M
#define NX30_NOR_PRIMARY_OFFSET   0x001f0000U
#define NX30_NOR_BACKUP_OFFSET    0x00290000U
#define NX30_NOR_LAYOUT_ID        1U
#ifdef NX30_NOR_HOST_TEST
#define NX30_NOR_MARKER "NX30-HW-D-SPI-NOR-RESCUE-V1"
#define NX30_NOR_MARKER_SIZE (sizeof(NX30_NOR_MARKER) - 1)
#else
#define NX30_NOR_MARKER h3c_hw_d_nor_rescue_marker
#define NX30_NOR_MARKER_SIZE H3C_HW_D_NOR_RESCUE_MARKER_SIZE
#endif

typedef void (*nx30_nor_digest_fn)(const void *data, u32 size, u8 digest[32]);

struct nx30_nor_image {
	const u8 *payload;
	u32 payload_size;
	u32 nor_size;
};

struct nx30_nor_flash_ops {
	int (*verify_layout_and_bootconfig)(void *ctx, u32 requested_nor_size);
	int (*erase)(void *ctx, u32 offset, u32 size);
	int (*write)(void *ctx, u32 offset, const void *data, u32 size);
	int (*read)(void *ctx, u32 offset, void *data, u32 size);
};

int nx30_nor_validate_package(const void *package, u32 package_size,
			      nx30_nor_digest_fn digest,
			      struct nx30_nor_image *image);
int nx30_nor_write_both(const struct nx30_nor_image *image,
			const struct nx30_nor_flash_ops *ops, void *ctx);

#endif
