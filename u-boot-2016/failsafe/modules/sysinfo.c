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
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
#include <h3c_nand_ecc.h>
#endif
#include <command.h>
#include <linux/ctype.h>
#include <asm/arch-qca-common/smem.h>
#include <part.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <mmc.h>
#include <sdhci.h>
#include <spi.h>
#include <spi_flash.h>
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
#include <qpic_nand_model.h>
#endif
#include <malloc.h>
#include <errno.h>
#include <version.h>
#include <net/httpd.h>
#include <ipq_api.h>

#include "modules.h"

#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern struct sdhci_host mmc_host;
#endif

DECLARE_GLOBAL_DATA_PTR;

#define GPT_PART_NAME "0:GPT"
#define GPT_SIZE_IN_BLOCKS 34

#define SMEM_PTN_NAME_MAX     16
#define SMEM_PTABLE_PARTS_MAX 32

static const qca_smem_flash_info_t *sfi = &qca_smem_flash_info;

struct smem_ptn {
	char name[SMEM_PTN_NAME_MAX];
	unsigned start;
	unsigned size;
	unsigned attr;
} __attribute__ ((__packed__));

struct smem_ptable {
	unsigned magic[2];
	unsigned version;
	unsigned len;
	struct smem_ptn parts[SMEM_PTABLE_PARTS_MAX];
} __attribute__ ((__packed__));

#define APPEND_JSON_TO_BUF(fmt, args...)	\
	do {	\
		len += snprintf(buf + len, left - len, fmt, ##args);	\
	} while (0)

static void handle_response_message(struct httpd_response *response,
    int code, const char *data, const char *content_type)
{
	response->status = HTTP_RESP_STD;
	response->data = data ? data : "";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = content_type ? content_type : "text/plain";
}

static void sysinfo_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = 16666;
	struct mmc *mmc;
	const struct smem_ptable *spt;
	block_dev_desc_t *bd = NULL;
    const char *board_hostname = NULL;
    const char *board_model = NULL;
    const char *board_compat = NULL;
    char esc_board_hostname[128], esc_board_model[128], esc_board_compat[128];
    char esc_version_string[256];
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
    const char *board_family;
    char esc_board_family[64];
#endif

	if (status == HTTP_CB_CLOSED && response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->session_data = NULL;

	buf = malloc(left);
	if (!buf) {
		handle_response_message(response, 500, "no mem", NULL);
		return;
	}

	APPEND_JSON_TO_BUF("{");

    APPEND_JSON_TO_BUF("\"is_9008_mode\":%s,", is_9008_mode() ? "true" : "false");
#if defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
    APPEND_JSON_TO_BUF("\"ram_only\":false,\"read_only\":false,\"profile\":\"common\",");
#elif defined(CONFIG_H3C_HW_D_NAND_SERVICE)
#ifdef CONFIG_H3C_HW_D_SERVICE_RAM
    APPEND_JSON_TO_BUF("\"ram_only\":true,\"read_only\":false,\"profile\":\"nand-service-ram\",");
#else
    APPEND_JSON_TO_BUF("\"ram_only\":false,\"read_only\":false,\"profile\":\"nand-service\",");
#endif
#elif defined(CONFIG_H3C_HW_D_NAND_READONLY)
    APPEND_JSON_TO_BUF("\"ram_only\":false,\"read_only\":true,\"profile\":\"nand-readonly\",");
#elif defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_READONLY)
    APPEND_JSON_TO_BUF("\"ram_only\":true,\"read_only\":true,");
#elif defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT)
    APPEND_JSON_TO_BUF("\"ram_only\":false,\"read_only\":false,");
#endif

    json_escape(version_string, esc_version_string, sizeof(esc_version_string));
    APPEND_JSON_TO_BUF("\"uboot_version\":\"%s\",", esc_version_string);

    /* Board info */
    board_hostname = fdt_getprop(gd->fdt_blob, 0, "host_name", NULL);
    board_model = fdt_getprop(gd->fdt_blob, 0, "model", NULL);
    board_compat = fdt_getprop(gd->fdt_blob, 0, "compatible", NULL);

    json_escape(board_hostname, esc_board_hostname, sizeof(esc_board_hostname));
    json_escape(board_model, esc_board_model, sizeof(esc_board_model));
    json_escape(board_compat, esc_board_compat, sizeof(esc_board_compat));

	APPEND_JSON_TO_BUF("\"board\":{");
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
    /* Family is the compiled hardware series; it is not a BOM probe. */
    board_family = fdt_getprop(gd->fdt_blob, 0, "h3c,board-family", NULL);
    json_escape(board_family, esc_board_family, sizeof(esc_board_family));
    APPEND_JSON_TO_BUF("\"family\":\"%s\",\"identity_source\":\"build_configuration\",",
        esc_board_family);
#endif
	APPEND_JSON_TO_BUF("\"hostname\":\"%s\",", esc_board_hostname);
	APPEND_JSON_TO_BUF("\"model\":\"%s\",", esc_board_model);
	APPEND_JSON_TO_BUF("\"compatible\":\"%s\",", esc_board_compat);
	APPEND_JSON_TO_BUF("\"machid\":\"0x%lx\",", (ulong)gd->bd->bi_arch_number);
	APPEND_JSON_TO_BUF("\"ram_size\":%lu", (ulong)gd->ram_size);
	APPEND_JSON_TO_BUF("},"); /* board */

    /* SMEM info */
	APPEND_JSON_TO_BUF("\"smeminfo\": {");

    APPEND_JSON_TO_BUF("\"flash_type\":\"%s\",", flash_type_to_string(sfi->flash_type));

    APPEND_JSON_TO_BUF("\"flash_block_size\":%lu,", (ulong)sfi->flash_block_size);

    APPEND_JSON_TO_BUF("\"flash_density\":%lu", (ulong)sfi->flash_density);

	APPEND_JSON_TO_BUF("},"); /* smeminfo */

	/* Flash devices info */
	APPEND_JSON_TO_BUF("\"devices\":{");

	/* SPI info */
	APPEND_JSON_TO_BUF("\"spi\":{");
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	{
		const struct spi_flash *spi = get_detected_spi_flash();
		char esc_spi_name[128];
		json_escape(spi ? spi->name : NULL, esc_spi_name, sizeof(esc_spi_name));
		APPEND_JSON_TO_BUF("\"present\":%s", spi ? "true" : "false");
		if (spi)
			APPEND_JSON_TO_BUF(",\"name\":\"%s\",\"size\":%lu,\"page_size\":%lu,\"sector_size\":%lu,\"erase_size\":%lu",
			    esc_spi_name, (ulong)spi->size, (ulong)spi->page_size,
			    (ulong)spi->sector_size, (ulong)spi->erase_size);
	}
#else
	APPEND_JSON_TO_BUF("\"present\":%s", has_spi() ? "true" : "false");
	if (has_spi()) {
		struct spi_flash *spi;
		spi = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
					CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		APPEND_JSON_TO_BUF(",\"name\":\"%s\",\"size\":%lu,\"page_size\":%lu,\"sector_size\":%lu,\"erase_size\":%lu",
		    spi->name, (ulong)spi->size, (ulong)spi->page_size,
		    (ulong)spi->sector_size, (ulong)spi->erase_size);
	}
#endif
	APPEND_JSON_TO_BUF("},"); /* devices: spi */

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	/* Configured probe location only; this is not an exhaustive wiring scan. */
	{
		const struct spi_flash *spi = get_detected_spi_flash();
		char esc_spi_name[128];
		json_escape(spi ? spi->name : NULL, esc_spi_name, sizeof(esc_spi_name));
		if (spi)
			APPEND_JSON_TO_BUF("\"spi_nor\":{\"present\":true,\"bus\":%u,\"chip_select\":%u,\"name\":\"%s\",\"model\":\"%s\",\"size\":%lu,\"page_size\":%lu,\"sector_size\":%lu,\"erase_size\":%lu},",
			    (unsigned int)CONFIG_SF_DEFAULT_BUS, (unsigned int)CONFIG_SF_DEFAULT_CS,
			    esc_spi_name, esc_spi_name, (ulong)spi->size, (ulong)spi->page_size,
			    (ulong)spi->sector_size, (ulong)spi->erase_size);
		else
			APPEND_JSON_TO_BUF("\"spi_nor\":{\"present\":false,\"bus\":%u,\"chip_select\":%u,\"name\":null,\"model\":null,\"size\":null,\"page_size\":null,\"sector_size\":null,\"erase_size\":null},",
			    (unsigned int)CONFIG_SF_DEFAULT_BUS, (unsigned int)CONFIG_SF_DEFAULT_CS);
	}
#endif

	/* MMC info */
	APPEND_JSON_TO_BUF("\"mmc\":{");
	APPEND_JSON_TO_BUF("\"present\":%s", has_mmc() ? "true" : "false");
	if (has_mmc()) {
        int major_ver, minor_ver, change_ver;

		mmc = find_mmc_device(mmc_host.dev_num);
		bd = mmc_get_dev(mmc_host.dev_num);

        major_ver = EXTRACT_SDMMC_MAJOR_VERSION(mmc->version);
        minor_ver = EXTRACT_SDMMC_MINOR_VERSION(mmc->version);
        change_ver = EXTRACT_SDMMC_CHANGE_VERSION(mmc->version);

		APPEND_JSON_TO_BUF(",\"vendor\":\"%s\",\"product\":\"%s\","
            "\"size\":%llu,\"block_size\":%lu,"
            "\"version\":\"%d.%d",
			bd->vendor, bd->product,
            (unsigned long long)mmc->capacity_user, bd->blksz,
            major_ver, minor_ver);

        if (change_ver)
            APPEND_JSON_TO_BUF(".%d\"", change_ver);
        else
            APPEND_JSON_TO_BUF("\"");

		if (!IS_SD(mmc) && mmc->version >= MMC_VERSION_4) {
			u8 life_a, life_b, pre_eol;
			if (!mmc_get_life_info(mmc, &life_a, &life_b, &pre_eol)) {
				APPEND_JSON_TO_BUF(",\"life_a\":\"0x%02x\",\"life_b\":\"0x%02x\",\"pre_eol\":\"0x%02x\"",
					life_a, life_b, pre_eol);
			}
		}
	}
	APPEND_JSON_TO_BUF("},"); /* devices: mmc */

#ifdef CONFIG_H3C_NAND_ECC_COMPAT
    APPEND_JSON_TO_BUF(
        "\"nand_ecc_compat\":{\"ready\":%s,\"env_ecc\":%u,"
        "\"system_ecc\":%u,\"spare\":%u},",
        h3c_nand_ecc_ready() ? "true" : "false",
        h3c_nand_ecc_env_mode(), h3c_nand_ecc_system_mode(),
        h3c_nand_ecc_spare());
#endif
	/* NAND info */
	APPEND_JSON_TO_BUF("\"nand\":{");
	APPEND_JSON_TO_BUF("\"present\":%s", has_nand() ? "true" : "false");
	if (has_nand()) {
		nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
		APPEND_JSON_TO_BUF(",\"name\":\"%s\",\"size\":%llu,"
            "\"page_size\":%lu,\"block_size\":%lu,\"oob_size\":%lu,"
            "\"oob_avail\":%lu,\"ecc_step_size\":%u,\"ecc_strength\":%u",
            nand->name, (unsigned long long)nand->size,
            (ulong)nand->writesize, (ulong)nand->erasesize, (ulong)nand->oobsize,
            (ulong)nand->oobavail, nand->ecc_step_size, nand->ecc_strength);
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
		{
			struct qpic_nand_model_info model;
			char esc_nand_model[128];
			if (!qpic_nand_get_model_info(&model)) {
				json_escape(model.name, esc_nand_model, sizeof(esc_nand_model));
				APPEND_JSON_TO_BUF(",\"model\":\"%s\",\"model_id\":\"%02x:%02x:%02x\"",
				    esc_nand_model, model.id[0], model.id[1], model.id[2]);
			} else {
				APPEND_JSON_TO_BUF(",\"model\":null,\"model_id\":null");
			}
		}
#endif
	}
	APPEND_JSON_TO_BUF("}"); /* devices: nand */
	APPEND_JSON_TO_BUF("},"); /* devices */


	/* Partitions */
	APPEND_JSON_TO_BUF("\"partitions\":{");

	/* SMEM partitions */
	spt = get_smem_ptable_addr();

	APPEND_JSON_TO_BUF("\"smem\":{");
	APPEND_JSON_TO_BUF("\"present\":%s,", spt->len ? "true" : "false");
	APPEND_JSON_TO_BUF("\"parts\":[");

	for (int i = 0; i < spt->len; i++) {
		const struct smem_ptn *p = &spt->parts[i];
		uint32_t offset_in_bytes = 0, size_in_bytes = 0;

		getpart_offset_size((char *)p->name, &offset_in_bytes, &size_in_bytes);

		APPEND_JSON_TO_BUF("%s{\"name\":\"%s\",\"start\":%lu,\"size\":%lu}",
			i ? "," : "", p->name, (ulong)offset_in_bytes, (ulong)size_in_bytes);
	}

	APPEND_JSON_TO_BUF("]"); /* partitions: smem: parts */
	APPEND_JSON_TO_BUF("},"); /* partitions: smem */

	/* MMC partitions */
	APPEND_JSON_TO_BUF("\"mmc\":{");
	APPEND_JSON_TO_BUF("\"present\":%s,", has_mmc() ? "true" : "false");
	APPEND_JSON_TO_BUF("\"parts\":[");

	if (has_mmc()) {
		disk_partition_t dpart = {0};
		int idx = 1;

		APPEND_JSON_TO_BUF("{\"name\":\"0:GPT\",\"start\":0,\"size\":%lu}",
			GPT_SIZE_IN_BLOCKS * bd->blksz);

		while (len < left - 128) {
			if (get_partition_info_efi(bd, idx, &dpart))
				break;

			if (!dpart.name[0]) {
				idx++;
				continue;
			}

			APPEND_JSON_TO_BUF(",{\"name\":\"%s\",\"start\":%llu,\"size\":%llu}",
				dpart.name, (unsigned long long)dpart.start * dpart.blksz,
                (unsigned long long)dpart.size * dpart.blksz);

			idx++;
		}
	}

	APPEND_JSON_TO_BUF("]"); /* partitions: mmc: parts */
	APPEND_JSON_TO_BUF("}"); /* partitions: mmc */
	APPEND_JSON_TO_BUF("}"); /* partitions */

	APPEND_JSON_TO_BUF("}");

	handle_response_message(response, 200, buf, "application/json");
	response->session_data = buf;
}

void sysinfo_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/sysinfo", &sysinfo_handler, NULL);
}
