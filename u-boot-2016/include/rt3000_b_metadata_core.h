/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_B_METADATA_CORE_H__
#define __RT3000_B_METADATA_CORE_H__
#ifdef RT3_METADATA_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint8_t rt3_md_u8;
typedef uint16_t rt3_md_u16;
typedef uint32_t rt3_md_u32;
typedef uint64_t rt3_md_u64;
#else
#include <linux/types.h>
typedef u8 rt3_md_u8;
typedef u16 rt3_md_u16;
typedef u32 rt3_md_u32;
typedef u64 rt3_md_u64;
#endif
#ifdef RT3_METADATA_HOST_TEST_C
int rt3_md_test_target_identity(const rt3_md_u8 *load, rt3_md_u32 size);
#endif
enum rt3_md_kind { RT3_MD_UBOOT=1, RT3_MD_ART=2, RT3_MD_CDT=3, RT3_MD_MIBIB=4 };
#define RT3_MD_APPSBL_SIZE 0x00140000U
#define RT3_MD_ART_SIZE 0x00100000U
#define RT3_MD_CDT_SIZE 0x00040000U
#define RT3_MD_PAGE_SIZE 0x00000800U
#define RT3_MD_ERASE_SIZE 0x00020000U
int rt3_md_validate_image(enum rt3_md_kind kind,const rt3_md_u8 *data,rt3_md_u32 size);
int rt3_md_validate_elf(const rt3_md_u8 *data,rt3_md_u32 size);
const char *rt3_md_uboot_image_name(const rt3_md_u8 *data,rt3_md_u32 size);
const char *rt3_md_uboot_board_name(void);
int rt3_md_prepare_elf(const rt3_md_u8 *src,rt3_md_u32 src_size,
		       rt3_md_u8 *dst,rt3_md_u32 capacity,rt3_md_u32 *out_size);
int rt3_md_snapshot_copy(rt3_md_u8 *dst,const rt3_md_u8 *src,rt3_md_u32 size);
int rt3_md_hash_matches(const rt3_md_u8 *expected,const rt3_md_u8 *actual,rt3_md_u32 size);
#endif
