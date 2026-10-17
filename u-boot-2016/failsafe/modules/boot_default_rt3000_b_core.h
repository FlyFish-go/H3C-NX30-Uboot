#ifndef __RT3_BOOT_DEFAULT_CORE_H__
#define __RT3_BOOT_DEFAULT_CORE_H__
#ifdef RT3_BOOTCFG_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint8_t rt3_boot_u8;
typedef uint32_t rt3_boot_u32;
#else
#include <linux/types.h>
#include <linux/stddef.h>
typedef u8 rt3_boot_u8;
typedef u32 rt3_boot_u32;
#endif

#define RT3_BOOTCFG_SIZE 0x40000U
#define RT3_BOOTCFG_INFO_SIZE 336U
#define RT3_BOOTCFG_ENTRIES 16U
#define RT3_BOOTCFG_ROOTFS_OFFSET 0x80U
#define RT3_BOOTCFG_MAGIC 0xA3A2A1A0U
#define RT3_BOOTCFG_TRY_MAGIC 0xA3A2A1A1U
#define RT3_BOOTCFG_END 0xB3B2B1B0U

struct rt3_bootcfg_view {
    rt3_boot_u32 age;
    rt3_boot_u32 count;
    rt3_boot_u32 rootfs_row;
    rt3_boot_u32 rootfs_selector;
};

int rt3_bootcfg_validate_pair(const rt3_boot_u8 *primary, const rt3_boot_u8 *backup,
                              size_t size, struct rt3_bootcfg_view *view);
int rt3_bootcfg_stage(const rt3_boot_u8 *source, size_t size, rt3_boot_u32 selector,
                      rt3_boot_u8 *staged);

#endif
