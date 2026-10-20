/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BRIDGE_ROOT_BINDING_H
#define BRIDGE_ROOT_BINDING_H

#include <stdint.h>

typedef struct {
    uint32_t root_major, root_minor;
    uint32_t event_major, event_minor;
    uint32_t block_mtd_index;
    uint32_t mtd_major, mtd_minor;
    uint32_t mtd_name_matches, proc_mtd_name_matches;
    uint32_t attached_ubi_count, ubi_id, ubi_mtd_index;
    uint32_t root_volume_name_matches, volume_ubi_id, volume_id;
    uint32_t volume_alignment, volume_corrupted, volume_update_marker;
    uint64_t mtd_size, mtd_erase, mtd_write;
    uint64_t volume_data_bytes, volume_reserved_ebs, usable_leb_size;
    const char *block_devname, *block_devtype, *block_sysfs_path;
    const char *mtd_name, *mtd_type, *mount_source, *mount_target;
    const char *mount_type, *mount_options, *cmdline;
    const char *volume_name, *volume_type;
} br_gluebi_evidence;

/* Generated from the exact source package's UBI volume table. This is an
 * expected contract, never filled from live observations being validated. */
typedef struct {
    uint64_t volume_bytes, usable_leb_bytes, reserved_ebs, mtd_write_bytes;
} br_gluebi_contract;

typedef enum {
    BR_GB_OK = 0,
    BR_GB_ERR_ARGUMENT = -1,
    BR_GB_ERR_ROOT_DEVICE = -2,
    BR_GB_ERR_BLOCK_IDENTITY = -3,
    BR_GB_ERR_MTD_IDENTITY = -4,
    BR_GB_ERR_UBI_MAPPING = -5,
    BR_GB_ERR_VOLUME_GEOMETRY = -6,
    BR_GB_ERR_MOUNT_POLICY = -7,
    BR_GB_ERR_CMDLINE_BINDING = -8
} br_gluebi_result;

/* Validate the retained read-only GlueBI topology and this package's exact
 * generated volume geometry. It does not define a generic GlueBI topology.
 * The UBI attach
 * index is returned only after every supplied sysfs/proc/mount/cmdline
 * witness agrees. */
int br_validate_gluebi_root(const br_gluebi_evidence *evidence,
                            const br_gluebi_contract *contract,
                            uint32_t *attached_mtd_index);
const char *br_gluebi_result_string(br_gluebi_result result);

#endif
