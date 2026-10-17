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

#ifndef __FAILSAFE_H__
#define __FAILSAFE_H__

#include <ipq_led.h>
#include <net/httpd.h>
#include <rt3000_b_metadata.h>

#if defined(CONFIG_HTTPD_DEBUG)
extern bool httpd_debug_on;
#define httpd_debug(fmt, args...)   \
    do {    \
        if (httpd_debug_on)  \
            printf("%s: " fmt, __func__, ##args);    \
    } while (0)
#else
#define httpd_debug(fmt, args...)   do { } while (0)
#endif

typedef enum {
    WEBFAILSAFE_UPGRADE_TYPE_UNKNOWN = -1,
    WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE,
    WEBFAILSAFE_UPGRADE_TYPE_UBOOT,
    WEBFAILSAFE_UPGRADE_TYPE_ART,
    WEBFAILSAFE_UPGRADE_TYPE_CDT,
    WEBFAILSAFE_UPGRADE_TYPE_PTABLE,
    WEBFAILSAFE_UPGRADE_TYPE_SIMG,
    WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS,
} upgrade_type_t;

enum {
    RET_FAILURE = -1,
    RET_SUCCESS = 0,
    RET_FILE_TOO_BIG,
    RET_FLASH_NOT_FOUND,
    RET_PART_NOT_FOUND,
    RET_COMMAND_TOO_LONG,
    RET_TOO_MANY_COMMANDS,
    RET_UPLOAD_ID_MISMATCH,
    RET_WRONG_FW_TYPE,
    RET_WRONG_UPGRADE_TYPE,
};

static inline void handle_start_led_state(void)
{
	led_all_off();
	led_on("blink_led");
}

static inline void handle_fail_led_state(void)
{
    led_all_off();
	led_on("power_led");
}

static inline void handle_success_led_state(void)
{
	led_all_off();
	led_on("system_led");
}

bool httpd_is_running(void);
void print_eth_init_halt_skip_hint(int init);
int boot_from_mem(ulong data_addr);
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
void failsafe_initramfs_invalidate(void);
bool failsafe_initramfs_is_validated(ulong data_addr, ulong data_size);
#endif
int failsafe_validate_image(upgrade_type_t upgrade_type, const char *filename,
        enum rt3_md_target metadata_target, const void *data_addr, ulong data_size,
        struct httpd_response *response);
int failsafe_write_image(upgrade_type_t upgrade_type, ulong data_addr,
        ulong data_size, struct httpd_response *response);

#endif /* __FAILSAFE_H__ */
