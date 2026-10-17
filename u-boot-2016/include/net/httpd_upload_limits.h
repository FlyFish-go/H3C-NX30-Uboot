/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NET_HTTPD_UPLOAD_LIMITS_H__
#define __NET_HTTPD_UPLOAD_LIMITS_H__
#include <linux/sizes.h>
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
#include <rt3000_b_initramfs.h>
#endif
#define RT3_HTTP_UPLOAD_LIMIT (80U * SZ_1M + SZ_64K)
#define RT3_HTTP_METADATA_UPLOAD_LIMIT (0x00140000U + SZ_64K)
#define RT3_HTTP_RECOVERY_UPLOAD_LIMIT (16U * SZ_1M + SZ_64K)
#define NX30_NOR_HTTP_UPLOAD_LIMIT (0x000a0000U + 128U + SZ_64K)
#define NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT (0x02800000U + SZ_64K)
#define NX30_D_SERVICE_UBOOT_UPLOAD_LIMIT (0x00140000U + SZ_64K)
static inline int httpd_upload_uri_is_standard_upload(const char *uri)
{
    return uri && uri[0] == '/' && uri[1] == 'u' && uri[2] == 'p' &&
           uri[3] == 'l' && uri[4] == 'o' && uri[5] == 'a' &&
           uri[6] == 'd' && uri[7] == '\0';
}
static inline int httpd_upload_length_allowed(const char *uri, unsigned long length)
{
#if defined(CONFIG_H3C_HW_D_NAND_SERVICE) && !defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
    if (!uri) return 0;
    if (!strcmp(uri, "/service/firmware/validate") ||
        !strcmp(uri, "/service/firmware/write"))
        return length <= NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT;
    if (!strcmp(uri, "/service/uboot/validate") ||
        !strcmp(uri, "/service/uboot/write"))
        return length <= NX30_D_SERVICE_UBOOT_UPLOAD_LIMIT;
    if (!strcmp(uri, "/service/env/prepare")) return length <= 2048U;
    if (!strcmp(uri, "/service/env/commit")) return length <= 2048U;
    if (!strcmp(uri, "/service/env/abort")) return length <= 2048U;
    if (!strcmp(uri, "/service/network/ram")) return length <= 2048U;
    if (!strcmp(uri, "/service/backup")) return length <= 2048U;
    if (!strcmp(uri, "/service/console")) return length <= 2048U;
    if (!strcmp(uri, "/service/led")) return length <= 2048U;
    if (!strcmp(uri, "/service/boot/slot")) return length <= 2048U;
    if (!strcmp(uri, "/service/boot/default/prepare")) return length <= 2048U;
    if (!strcmp(uri, "/service/boot/default/commit")) return length <= 2048U;
    if (!strcmp(uri, "/service/boot/default/abort")) return length <= 2048U;
    if (!strcmp(uri, "/service/reboot")) return length <= 2048U;
    return 0;
#elif defined(CONFIG_H3C_HW_D_NOR_RESCUE)
    if (!uri || strncmp(uri, "/nor/", 5)) return 0;
    if (!strcmp(uri, "/nor/validate")) return length <= NX30_NOR_HTTP_UPLOAD_LIMIT;
    if (!strcmp(uri, "/nor/write")) return length <= 512U;
    return 0;
#elif defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
    return rt3_hw_b_http_content_length_allowed(length, httpd_upload_uri_is_standard_upload(uri));
#else
    if (length > RT3_HTTP_UPLOAD_LIMIT) return 0;
    if (!httpd_upload_uri_is_standard_upload(uri)) return 1;
#if defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
    return length <= RT3_HTTP_RECOVERY_UPLOAD_LIMIT;
#else
    return length <= RT3_HTTP_METADATA_UPLOAD_LIMIT;
#endif
#endif
}
#endif
