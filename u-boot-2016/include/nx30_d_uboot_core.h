/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_UBOOT_CORE_H__
#define __NX30_D_UBOOT_CORE_H__

#ifdef NX30_D_UBOOT_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include <linux/types.h>
#endif

#define NX30_D_UBOOT_LOW_OFFSET 0x00500000ULL
#define NX30_D_UBOOT_HIGH_OFFSET 0x00640000ULL
#define NX30_D_UBOOT_SLOT_OFFSET NX30_D_UBOOT_HIGH_OFFSET
#define NX30_D_UBOOT_SLOT_BYTES  0x00140000U
#define NX30_D_UBOOT_ECC_BITS    4U
#define NX30_D_UBOOT_ENTRY       0x4a920000U
#define NX30_D_UBOOT_BSS_LIMIT   0x4aa00000U
#if defined(CONFIG_H3C_HW_D_STORAGE_PORT) && \
	!defined(CONFIG_H3C_HW_D_SERVICE_RAM) && \
	!defined(CONFIG_H3C_HW_D_NOR_RESCUE)
#define NX30_D_UBOOT_SERVICE_RAM 0U
#else
#define NX30_D_UBOOT_SERVICE_RAM 1U
#endif
#define NX30_D_UBOOT_REQUIRED_SERVICE_RAM 0U

#define NX30_D_UBOOT_IMAGE_MAGIC "NX30-D-UBOOT-SERVICE-IMAGE-V1"
/* Neutral project label; contains no individual sample identity. */
#define NX30_D_UBOOT_LABEL       "BOOTBRIDGE-HW-D"
#define NX30_D_UBOOT_COMPATIBLE  "h3c,nx30-d"
#define NX30_D_UBOOT_MODEL       "H3C HW-D"
#define NX30_D_UBOOT_MACHID      0x08040000U
#define NX30_D_UBOOT_IDENTITY_FLAGS 0x00000001U

/* This descriptor is retained in PT_LOAD and binds an MBN to the D NAND
 * service profile and its final linker BSS boundary. */
struct nx30_d_uboot_identity {
	char magic[32];
	char sample[32];
	char compatible[16];
	char model[16];
	u32 machid;
	u32 ecc_bits;
	u32 service_ram;
	u32 flags;
	u32 bss_end;
} __attribute__((packed));

struct nx30_d_uboot_core_info {
	u32 image_crc32;
	u32 image_bytes;
	u32 load_bytes;
	u32 load_end;
	u32 bss_end;
};

typedef void (*nx30_d_uboot_digest_fn)(const u8 *data, size_t length,
					       u8 digest[32], void *context);

struct nx30_d_uboot_io {
	void *context;
	int (*open)(void *context, u64 offset, u64 size, unsigned int ecc_bits);
	void (*close)(void *context);
	int (*close_error)(void *context);
	int (*erase)(void *context, u64 offset, u64 size);
	int (*write)(void *context, u64 offset, const void *data, size_t size,
		     size_t *written);
	int (*read)(void *context, u64 offset, void *data, size_t size,
		    size_t *read);
};

struct nx30_d_uboot_geometry {
	u32 generation;
	u32 nand_size;
	u32 erase_size;
	u32 write_size;
	u32 ecc_bits;
	u64 appsbl_offset;
	u64 appsbl_size;
};

enum nx30_d_uboot_stage {
	NX30_D_UBOOT_STAGE_NONE = 0,
	NX30_D_UBOOT_STAGE_VALIDATE = 1,
	NX30_D_UBOOT_STAGE_WINDOW = 2,
	NX30_D_UBOOT_STAGE_ERASE = 3,
	NX30_D_UBOOT_STAGE_WRITE = 4,
	NX30_D_UBOOT_STAGE_READBACK = 5,
	NX30_D_UBOOT_STAGE_COMPLETE = 6,
	NX30_D_UBOOT_STAGE_PROTECT = 7,
};

int nx30_d_uboot_runtime_role_matches(u32 descriptor_role);
int nx30_d_uboot_active_copy_resolve(const void *bc0, const void *bc1,
				       size_t length, u64 smem_offset,
				       u64 smem_size, u64 *active_offset);
int nx30_d_uboot_fixed_string_equal(const volatile char *actual,
					 const char *expected, size_t length);
int nx30_d_uboot_parse_mbn(const void *image, size_t length,
			   nx30_d_uboot_digest_fn digest, void *digest_context,
			   struct nx30_d_uboot_core_info *info);
u32 nx30_d_uboot_crc32(const void *data, size_t length);
int nx30_d_uboot_program_core(const void *image, size_t length,
			      u32 expected_generation, u32 expected_crc32,
			      const struct nx30_d_uboot_geometry *geometry,
			      nx30_d_uboot_digest_fn digest, void *digest_context,
			      const struct nx30_d_uboot_io *io,
			      void *partition_buffer, size_t partition_capacity,
			      void *verify_buffer, size_t verify_capacity,
			      u32 *bytes_verified, u32 *stage);

#endif
