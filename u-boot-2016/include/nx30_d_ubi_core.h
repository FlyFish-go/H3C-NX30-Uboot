/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_UBI_CORE_H__
#define __NX30_D_UBI_CORE_H__
#include <linux/types.h>
#define NX30_D_UBI_PEB_SIZE 0x20000U
#define NX30_D_UBI_PAGE_SIZE 0x800U
#define NX30_D_UBI_VID_OFFSET 0x800U
#define NX30_D_UBI_DATA_OFFSET 0x1000U
#define NX30_D_UBI_LEB_SIZE (NX30_D_UBI_PEB_SIZE-NX30_D_UBI_DATA_OFFSET)
#define NX30_D_UBI_IMAGE_SIZE 0x02800000U
#define NX30_D_UBI_PEB_COUNT (NX30_D_UBI_IMAGE_SIZE/NX30_D_UBI_PEB_SIZE)
#define NX30_D_UBI_MAX_VOLUMES 128U
struct nx30_d_ubi_volume {
 u32 id, reserved_pebs, used_ebs;
 u8 type;
 char name[32];
 u16 name_len;
};
struct nx30_d_ubi_map {
 u32 peb_count, volume_count, image_seq, reserved_pebs;
 u32 kernel_id, rootfs_id;
 u32 kernel_bytes, rootfs_bytes, kernel_lebs, rootfs_lebs;
 u32 kernel_peb[NX30_D_UBI_PEB_COUNT];
 u32 rootfs_peb[NX30_D_UBI_PEB_COUNT];
 u32 kernel_data_size[NX30_D_UBI_PEB_COUNT];
 u32 rootfs_data_size[NX30_D_UBI_PEB_COUNT];
 struct nx30_d_ubi_volume volumes[NX30_D_UBI_MAX_VOLUMES];
};
typedef u32 (*nx30_d_ubi_crc_fn)(u32 seed, const unsigned char *data, unsigned int len);
int nx30_d_ubi_validate(const unsigned char *image, u32 size,
                        nx30_d_ubi_crc_fn crc_fn,
                        struct nx30_d_ubi_map *map);
#endif
