/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_B_FW_CORE_H__
#define __RT3000_B_FW_CORE_H__
#ifdef RT3_FW_HOST_TEST
#include <stdint.h>
#include <stddef.h>
typedef uint8_t rt3_core_u8;
typedef uint32_t rt3_core_u32;
typedef uint64_t rt3_core_u64;
#else
#include <linux/types.h>
typedef u8 rt3_core_u8;
typedef u32 rt3_core_u32;
typedef u64 rt3_core_u64;
#endif

/* Where rt3_fw_core_transaction stopped. Before ERASE nothing was changed. */
enum rt3_fw_stage {
	RT3_FW_STAGE_NONE = 0,
	RT3_FW_STAGE_SCAN,	/* bad-block scan or capacity */
	RT3_FW_STAGE_OPEN,	/* write window refused */
	RT3_FW_STAGE_ERASE,
	RT3_FW_STAGE_WRITE,
	RT3_FW_STAGE_VERIFY,	/* readback of written or erased PEBs */
};

struct rt3_fw_io {
	void *ctx;
	int (*is_bad)(void *ctx, rt3_core_u64 offset);
	int (*erase)(void *ctx, rt3_core_u64 offset, rt3_core_u32 size);
	int (*write)(void *ctx, rt3_core_u64 offset, const rt3_core_u8 *buf,
		     rt3_core_u32 size, rt3_core_u32 *written);
	int (*read)(void *ctx, rt3_core_u64 offset, rt3_core_u8 *buf,
		    rt3_core_u32 size, rt3_core_u32 *read);
	int (*window_open)(void *ctx, rt3_core_u64 offset,
			   rt3_core_u64 size);
	void (*window_close)(void *ctx);
	int *stage;		/* optional: set to enum rt3_fw_stage */
};

/* Authorize only one exact MTD and range, with ordinary ECC data mode. */
int rt3_fw_core_range_allows(int open, const void *window_mtd,
			     const void *request_mtd, rt3_core_u64 start,
			     rt3_core_u64 end, rt3_core_u64 offset,
			     rt3_core_u64 size, int raw, int explicit_oob);

/* Bind the one-shot token to target, mode, table fingerprint and staged bytes. */
int rt3_fw_core_request_matches(rt3_core_u32 pending_token,
				 rt3_core_u32 request_token,
				 const char *pending_target,
				 const char *request_target,
				 const char *pending_mode,
				 const char *request_mode,
				 rt3_core_u32 pending_fingerprint,
				 rt3_core_u32 current_fingerprint,
				 const rt3_core_u8 *expected_digest,
				 const rt3_core_u8 *current_digest,
				 unsigned int digest_size);
int rt3_fw_core_input_size_ok(rt3_core_u64 image_size,
			      rt3_core_u64 max_size,
			      rt3_core_u64 target_size);

/* The input image must fit in good PEBs; independently reserve enough good
 * PEBs for the volume table's allocation plus UBI's bad-block/WL/EBA margin.
 * EC-only free PEBs already present in the input image count toward the image
 * extent and are available to UBI after attach; do not charge margin twice. */
int rt3_fw_core_capacity_ok(rt3_core_u32 good_pebs,
			    rt3_core_u32 image_pebs,
			    rt3_core_u32 reserved_pebs,
			    rt3_core_u32 margin);

/* Preflight, erase and program one target extent; always close opened window. */
int rt3_fw_core_transaction(const struct rt3_fw_io *io,
			    rt3_core_u64 target_offset,
			    rt3_core_u64 target_size,
			    const rt3_core_u8 *image,
			    rt3_core_u32 image_bytes,
			    rt3_core_u32 peb_size,
			    rt3_core_u32 image_pebs,
			    rt3_core_u32 reserved_pebs,
			    rt3_core_u32 margin,
			    rt3_core_u32 *good_offsets,
			    rt3_core_u32 max_good,
			    rt3_core_u8 *verify_buf);
#endif
