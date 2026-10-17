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
#include <spi.h>
#include <spi_flash.h>
#include <nand.h>
#include <linux/sizes.h>
#include <asm/arch-qca-common/smem.h>
#include <net/httpd.h>
#include <failsafe/failsafe.h>
#include <failsafe/fw_dec.h>
#include <ipq_api.h>
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
#include <rt3000_b_layout.h>
#endif

#include "modules.h"

#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
#define H3C_MIBIB_NAND_BYTES 0x80000U
#define H3C_SMEM_TABLE_BYTES (16U + 32U * (16U + 3U * sizeof(u32)))

struct h3c_mibib_smem_part {
	char name[16];
	u32 start;
	u32 size;
	u32 attr;
} __attribute__((packed));

struct h3c_mibib_smem_table {
	u32 magic[2];
	u32 version;
	u32 len;
	struct h3c_mibib_smem_part parts[32];
} __attribute__((packed));

/* Keep the live logical-name -> physical-extent mapping when an OEM raw MIBIB
 * uses the opposite aliases from the current BOOTCONFIG-selected SMEM view.
 */
static int h3c_mibib_preserve_live_aliases(void *candidate, const void *live,
					   size_t table_bytes)
{
	struct h3c_mibib_smem_table *dst = candidate;
	const struct h3c_mibib_smem_table *src = live;
	unsigned int i, j;

	if (!candidate || !live || table_bytes < 16 ||
	    dst->len == 0 || dst->len > 32 || dst->len != src->len ||
	    table_bytes < 16U + dst->len * sizeof(dst->parts[0]))
		return -EINVAL;
	for (i = 0; i < dst->len; i++) {
		if (!memchr(dst->parts[i].name, 0, sizeof(dst->parts[i].name)))
			return -EINVAL;
		for (j = 0; j < src->len; j++) {
			if (!memchr(src->parts[j].name, 0, sizeof(src->parts[j].name)))
				return -EINVAL;
			if (!strncmp(dst->parts[i].name, src->parts[j].name,
				     sizeof(dst->parts[i].name)))
				break;
		}
		if (j == src->len || dst->parts[i].size != src->parts[j].size)
			return -EINVAL;
		dst->parts[i].start = src->parts[j].start;
	}
	return 0;
}

static int h3c_mibib_aliases_equal(const void *left, const void *right,
				   size_t table_bytes)
{
	const struct h3c_mibib_smem_table *a = left, *b = right;
	unsigned int i, j;

	if (!left || !right || table_bytes < 16 || !a->len ||
	    a->len > 32 || a->len != b->len ||
	    table_bytes < 16U + a->len * sizeof(a->parts[0]))
		return 0;
	for (i = 0; i < a->len; i++) {
		if (!memchr(a->parts[i].name, 0, sizeof(a->parts[i].name)))
			return 0;
		for (j = 0; j < b->len; j++) {
			if (!memchr(b->parts[j].name, 0, sizeof(b->parts[j].name)))
				return 0;
			if (!strncmp(a->parts[i].name, b->parts[j].name,
				     sizeof(a->parts[i].name)))
				break;
		}
		if (j == b->len || a->parts[i].start != b->parts[j].start ||
		    a->parts[i].size != b->parts[j].size)
			return 0;
	}
	return 1;
}

/* AUTO reload is volatile only; accept the board allowlist and unchanged physical rootfs mode/extents. */
static int h3c_reload_mibib_ram_only(const void *image, size_t image_size)
{
	const void *mibib_header = NULL, *ptable = NULL, *live;
	struct rt3_layout current, candidate, installed;
	unsigned char staged[H3C_SMEM_TABLE_BYTES] __aligned(4);
	unsigned char saved[H3C_SMEM_TABLE_BYTES] __aligned(4);
	uintptr_t base, table_addr;
	u32 raw_len;
	size_t table_bytes;
	int ret;

	if (!image || image_size != H3C_MIBIB_NAND_BYTES ||
	    check_fw_type((uintptr_t)image, image_size) != FW_TYPE_MIBIB_NAND)
		return -EINVAL;
	ret = get_mibib_and_ptable_addr(image, image_size, &mibib_header, &ptable, MIBIB_TYPE_NAND);
	if (ret || mibib_header != image || ptable != (const unsigned char *)image + 0x800)
		return -EINVAL;
	base = (uintptr_t)image;
	table_addr = (uintptr_t)ptable;
	if (table_addr < base || table_addr - base > image_size - sizeof(staged))
		return -EINVAL;
	ret = rt3_layout_read(&current);
	if (ret)
		return ret;
	ret = rt3_layout_resolve_raw(ptable, sizeof(staged), &candidate);
	if (ret || !rt3_layout_same_physical_mode(&current, &candidate))
		return ret ? ret : -EPERM;
	memcpy(&raw_len, (const unsigned char *)ptable + 12, sizeof(raw_len));
	if (!raw_len || raw_len > 32)
		return -EINVAL;
	table_bytes = 16U + raw_len * 28U;
	live = get_smem_ptable_addr();
	if (!live)
		return -ENODEV;
	memcpy(saved, live, sizeof(saved));
	memset(staged, 0, sizeof(staged));
	memcpy(staged, ptable, table_bytes);
	ret = h3c_mibib_preserve_live_aliases(staged, live, table_bytes);
	if (ret)
		return ret;
	ret = rt3_layout_resolve_raw(staged, sizeof(staged), &candidate);
	if (ret || !rt3_layout_same_physical_mode(&current, &candidate))
		return ret ? ret : -EPERM;
	ret = mibib_ptable_init((unsigned int *)staged);
	if (ret) {
		(void)mibib_ptable_init((unsigned int *)saved);
		rt3_layout_invalidate_cache();
		return ret;
	}
	rt3_layout_invalidate_cache();
	live = get_smem_ptable_addr();
	ret = rt3_layout_read(&installed);
	if (ret || !live || !h3c_mibib_aliases_equal(staged, live, table_bytes) ||
	    !rt3_layout_same_physical_mode(&current, &installed)) {
		(void)mibib_ptable_init((unsigned int *)saved);
		rt3_layout_invalidate_cache();
		return ret ? ret : -EPERM;
	}
	return 0;
}
#endif

static void mibib_reload_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
    static char resp[256];
    qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
    struct spi_flash *spi;
    nand_info_t *nand;
    struct httpd_form_value *mibib;
    const void *ptable_addr;
    fw_type_t fw_type;
    int ret;

    if (status != HTTP_CB_NEW)
        return;

    response->status = HTTP_RESP_STD;
    response->info.code = 400;
    response->info.connection_close = 1;
    response->info.content_type = "application/json";
    if (!request || request->method != HTTP_POST ||
        request->form.count != 1 ||
        !request->form.values[0].name ||
        strcmp(request->form.values[0].name, "mibib") ||
        !request->form.values[0].data) {
        response->data = "{\"status\":\"fail\",\"info\":{\"type\":\"invalid_request\"}}";
        response->size = strlen(response->data);
        return;
    }
    mibib = &request->form.values[0];

    if (!is_9008_mode()) {
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
        ret = h3c_reload_mibib_ram_only(mibib->data, mibib->size);
        if (ret) {
            handle_fail_led_state();
            response->data = "{\"status\":\"fail\","
                            "\"info\":{\"type\":\"unsafe_or_incompatible_mibib\"}}";
            response->size = strlen(response->data);
            return;
        }
        handle_success_led_state();
        get_kernel_fs_part_details();
        response->info.code = 200;
        response->data = "{\"status\":\"success\","
                        "\"info\":{\"ram_only\":true,\"persistent\":false}}";
        response->size = strlen(response->data);
        return;
#else
        handle_fail_led_state();
        response->data = "{\"status\":\"fail\","
                        "\"info\":{\"type\":\"not_in_9008_mode\"}}";
        response->size = strlen(response->data);
        return;
#endif
    }

    fw_type = check_fw_type((uintptr_t)mibib->data, mibib->size);
    switch (fw_type) {
    case FW_TYPE_MIBIB_NAND:
        if (!has_nand())
			goto flash_not_found;
        nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
#ifdef CONFIG_QPIC_SERIAL
		sfi->flash_type = SMEM_BOOT_QSPI_NAND_FLASH;
#else
		sfi->flash_type = SMEM_BOOT_NAND_FLASH;
#endif
        sfi->flash_block_size = nand->erasesize;
        sfi->flash_density = nand->size;
        break;
    case FW_TYPE_MIBIB_NOR:
        if (!has_spi())
			goto flash_not_found;
        spi = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
                    CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
        sfi->flash_type = SMEM_BOOT_SPI_FLASH;
        sfi->flash_block_size = SZ_64K;
        sfi->flash_density = spi->size;
        break;
    default:
        handle_fail_led_state();
        snprintf(resp, sizeof(resp),
            "{\"status\":\"fail\",\"info\":{\"type\":\"wrong_file_type\","
            "\"expected\":\"MIBIB\",\"actual\":\"%s\"}}", fw_type_to_string(fw_type));
        response->data = resp;
        response->size = strlen(response->data);
        return;
    }

    ret = get_mibib_and_ptable_addr(mibib->data, mibib->size, NULL, &ptable_addr,
            (fw_type == FW_TYPE_MIBIB_NOR) ? MIBIB_TYPE_NOR : MIBIB_TYPE_NAND);
    if (ret) {
        handle_fail_led_state();
        response->data = "{\"status\":\"fail\","
                        "\"info\":{\"type\":\"invalid_mibib_file\"}}";
        response->size = strlen(response->data);
        return;
    }

    ret = mibib_ptable_init((unsigned int *)ptable_addr);
    if (ret) {
        handle_fail_led_state();
        response->data = "{\"status\":\"fail\","
                        "\"info\":{\"type\":\"invalid_table_magic\"}}";
        response->size = strlen(response->data);
        return;
    }

    handle_success_led_state();
    get_kernel_fs_part_details();

    response->info.code = 200;
    response->data = "{\"status\":\"success\"}";
    response->size = strlen(response->data);

    return;

flash_not_found:
    handle_fail_led_state();
	snprintf(resp, sizeof(resp),
		"{\"status\":\"fail\",\"info\":{\"type\":\"flash_not_found\","
        "\"filetype\":\"%s\",\"flashtype\":\"%s\"}}",
		fw_type_to_string(fw_type),
        (fw_type == FW_TYPE_MIBIB_NOR) ? "SPI-NOR" : "NAND");
    response->data = resp;
    response->size = strlen(response->data);
    return;
}

void mibib_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/mibib/reload", &mibib_reload_handler, NULL);
}
