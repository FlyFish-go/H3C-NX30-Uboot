/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_SERVICE_H__
#define __NX30_D_SERVICE_H__
#include <linux/types.h>

#define NX30_D_ENV_KEY_COUNT 6
#define NX30_D_ENV_NAME_LEN 24
#define NX30_D_ENV_VALUE_LEN 128
#define NX30_D_ENV_MAX_CHANGES 6
#define NX30_D_TOKEN_LEN 17

enum nx30_d_slot { NX30_D_SLOT_A = 0, NX30_D_SLOT_B = 1 };
enum nx30_d_stage {
 NX30_D_STAGE_NONE=0, NX30_D_STAGE_PREFLIGHT=1, NX30_D_STAGE_REVALIDATE=2,
 NX30_D_STAGE_WINDOW_OPEN=3, NX30_D_STAGE_ERASE=4, NX30_D_STAGE_WRITE=5,
 NX30_D_STAGE_READBACK=6, NX30_D_STAGE_COMPLETE=7, NX30_D_STAGE_RESTORE=8,
};
enum nx30_d_object {
 NX30_D_OBJ_SLOT_A, NX30_D_OBJ_SLOT_B, NX30_D_OBJ_ENV,
 NX30_D_OBJ_BC0, NX30_D_OBJ_BC1, NX30_D_OBJ_APPSBL_HIGH,
 NX30_D_OBJ_APPSBL_LOW, NX30_D_OBJ_ART, NX30_D_OBJ_TRAINING,
 NX30_D_OBJ_SBL1, NX30_D_OBJ_MIBIB, NX30_D_OBJ_QSEE, NX30_D_OBJ_QSEE_1,
 NX30_D_OBJ_DEVCFG, NX30_D_OBJ_DEVCFG_1, NX30_D_OBJ_CDT, NX30_D_OBJ_CDT_1,
 NX30_D_OBJ_PDT_DATA, NX30_D_OBJ_PDT_DATA_1, NX30_D_OBJ_EXP, NX30_D_OBJ_PLUGIN,
 NX30_D_OBJ_BC_ORIGINAL0, NX30_D_OBJ_BC_ORIGINAL1,
};
struct nx30_d_extent { u64 offset, size; };
struct nx30_d_layout {
 u32 generation; u32 nand_size; u32 erase_size; u32 write_size; u32 ecc_bits;
 struct nx30_d_extent slot_a, slot_b, env, bc0, bc1;
 struct nx30_d_extent appsbl_high, appsbl_low, art, training, appsbl_active;
 struct nx30_d_extent sbl1, mibib, qsee, qsee_1, devcfg, devcfg_1;
 struct nx30_d_extent cdt, cdt_1, pdt_data, pdt_data_1, exp, plugin;
};
struct nx30_d_slots {
 u32 generation; u32 selector; u32 age; u32 ecc_bits;
 enum nx30_d_slot active; enum nx30_d_slot inactive;
 char logical_a[16]; char logical_b[16]; char active_logical[16];
};
struct nx30_d_env_change {
 char name[NX30_D_ENV_NAME_LEN];
 char value[NX30_D_ENV_VALUE_LEN];
 u8 unset;
};
struct nx30_d_env_snapshot {
 u32 generation;
 char values[NX30_D_ENV_KEY_COUNT][NX30_D_ENV_VALUE_LEN];
};
struct nx30_d_result {
 int status; u32 stage; u32 generation; u32 crc_before; u32 crc_after;
 u32 bytes_verified; u32 selector; u32 age; u64 target_offset; u64 target_size;
};
struct nx30_d_firmware_info {
 u32 generation; u32 image_crc32; u32 image_bytes; u32 peb_count;
 enum nx30_d_slot target;
 char release[64];
};

int nx30_d_layout_get(struct nx30_d_layout *layout);
int nx30_d_slots_get(struct nx30_d_slots *slots);
int nx30_d_object_extent(enum nx30_d_object object, struct nx30_d_extent *extent);
int nx30_d_read_object(enum nx30_d_object object, u64 offset,
                       void *buffer, size_t length, size_t *read);
int nx30_d_boot_slot(enum nx30_d_slot slot);
int nx30_d_default_prepare(enum nx30_d_slot slot, char token[NX30_D_TOKEN_LEN],
                           struct nx30_d_result *result);
int nx30_d_default_commit(const char *token, struct nx30_d_result *result);
int nx30_d_default_original_get(unsigned int,const void **,size_t *);
int nx30_d_default_failure_get(struct nx30_d_result *,u32 *);
int nx30_d_default_abort(const char *token);
int nx30_d_firmware_validate(const void *image, size_t length,
                             struct nx30_d_firmware_info *info);
int nx30_d_firmware_write_inactive(const void *image, size_t length,
                                   u32 expected_generation, u32 expected_crc32,
                                   struct nx30_d_result *result);
int nx30_d_env_get(struct nx30_d_env_snapshot *snapshot);
int nx30_d_env_prepare(const struct nx30_d_env_change *changes, size_t count,
                       u32 expected_generation,
                       char token[NX30_D_TOKEN_LEN],
                       struct nx30_d_result *result);
int nx30_d_env_commit(const char *token, struct nx30_d_result *result);
void nx30_d_env_abort(const char *token);
int nx30_d_uboot_active_target(u64 *offset, u64 *size);
int nx30_d_uboot_target_check(u64 offset,u64 size);
int nx30_d_uboot_validate(const void *image, size_t length,
                          struct nx30_d_firmware_info *info);
int nx30_d_uboot_write(const void *image, size_t length,
                       u32 expected_generation, u32 expected_crc32,
                       struct nx30_d_result *result);
#endif
