#ifndef NX30_D_SERVICE_HOST_TEST_H
#define NX30_D_SERVICE_HOST_TEST_H
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define NX30_D_ENV_KEY_COUNT 6
#define NX30_D_ENV_NAME_LEN 24
#define NX30_D_ENV_VALUE_LEN 128
#define NX30_D_ENV_MAX_CHANGES 6
#define NX30_D_TOKEN_LEN 17
enum nx30_d_slot { NX30_D_SLOT_A = 0, NX30_D_SLOT_B = 1 };
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
 u32 generation,nand_size,erase_size,write_size,ecc_bits;
 struct nx30_d_extent slot_a,slot_b,env,bc0,bc1,appsbl_high,appsbl_low,art,training,appsbl_active;
 struct nx30_d_extent sbl1,mibib,qsee,qsee_1,devcfg,devcfg_1,cdt,cdt_1,pdt_data,pdt_data_1,exp,plugin;
};
struct nx30_d_slots {
 u32 generation,selector,age,ecc_bits; enum nx30_d_slot active,inactive;
 char logical_a[16],logical_b[16],active_logical[16];
};
struct nx30_d_env_change { char name[NX30_D_ENV_NAME_LEN],value[NX30_D_ENV_VALUE_LEN]; u8 unset; };
struct nx30_d_env_snapshot { u32 generation; char values[NX30_D_ENV_KEY_COUNT][NX30_D_ENV_VALUE_LEN]; };
struct nx30_d_result { int status; u32 stage,generation,crc_before,crc_after,bytes_verified,selector,age; u64 target_offset,target_size; };
struct nx30_d_firmware_info { u32 generation,image_crc32,image_bytes,peb_count; enum nx30_d_slot target; char release[64]; };
int nx30_d_layout_get(struct nx30_d_layout *);
int nx30_d_slots_get(struct nx30_d_slots *);
int nx30_d_object_extent(enum nx30_d_object, struct nx30_d_extent *);
int nx30_d_read_object(enum nx30_d_object,u64,void *,size_t,size_t *);
int nx30_d_boot_slot(enum nx30_d_slot);
int nx30_d_default_prepare(enum nx30_d_slot,char [NX30_D_TOKEN_LEN],struct nx30_d_result *);
int nx30_d_default_commit(const char *,struct nx30_d_result *);
int nx30_d_default_original_get(unsigned int,const void **,size_t *);
int nx30_d_default_failure_get(struct nx30_d_result *,u32 *);
int nx30_d_default_abort(const char *);
int nx30_d_firmware_validate(const void *,size_t,struct nx30_d_firmware_info *);
int nx30_d_firmware_write_inactive(const void *,size_t,u32,u32,struct nx30_d_result *);
void slots_rt3000_b_invalidate_cache(void);
int nx30_d_env_get(struct nx30_d_env_snapshot *);
int nx30_d_env_prepare(const struct nx30_d_env_change *,size_t,u32,char [NX30_D_TOKEN_LEN],struct nx30_d_result *);
int nx30_d_env_commit(const char *,struct nx30_d_result *);
void nx30_d_env_abort(const char *);
int nx30_d_uboot_high_copy_active(void);
int nx30_d_uboot_target_check(u64 offset,u64 size);
int nx30_d_uboot_validate(const void *,size_t,struct nx30_d_firmware_info *);
int nx30_d_uboot_write(const void *,size_t,u32,u32,struct nx30_d_result *);
#endif
