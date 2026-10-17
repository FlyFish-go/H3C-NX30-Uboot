// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 chenxin527. All Rights Reserved.
 *
 * This file is part of the project uboot-qsdk12.5-build
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <common.h>
#include <mmc.h>
#include <sdhci.h>
#include <malloc.h>
#include <errno.h>
#include <ipq_api.h>
#include <flashrw.h>
#include <bootconfig.h>

#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
#define H3C_BOOTCFG_POLICY_LABEL "H3C HW-C"
#else
#define H3C_BOOTCFG_POLICY_LABEL "RT3000-B"
#endif

#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif

static const qca_smem_flash_info_t *sfi = &qca_smem_flash_info;

#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_D) && defined(CONFIG_H3C_RELEASE_SYSTEM_DEFAULT)
#include <nand.h>
#include <h3c_bootconfig_readonly.h>

/* Release boot consumes BC without entering the legacy repair/write path.
 * These are the audited ordinary OEM MIBIB physical windows, not MTD aliases. */
struct h3c_bc_release_context {
	nand_info_t *nand;
	uint32_t offset[2];
};
static int h3c_bc_release_read(void *opaque, unsigned int copy,
	unsigned char *record, unsigned int requested, unsigned int *actual)
{
	struct h3c_bc_release_context *context = opaque;
	size_t length = requested;
	int ret;
	*actual = 0;
	if (copy > 1 || requested != H3C_BC_RO_BYTES)
		return -EINVAL;
	/* Do not move a record out of its physical copy or skip to another block. */
	ret = nand_block_isbad(context->nand, context->offset[copy]);
	if (ret)
		return -EIO;
	ret = nand_read(context->nand, context->offset[copy], &length, record);
	if (length <= requested)
		*actual = (unsigned int)length;
	/* Any transport/ECC warning, short read, or impossible length is unusable. */
	if (ret || length != requested)
		return -EIO;
	return 0;
}
static int h3c_bc_release_get(const char *part_name)
{
	struct h3c_bc_release_context context;
	struct h3c_bc_ro_result result;
	uint32_t size[2];
	int ret;
	if ((sfi->flash_type != SMEM_BOOT_NAND_FLASH &&
	     sfi->flash_type != SMEM_BOOT_QSPI_NAND_FLASH) || !has_nand())
		return -ENODEV;
	context.nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	if (context.nand->size != 0x8000000ULL ||
	    context.nand->writesize != 2048 || context.nand->erasesize != 0x20000)
		return -EINVAL;
	ret = getpart_offset_size(BOOTCONFIG_PART_NAME, &context.offset[0], &size[0]);
	if (ret)
		return -EINVAL;
	ret = getpart_offset_size(BOOTCONFIG_BACKUP_PART_NAME, &context.offset[1], &size[1]);
	if (ret || context.offset[0] != 0x100000 || context.offset[1] != 0x140000 ||
	    size[0] != 0x40000 || size[1] != 0x40000)
		return -EINVAL;
	ret = h3c_bc_ro_select(h3c_bc_release_read, &context, part_name, &result);
	if (ret)
		return -EINVAL;
	return (int)result.selector;
}
#endif


/**
 * get_bootconfig_part_offset - Get bootconfig partition offset
 */
int get_bootconfig_part_offset(const char *part_name)
{
	block_dev_desc_t *mmc_dev;
	disk_partition_t disk_info = {0};
	uint32_t offset_bytes = 0, size_bytes = 0;
	int ret;

	switch (sfi->flash_type) {
	case SMEM_BOOT_NAND_FLASH:
	case SMEM_BOOT_NOR_FLASH:
	case SMEM_BOOT_NORPLUSEMMC:
	case SMEM_BOOT_NORPLUSNAND:
	case SMEM_BOOT_ONENAND_FLASH:
	case SMEM_BOOT_QSPI_NAND_FLASH:
	case SMEM_BOOT_SPI_FLASH:
		ret = getpart_offset_size((char *)part_name, &offset_bytes, &size_bytes);
		if (ret)
			return -ENOENT;
		return offset_bytes;
	case SMEM_BOOT_MMC_FLASH:
	case SMEM_BOOT_NO_FLASH:
	case SMEM_BOOT_SDC_FLASH:
		if (!has_mmc())
			return -ENODEV;
		mmc_dev = mmc_get_dev(mmc_host.dev_num);
		if (!mmc_dev)
			return -ENODEV;
		ret = get_partition_info_efi_by_name(mmc_dev, (char *)part_name, &disk_info);
		if (ret)
			return -ENOENT;
		return disk_info.start * disk_info.blksz;
	default:
		puts("Unsupported flash type\n");
		return -EINVAL;
	}
}

/**
 * read_bootconfig - Read bootconfig data to bootcfg structure
 */
int read_bootconfig(bootconfig_info_t *bootcfg, bool skip_validation, bool quiet)
{
	int offset_bytes, ret;

	offset_bytes = get_bootconfig_part_offset(bootcfg->part_name);
	if (offset_bytes < 0) {
		if (!quiet)
			printf("Partition %s not found\n", bootcfg->part_name);
		return -ENOENT;
	}

	bootcfg->offset = offset_bytes;

	switch (sfi->flash_type) {
	case SMEM_BOOT_NOR_FLASH:
	case SMEM_BOOT_NORPLUSEMMC:
	case SMEM_BOOT_NORPLUSNAND:
	case SMEM_BOOT_SPI_FLASH:
		ret = read_data_from_spi(bootcfg->offset,
			bootcfg->size, &bootcfg->info, bootcfg->size);
		break;
	case SMEM_BOOT_NAND_FLASH:
	case SMEM_BOOT_ONENAND_FLASH:
	case SMEM_BOOT_QSPI_NAND_FLASH:
		ret = read_data_from_nand(bootcfg->offset,
			bootcfg->size, &bootcfg->info, bootcfg->size);
		break;
	case SMEM_BOOT_MMC_FLASH:
	case SMEM_BOOT_NO_FLASH:
	case SMEM_BOOT_SDC_FLASH:
		ret = read_data_from_mmc(bootcfg->offset,
			bootcfg->size, &bootcfg->info, bootcfg->size);
		break;
	default:
		if (!quiet)
			puts("Unsupported flash type\n");
		return -EINVAL;
	}

	if (ret) {
		if (!quiet)
			printf("Failed to read bootconfig data from partition %s\n", bootcfg->part_name);
		return ret;
	}

	if (skip_validation)
		return 0;

	/* Validate magic numbers */
	if (bootcfg->info.magic_start != BOOTCONFIG_MAGIC_START &&
	    bootcfg->info.magic_start != BOOTCONFIG_MAGIC_START_TRYMODE) {
		if (!quiet)
			printf("Invalid magic_start: 0x%08x in %s\n", bootcfg->info.magic_start, bootcfg->part_name);
		return -EINVAL;
	}

	if (bootcfg->info.magic_end != BOOTCONFIG_MAGIC_END) {
		if (!quiet)
			printf("Invalid magic_end: 0x%08x in %s\n", bootcfg->info.magic_end, bootcfg->part_name);
		return -EINVAL;
	}

	if (bootcfg->info.numaltpart > NUM_ALT_PARTITION) {
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
		return -EINVAL; /* Do not guess a slot from a malformed table. */
#endif
		if (!quiet)
			printf("Warning: numaltpart (%u) exceeds maximum (%d), truncating\n",
				bootcfg->info.numaltpart, NUM_ALT_PARTITION);
		bootcfg->info.numaltpart = NUM_ALT_PARTITION;
	}

	return 0;
}

int read_bootconfig_and_write_back_if_needed(bootconfig_info_t *bootcfg)
{
	bootconfig_info_t backup_bootcfg;
	int ret;

	bootcfg->part_name = BOOTCONFIG_PART_NAME;
	bootcfg->size = sizeof(struct bootconfig_info);

	ret = read_bootconfig(bootcfg, false, false);
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	if (!ret)
		return 0;
	backup_bootcfg.part_name = BOOTCONFIG_BACKUP_PART_NAME;
	backup_bootcfg.size = sizeof(struct bootconfig_info);
	printf("Primary BOOTCONFIG invalid; trying backup %s\n", backup_bootcfg.part_name);
	ret = read_bootconfig(&backup_bootcfg, false, false);
	if (ret) {
		puts("BOOTCONFIG invalid; stage-0 will not repair it\n");
		return ret;
	}
	memcpy(&bootcfg->info, &backup_bootcfg.info, backup_bootcfg.size);
	bootcfg->part_name = backup_bootcfg.part_name;
	bootcfg->offset = backup_bootcfg.offset;
	printf("Using validated %s in RAM only\n", bootcfg->part_name);
	return 0;
#else
	if (ret == 0 || ret == -ENOENT)
		return ret;

	backup_bootcfg.part_name = BOOTCONFIG_BACKUP_PART_NAME;
	backup_bootcfg.size = sizeof(struct bootconfig_info);

	printf("Read bootconfig data from partition %s\n", backup_bootcfg.part_name);

	ret = read_bootconfig(&backup_bootcfg, false, false);
	if (ret) {
		puts("Bootconfig data corrupted, please restore manually\n");
		return ret;
	}

	printf("Write bootconfig data back to partition %s\n", bootcfg->part_name);
	memcpy(&bootcfg->info, &backup_bootcfg.info, backup_bootcfg.size);
	return write_bootconfig(bootcfg);
#endif
}

/**
 * write_bootconfig - Write bootconfig data back to partition
 */
int write_bootconfig(bootconfig_info_t *bootcfg)
{
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	printf("BOOTCONFIG writes blocked by %s stage-0 policy\n", H3C_BOOTCFG_POLICY_LABEL);
	return -EROFS;
#else
	int ret;

	switch (sfi->flash_type) {
	case SMEM_BOOT_NOR_FLASH:
	case SMEM_BOOT_NORPLUSEMMC:
	case SMEM_BOOT_NORPLUSNAND:
	case SMEM_BOOT_SPI_FLASH:
		ret = write_data_to_spi(bootcfg->offset, bootcfg->size, &bootcfg->info);
		break;
	case SMEM_BOOT_NAND_FLASH:
	case SMEM_BOOT_ONENAND_FLASH:
	case SMEM_BOOT_QSPI_NAND_FLASH:
		ret = write_data_to_nand(bootcfg->offset, bootcfg->size, &bootcfg->info);
		break;
	case SMEM_BOOT_MMC_FLASH:
	case SMEM_BOOT_NO_FLASH:
	case SMEM_BOOT_SDC_FLASH:
		ret = write_data_to_mmc(bootcfg->offset, bootcfg->size, &bootcfg->info);
		break;
	default:
		puts("Unsupported flash type\n");
		return -EINVAL;
	}

	if (ret)
		printf("Failed to write bootconfig data back to partition %s\n", bootcfg->part_name);

	return ret;
#endif
}

bool validate_bootconfig(void)
{
    bootconfig_info_t bootcfg;
	int ret;

	bootcfg.size = sizeof(struct bootconfig_info);

	bootcfg.part_name = BOOTCONFIG_PART_NAME;
	ret = read_bootconfig(&bootcfg, false, true);
	if (ret == 0 || ret == -ENOENT)
		goto done;

	bootcfg.part_name = BOOTCONFIG_BACKUP_PART_NAME;
	ret = read_bootconfig(&bootcfg, false, true);

done:
    return ret ? false : true;
}

/**
 * sync_bootconfig - Sync 0:BOOTCONFIG1 with 0:BOOTCONFIG or vice versa
 */
int sync_bootconfig(bool reverse)
{
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	printf("BOOTCONFIG sync blocked by %s stage-0 policy\n", H3C_BOOTCFG_POLICY_LABEL);
	return -EROFS;
#else
	bootconfig_info_t src, dst;
	int ret;

	/* Check if 0:BOOTCONFIG1 partition exists */
	ret = get_bootconfig_part_offset(BOOTCONFIG_BACKUP_PART_NAME);
	if (ret < 0) {
		printf("Partition %s does not exist, skip sync\n", BOOTCONFIG_BACKUP_PART_NAME);
		return 0;
	}

	if (reverse) {
		/* 0:BOOTCONFIG1 -> 0:BOOTCONFIG */
		src.part_name = BOOTCONFIG_BACKUP_PART_NAME;
		dst.part_name = BOOTCONFIG_PART_NAME;
	} else {
		/* 0:BOOTCONFIG -> 0:BOOTCONFIG1 */
		src.part_name = BOOTCONFIG_PART_NAME;
		dst.part_name = BOOTCONFIG_BACKUP_PART_NAME;
	}

	src.size = sizeof(struct bootconfig_info);
	dst.size = sizeof(struct bootconfig_info);

	/* Read source BOOTCONFIG partition */
	ret = read_bootconfig(&src, false, false);
	if (ret)
		return ret;

	/* Read destination BOOTCONFIG partition */
	ret = read_bootconfig(&dst, true, false);
	if (ret)
		return ret;

	printf("Syncing %s -> %s: ", src.part_name, dst.part_name);

	/* Compare the two bootconfig structures */
	ret = memcmp(&dst.info, &src.info, src.size);
	if (!ret) {
		puts("already in sync\n\n");
		return 0;
	}

	/* They are different, need to sync */
	memcpy(&dst.info, &src.info, src.size);

	ret = write_bootconfig(&dst);
	if (!ret)
		puts("success\n\n");
	else
		printf("failure (errno: %d)\n\n", ret);

	return ret;
#endif
}

/**
 * print_bootconfig - Print all bootconfig information
 */
int print_bootconfig(void)
{
	bootconfig_info_t bootcfg;
	int ret;

	ret = read_bootconfig_and_write_back_if_needed(&bootcfg);
	if (ret)
		return ret;

	puts("\n========== Bootconfig Information ==========\n");
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	printf("Source Partition: %s\n", bootcfg.part_name);
#endif
	printf("Magic Start:      0x%08x %s\n", bootcfg.info.magic_start,
	       bootcfg.info.magic_start == BOOTCONFIG_MAGIC_START ? "(Normal)" :
	       bootcfg.info.magic_start == BOOTCONFIG_MAGIC_START_TRYMODE ? "(Try Mode)" : "(Unknown)");
	printf("Magic End:        0x%08x\n", bootcfg.info.magic_end);
	printf("Age:              0x%08x\n", bootcfg.info.age);
	printf("Number of Alt Partitions: %u\n", bootcfg.info.numaltpart);
	printf("\n%-3s %-16s %s\n", "Idx", "Partition Name", "Primary Boot");
	printf("--------------------------------------------\n");

	for (int i = 0; i < bootcfg.info.numaltpart && i < NUM_ALT_PARTITION; i++) {
		printf("%3d: %-16s %u\n", i, bootcfg.info.per_part_entry[i].name,
		       bootcfg.info.per_part_entry[i].primaryboot);
	}
	puts("============================================\n\n");

	return 0;
}

/**
 * get_bootconfig - Get primaryboot value for a partition
 */
int get_bootconfig(const char *part_name)
{
#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_D) && defined(CONFIG_H3C_RELEASE_SYSTEM_DEFAULT)
	return h3c_bc_release_get(part_name);
#else
	bootconfig_info_t bootcfg;
	int ret;
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	int found = -1;
#endif

	ret = read_bootconfig_and_write_back_if_needed(&bootcfg);
	if (ret)
		return ret;

	for (int i = 0; i < bootcfg.info.numaltpart && i < NUM_ALT_PARTITION; i++) {
		if (strncmp(bootcfg.info.per_part_entry[i].name, part_name,
			    ALT_PART_NAME_LENGTH) == 0) {
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
			if (found >= 0)
				return -EINVAL; /* Duplicate selector is ambiguous. */
			found = i;
#else
			return bootcfg.info.per_part_entry[i].primaryboot;
#endif
		}
	}
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	if (found >= 0)
		return bootcfg.info.per_part_entry[found].primaryboot;
#endif

	printf("Entry '%s' not found\n", part_name);
	return -ENOENT;
#endif
}

/**
 * set_bootconfig - Set primaryboot value for partition(s)
 */
int set_bootconfig(const char *name, uint32_t value)
{
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	printf("BOOTCONFIG changes blocked by %s stage-0 policy\n", H3C_BOOTCFG_POLICY_LABEL);
	return -EROFS;
#else
	bootconfig_info_t bootcfg;
	int idx, modified = 0;
	int ret;

	if (value != 0 && value != 1) {
		printf("Invalid value: %u (must be 0 or 1)\n", value);
		return -EINVAL;
	}

	ret = read_bootconfig_and_write_back_if_needed(&bootcfg);
	if (ret)
		return ret;

	if (strcmp(name, "all") == 0) {
		/* Handle "all" special case */
		for (idx = 0; idx < bootcfg.info.numaltpart && idx < NUM_ALT_PARTITION; idx++) {
			if (bootcfg.info.per_part_entry[idx].primaryboot != value) {
				bootcfg.info.per_part_entry[idx].primaryboot = value;
				modified++;
				printf("Set %s to %u\n", bootcfg.info.per_part_entry[idx].name, value);
			} else {
				printf("%s already %u\n", bootcfg.info.per_part_entry[idx].name, value);
			}
		}
		if (modified == 0)
			printf("All partitions already have value %u, no change\n", value);
	} else if (strcmp(name, "firmware") == 0) {
		/* Handle firmware partitions */
		const char *fw_parts[] = {
			"0:HLOS", "rootfs", "0:WIFIFW"
#ifdef CONFIG_ARCH_IPQ5018
			, "0:BTFW"
#endif
		};
		for (idx = 0; idx < bootcfg.info.numaltpart && idx < NUM_ALT_PARTITION; idx++) {
			for (int j = 0; j < ARRAY_SIZE(fw_parts); j++) {
				if (strncmp(bootcfg.info.per_part_entry[idx].name, fw_parts[j],
						ALT_PART_NAME_LENGTH) == 0) {
					if (bootcfg.info.per_part_entry[idx].primaryboot != value) {
						bootcfg.info.per_part_entry[idx].primaryboot = value;
						modified++;
						printf("Set %s to %u\n", fw_parts[j], value);
					} else {
						printf("%s already %u\n", fw_parts[j], value);
					}
					break;
				}
			}
		}
		if (modified == 0)
			printf("All firmware partitions already have value %u, no change\n", value);
	} else {
		/* Handle specific partition entry */
		for (idx = 0; idx < bootcfg.info.numaltpart && idx < NUM_ALT_PARTITION; idx++) {
			if (strncmp(bootcfg.info.per_part_entry[idx].name, name,
				    ALT_PART_NAME_LENGTH) == 0) {
				if (bootcfg.info.per_part_entry[idx].primaryboot != value) {
					bootcfg.info.per_part_entry[idx].primaryboot = value;
					modified = 1;
					printf("Set %s to %u\n", name, value);
				} else {
					printf("%s already %u, no change\n", name, value);
				}
				break;
			}
		}
		if (idx >= bootcfg.info.numaltpart || idx >= NUM_ALT_PARTITION) {
			printf("Partition entry '%s' not found\n", name);
			return -ENOENT;
		}
	}

	/* Only write back if modifications were made */
	if (modified) {
		ret = write_bootconfig(&bootcfg);
		if (ret)
			return ret;
		puts("\nBootconfig updated successfully\n\n");
	}

	/* Always sync 0:BOOTCONFIG -> 0:BOOTCONFIG1 */
	return sync_bootconfig(false);
#endif
}
