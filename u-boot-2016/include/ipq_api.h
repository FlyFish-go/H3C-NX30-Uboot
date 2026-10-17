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

#ifndef __IPQ_API__
#define __IPQ_API__

#include <ipq_led.h>

#define SZ_KIB(n) ((n) << 10)
#define SZ_MIB(n) ((n) << 20)

/* loadaddr 环境变量的默认值 (u-boot-2016/include/env_default.h) */
#if defined(CONFIG_IPQ40XX)
#define CONFIG_LOADADDR 0x84000000
#else
#define CONFIG_LOADADDR 0x44000000
#endif

#define CONFIG_TFTP_TSIZE
#if defined(CONFIG_TFTP_TSIZE)
/* tftpboot 和 tftpput 显示数字百分比进度（依赖于 CONFIG_TFTP_TSIZE） */
#define CONFIG_TFTP_DIGITAL_PROGRESS
#endif

#define NO_FLASH_STR         "no"
#define NOR_FLASH_STR        "nor"
#define NAND_FLASH_STR       "nand"
#define ONENAND_FLASH_STR    "onenand"
#define SDC_FLASH_STR        "sdc"
#define MMC_FLASH_STR        "mmc"
#define SPI_FLASH_STR        "spi_nor"
#define NORPLUSNAND_STR      "nor_plus_nand"
#define NORPLUSEMMC_STR      "nor_plus_emmc"
#define QSPI_NAND_FLASH_STR  "qspi_nand"
#define UNKNOWN_FLASH_STR    "unknown"

typedef enum {
    MIBIB_TYPE_NAND,
    MIBIB_TYPE_NOR
} mibib_type_t;

void do_network_check(void);
void do_httpd_check(void);
void ipq_gpio_init(void);
size_t json_escape(const char *input, char *output, size_t output_buffer_size);
bool mmc_part_exists(const char *part_name);
int get_mibib_and_ptable_addr(const void *addr, size_t limit,
		const void **mibib_addr, const void **ptable_addr, mibib_type_t mibib_type);
void reload_mibib_from_flash_and_set_default_flash_type_in_9008_mode(void);

bool is_load_addr_valid(uintptr_t load_addr);
bool is_memory_region_available(uintptr_t load_addr, size_t size);

void detect_flash_device(void);
struct spi_flash;
const struct spi_flash *get_detected_spi_flash(void);
bool has_spi(void);
bool has_nand(void);
bool has_mmc(void);

const char *flash_type_to_string(uint32_t flash_type);
int string_to_flash_type(const char *str);

int print_board_info(void);
void set_file_info_env(ulong fileaddr, ulong filesize);
void print_progress_bar(ulong progress, ulong interval, const char *end_str);
bool get_enable_state(const char *env_key, bool enable_by_default);

#if defined(CONFIG_ARCH_IPQ5332) || defined(CONFIG_ARCH_IPQ9574)
#define PPE_TIMEOUT  60000UL /* 60000 毫秒 = 60 秒 */
extern ulong ppe_last_pkt_rcvd_or_sent;
extern void arp_probe(void);

static inline void ppe_kick_start(void)
{
	arp_probe();
	ppe_last_pkt_rcvd_or_sent = get_timer(0);
}

static inline void ppe_keep_alive(void)
{
	if (get_timer(0) - ppe_last_pkt_rcvd_or_sent > PPE_TIMEOUT)
		ppe_kick_start();
}

static inline void ppe_reset_timeout(void)
{
	ppe_last_pkt_rcvd_or_sent = get_timer(0);
}
#endif /* CONFIG_ARCH_IPQ5332 || CONFIG_ARCH_IPQ9574 */

#endif /* __IPQ_API__ */
