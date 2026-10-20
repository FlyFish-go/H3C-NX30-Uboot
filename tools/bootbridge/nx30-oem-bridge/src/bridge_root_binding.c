// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include "bridge_root_binding.h"
#include <stdio.h>
#include <string.h>

/* Retain the reviewed GlueBI topology; validate volume geometry against the
 * exact source-bound build contract rather than one model's observation. */

static int option_read_only(const char *options)
{
    char copy[512];
    unsigned ro = 0, rw = 0;
    if (!options || strlen(options) >= sizeof(copy)) return 0;
    memcpy(copy, options, strlen(options) + 1);
    char *save = NULL;
    for (char *t = strtok_r(copy, ",", &save); t; t = strtok_r(NULL, ",", &save)) {
        if (!strcmp(t, "ro")) ++ro;
        if (!strcmp(t, "rw")) ++rw;
    }
    return ro == 1 && rw == 0;
}

static int cmdline_bindings(const char *cmdline)
{
    char copy[4096];
    unsigned ubi = 0, root = 0, fs = 0;
    if (!cmdline || strlen(cmdline) >= sizeof(copy)) return -1;
    memcpy(copy, cmdline, strlen(cmdline) + 1);
    char *save = NULL;
    for (char *t = strtok_r(copy, " \t\r\n", &save); t; t = strtok_r(NULL, " \t\r\n", &save)) {
        if (!strncmp(t, "ubi.mtd=", 8)) {
            if (++ubi != 1 || strcmp(t + 8, "rootfs")) return -1;
        } else if (!strncmp(t, "root=", 5)) {
            if (++root != 1 || strcmp(t + 5, "mtd:ubi_rootfs")) return -1;
        } else if (!strncmp(t, "rootfstype=", 11)) {
            if (++fs != 1 || strcmp(t + 11, "squashfs")) return -1;
        }
    }
    return ubi == 1 && root == 1 && fs == 1 ? 0 : -1;
}

int br_validate_gluebi_root(const br_gluebi_evidence *e,
                          const br_gluebi_contract *contract,
                          uint32_t *attached_mtd_index)
{
    char expected_path[128], expected_devname[32];
    if (!e || !contract || !attached_mtd_index ||
        !contract->volume_bytes || !contract->usable_leb_bytes ||
        !contract->reserved_ebs || !contract->mtd_write_bytes ||
        contract->reserved_ebs > UINT64_MAX / contract->usable_leb_bytes ||
        contract->reserved_ebs * contract->usable_leb_bytes != contract->volume_bytes ||
        contract->usable_leb_bytes % contract->mtd_write_bytes)
        return BR_GB_ERR_ARGUMENT;
    if (snprintf(expected_path, sizeof(expected_path),
                 "/sys/devices/virtual/mtd/mtd%u/mtdblock%u",
                 e->block_mtd_index, e->block_mtd_index) < 0 ||
        snprintf(expected_devname, sizeof(expected_devname), "mtdblock%u",
                 e->block_mtd_index) < 0) return BR_GB_ERR_ARGUMENT;

    if (e->root_major != e->event_major || e->root_minor != e->event_minor ||
        !e->root_major || !e->root_minor ||
        !e->event_major || !e->event_minor) return BR_GB_ERR_ROOT_DEVICE;
    if (
        !e->block_devname || strcmp(e->block_devname, expected_devname) ||
        !e->block_devtype || strcmp(e->block_devtype, "disk") ||
        !e->block_sysfs_path || strcmp(e->block_sysfs_path, expected_path)) return BR_GB_ERR_BLOCK_IDENTITY;
    if (e->mtd_name_matches != 1 || e->proc_mtd_name_matches != 1 ||
        !e->mtd_name || strcmp(e->mtd_name, "ubi_rootfs") ||
        !e->mtd_type || strcmp(e->mtd_type, "ubi") ||
        e->mtd_major != 90 || e->mtd_minor != 48) return BR_GB_ERR_MTD_IDENTITY;
    if (e->attached_ubi_count != 1 || e->ubi_id != 0 ||
        e->root_volume_name_matches != 1 || e->volume_ubi_id != e->ubi_id ||
        e->volume_id != 3 || !e->volume_name || strcmp(e->volume_name, "ubi_rootfs")) return BR_GB_ERR_UBI_MAPPING;
    if (e->mtd_size != contract->volume_bytes || e->mtd_erase != contract->usable_leb_bytes ||
        e->mtd_write != contract->mtd_write_bytes ||
        !e->volume_type || strcmp(e->volume_type, "dynamic") ||
        e->volume_alignment != 1 || e->volume_corrupted != 0 ||
        e->volume_update_marker != 0 || e->usable_leb_size != contract->usable_leb_bytes ||
        e->volume_reserved_ebs != contract->reserved_ebs ||
        e->volume_data_bytes != contract->volume_bytes || e->mtd_size != e->volume_data_bytes ||
        e->volume_reserved_ebs > UINT64_MAX / e->usable_leb_size ||
        e->volume_reserved_ebs * e->usable_leb_size != e->volume_data_bytes) return BR_GB_ERR_VOLUME_GEOMETRY;
    if (!e->mount_source || strcmp(e->mount_source, "mtd:ubi_rootfs") ||
        !e->mount_target || strcmp(e->mount_target, "/") ||
        !e->mount_type || strcmp(e->mount_type, "squashfs") ||
        !option_read_only(e->mount_options)) return BR_GB_ERR_MOUNT_POLICY;
    if (cmdline_bindings(e->cmdline)) return BR_GB_ERR_CMDLINE_BINDING;
    *attached_mtd_index = e->ubi_mtd_index;
    return 0;
}

const char *br_gluebi_result_string(br_gluebi_result result)
{
    switch (result) {
    case BR_GB_OK: return "ok";
    case BR_GB_ERR_ARGUMENT: return "invalid validator input";
    case BR_GB_ERR_ROOT_DEVICE: return "root st_dev does not match sysfs uevent MAJOR/MINOR";
    case BR_GB_ERR_BLOCK_IDENTITY: return "root mtdblock DEVNAME/DEVTYPE/sysfs path mismatch";
    case BR_GB_ERR_MTD_IDENTITY: return "GlueBI MTD name/type/dev_t/unique mapping mismatch";
    case BR_GB_ERR_UBI_MAPPING: return "unique attached UBI/root volume identity mismatch";
    case BR_GB_ERR_VOLUME_GEOMETRY: return "observed GlueBI/UBI volume geometry mismatch";
    case BR_GB_ERR_MOUNT_POLICY: return "root mount source/target/type/read-only policy mismatch";
    case BR_GB_ERR_CMDLINE_BINDING: return "kernel cmdline UBI/root/rootfstype binding mismatch";
    default: return "unknown GlueBI root-binding result";
    }
}
