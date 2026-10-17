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
#include <command.h>
#include <console.h>
#include <malloc.h>
#include <version.h>
#include <net/tcp.h>
#include <net/httpd.h>
#include <failsafe/failsafe.h>
#include <ipq_api.h>

#include "../fs.h"
#include "modules.h"

struct reboot_session {
	int dummy;
};

static int gunzip_and_send(struct httpd_response *response,
		const struct fs_desc *file, const char *filename)
{
	void *dst = NULL;
	ulong len = file->uncompressed_size;

	httpd_debug("gunzipping %s (%u -> %u bytes)\n",
		filename, file->size, file->uncompressed_size);

#if defined(CONFIG_GZIP)
	dst = malloc(file->uncompressed_size);
	if (!dst) {
		printf("Error: Failed to allocate %u bytes for gunzip of %s\n",
			   file->uncompressed_size, filename);
        return -1;
	}

	if (gunzip(dst, file->uncompressed_size, (unsigned char *)file->data, &len) != 0) {
		printf("Error: Failed to gunzip %s\n", filename);
		printf("  - gzip_data: %p\n", file->data);
		printf("  - gzip_size: %u\n", file->size);
        printf("  - uncompressed_size: %u\n", file->uncompressed_size);
		free(dst);
		return -1;
	}
#else
	printf("Error: Failed to gunzip %s because the gunzip module is not enabled\n", filename);
	return -1;
#endif

	if (len != file->uncompressed_size)
        printf("Warning: %s uncompressed size mismatch (expect: %u bytes, in fact: %lu bytes)\n",
               filename, file->uncompressed_size, len);

	response->data = dst;
	response->size = len;
	response->info.content_encoding = NULL;
	response->info.content_length = len;
	response->gunzip_buffer = dst;  /* 使用gunzip_buffer指针存储需要释放的内存地址 */

	return 0;
}

static int output_plain_file(struct httpd_response *response, const char *filename)
{
	const struct fs_desc *file;

	response->status = HTTP_RESP_STD;
	response->info.connection_close = 1;
	response->gunzip_buffer = NULL;

	file = fs_find_file(filename);

	/* 找不到文件 */
	if (!file) {
		response->data = "Error: file not found";
		response->size = strlen(response->data);
		response->info.code = 404;
		response->info.content_type = "text/html";
		response->info.content_encoding = NULL;

		return 1;
	}

	response->info.code = 200;
	response->info.content_type = file->content_type;

	/* 文件本身就是未压缩的，直接发送 */
	if (!file->is_gzip) {
		response->data = file->data;
		response->size = file->size;
		response->info.content_encoding = NULL;
		response->info.content_length = file->size;
		return 0;
	}

	/* 检查客户端是否支持gzip */
	int client_accepts_gzip = 0;
	const char *accept_encoding = httpd_find_header("Accept-Encoding");

	if (accept_encoding && strstr(accept_encoding, "gzip"))
		client_accepts_gzip = 1;

	/* 客户端支持gzip，直接发送压缩数据 */
	if (client_accepts_gzip) {
		response->data = file->data;
		response->size = file->size;
		response->info.content_encoding = "gzip";
		response->info.content_length = file->size;
		response->info.vary = "Accept-Encoding";

		return 0;
	}

	/* 客户端不支持gzip，需要解压后发送 */
	if (gunzip_and_send(response, file, filename) != 0) {
		/* 解压失败，回退到发送压缩版本？或者返回错误 */
		/* 这里选择返回错误页面 */
		response->data = "Error: Failed to decompress file";
		response->size = strlen(response->data);
		response->info.content_encoding = NULL;
		response->info.content_length = response->size;
		response->info.code = 500;
		response->info.content_type = "text/html";

		return 1;
	}

	return 0;
}

static void not_found_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		output_plain_file(response, "404.html");
		response->info.code = 404;
	}
}

static void index_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
#if defined(CONFIG_H3C_HW_D_NAND_SERVICE)
		output_plain_file(response, "index.html");
#elif defined(CONFIG_H3C_HW_D_NAND_READONLY) && !defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
		output_plain_file(response, "index_hw_d_nand_readonly.html");
#elif defined(CONFIG_H3C_HW_D_RAM_WEB) && !defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
		output_plain_file(response, "index_hw_d_ram_readonly.html");
#else
		output_plain_file(response, "index.html");
#endif
	}
}

static void version_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;

	response->data = version_string;
	response->size = strlen(response->data);

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

#if !defined(CONFIG_H3C_HW_D_RAM_WEB) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
static void reboot_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	struct reboot_session *st;

	if (status == HTTP_CB_NEW) {
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		response->session_data = st;
		response->status = HTTP_RESP_STD;
		response->data = "rebooting";
		response->size = strlen(response->data);
		response->info.code = 200;
		response->info.connection_close = 1;
		response->info.content_type = "text/plain";
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		st = response->session_data;
		free(st);

		/* Make sure the current HTTP session has fully closed before reset */
		tcp_close_all_conn();
		do_reset(NULL, 0, 0, NULL);
	}
}

#endif /* !CONFIG_H3C_HW_D_RAM_WEB */

static void style_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "style.css");
}

static void js_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	if (status == HTTP_CB_NEW)
		output_plain_file(response, "main.js");
}

void html_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	if (output_plain_file(response, request->urih->uri + 1))
		not_found_handler(status, request, response);
}

/**
 * 负责处理上传类 HTML 页面内容的生成。包括以下页面：
 * art.html, cdt.html, firmware.html, initramfs.html,
 * mibib.html, ptable.html, simg.html, uboot.html
 */
void upload_html_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
	static char resp[512];
	char page_name[32];
	int cpy_len;

	if (status != HTTP_CB_NEW)
		return;

	/* 5 == strlen(".html") */
	cpy_len = strlen(request->urih->uri + 1) - 5;

	/* page_name 只包含去掉 / 和 .html 后的内容 */
	memcpy(page_name, request->urih->uri + 1, cpy_len);

	page_name[cpy_len] = '\0';

	snprintf(resp, sizeof(resp),
		"<!DOCTYPE HTML>"
		"<html>"
		"<head>"
		"	<meta charset='utf-8'>"
		"	<meta name='viewport' content='width=device-width, initial-scale=1'>"
		"	<title></title>"
		"	<link rel='stylesheet' href='/style.css'>"
		"	<script src='/main.js'></script>"
		"</head>"
		"<body onload='appInit(\"%s\")' data-page='%s'></body>"
		"</html>", page_name, page_name);

	response->status = HTTP_RESP_STD;
	response->data = resp;
	response->size = strlen(response->data);
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/html";
}

void index_register_uri_handlers(struct httpd_instance *inst)
{
    httpd_register_uri_handler(inst, "/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/cgi-bin/luci", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/cgi-bin/luci/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/index.html", &index_handler, NULL);
}

void html_register_uri_handlers(struct httpd_instance *inst)
{
    httpd_register_uri_handler(inst, "/firmware.html", &upload_html_handler, NULL);
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
    httpd_register_uri_handler(inst, "/firmware-dual.html", &upload_html_handler, NULL);
    httpd_register_uri_handler(inst, "/firmware-single.html", &upload_html_handler, NULL);
#endif
	httpd_register_uri_handler(inst, "/art.html", &upload_html_handler, NULL);
	httpd_register_uri_handler(inst, "/cdt.html", &upload_html_handler, NULL);
	httpd_register_uri_handler(inst, "/initramfs.html", &upload_html_handler, NULL);
	httpd_register_uri_handler(inst, "/ptable.html", &upload_html_handler, NULL);
	httpd_register_uri_handler(inst, "/simg.html", &upload_html_handler, NULL);
	httpd_register_uri_handler(inst, "/uboot.html", &upload_html_handler, NULL);

	httpd_register_uri_handler(inst, "/backup.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/env.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/mac.html", &html_handler, NULL);
    httpd_register_uri_handler(inst, "/mibib.html", &upload_html_handler, NULL);
	httpd_register_uri_handler(inst, "/network.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/syslog.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/webterm.html", &html_handler, NULL);

	httpd_register_uri_handler(inst, "/reboot.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/settings.html", &html_handler, NULL);
}

void misc_register_uri_handlers(struct httpd_instance *inst)
{
    httpd_register_uri_handler(inst, "/main.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/style.css", &style_handler, NULL);
	httpd_register_uri_handler(inst, "/version", &version_handler, NULL);
#if !defined(CONFIG_H3C_HW_D_RAM_WEB) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
	httpd_register_uri_handler(inst, "/reboot", &reboot_handler, NULL);
	httpd_register_uri_handler(inst, "", &not_found_handler, NULL);
#endif
}
