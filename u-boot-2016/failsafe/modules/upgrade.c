/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Failsafe upgrade module
 */

/*
 * Modified by: chenxin527
 */

#include <common.h>
#include <asm/errno.h>
#include <malloc.h>
#include <net/tcp.h>
#include <net/httpd.h>
#include <failsafe/failsafe.h>
#include <ipq_api.h>
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
#include <rt3000_b_metadata.h>
#endif

#include "modules.h"

struct upload_session {
	char header_buf[4096];
	int ret;
    int body_sent;
};

struct result_session {
	char buf[4096];
	int ret;
	int body_sent;
	bool auto_reboot;
};

#ifndef CONFIG_H3C_HW_D_NOR_RESCUE
static const struct {
	upgrade_type_t type;
	const char *label;
} upgrade_types[] = {
	{ WEBFAILSAFE_UPGRADE_TYPE_FIRMWARE, "firmware" },
	{ WEBFAILSAFE_UPGRADE_TYPE_UBOOT, "uboot" },
	{ WEBFAILSAFE_UPGRADE_TYPE_ART, "art" },
	{ WEBFAILSAFE_UPGRADE_TYPE_CDT, "cdt" },
	{ WEBFAILSAFE_UPGRADE_TYPE_PTABLE, "ptable" },
	{ WEBFAILSAFE_UPGRADE_TYPE_SIMG, "simg" },
	{ WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS, "initramfs" }
};
#endif

bool auto_action_pending;
const void *upload_data;
upgrade_type_t upgrade_type;

#ifndef CONFIG_H3C_HW_D_NOR_RESCUE
static u32 fs_upload_id;
static u32 upload_data_id;
static size_t upload_size;
#endif

#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
static int metadata_kind_for_upgrade(upgrade_type_t type, enum rt3_md_kind *kind)
{
	if (!kind) return -EINVAL;
	switch (type) {
	case WEBFAILSAFE_UPGRADE_TYPE_UBOOT: *kind=RT3_MD_UBOOT; return 1;
	case WEBFAILSAFE_UPGRADE_TYPE_ART: *kind=RT3_MD_ART; return 1;
	case WEBFAILSAFE_UPGRADE_TYPE_CDT: *kind=RT3_MD_CDT; return 1;
	case WEBFAILSAFE_UPGRADE_TYPE_PTABLE: *kind=RT3_MD_MIBIB; return 1;
	default: return 0;
	}
}
static int metadata_commit_token(struct httpd_request *request, char out[65])
{
	struct httpd_form_value *value=NULL;
	unsigned int i,count=0;
	if (!request||!out) return -EINVAL;
	for(i=0;i<request->form.count;i++) {
		if(request->form.values[i].name&&!strcmp(request->form.values[i].name,"commit_token")) {
			value=&request->form.values[i];count++;
		}
	}
	if(count!=1||!value||!value->data||value->size!=64||
	   memchr(value->data,0,value->size)) return -EINVAL;
	for(i=0;i<value->size;i++) {
		char c=value->data[i];
		if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return -EINVAL;
		out[i]=c;
	}
	out[64]=0;return 0;
}
static int metadata_upload_target(struct httpd_request *request,
				 upgrade_type_t type,
				 enum rt3_md_target *target,
				 int *explicit_target)
{
	struct httpd_form_value *value = NULL;
	unsigned int i, count = 0;
	if (!request || !target || !explicit_target) return -EINVAL;
	for (i = 0; i < request->form.count; i++) {
		if (request->form.values[i].name &&
		    !strcmp(request->form.values[i].name, "cdt_target")) {
			value = &request->form.values[i];
			count++;
		}
	}
	if (count > 1) return -EINVAL;
	*target = RT3_MD_TARGET_DEFAULT;
	*explicit_target = 0;
	if (!count) {
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
		return 0;
#else
		return type == WEBFAILSAFE_UPGRADE_TYPE_CDT ? -EINVAL : 0;
#endif
	}
	if (type != WEBFAILSAFE_UPGRADE_TYPE_CDT || !value || !value->data ||
	    rt3_metadata_target_parse(value->data, value->size, target)) return -EINVAL;
	*explicit_target = 1;
	return 0;
}
#endif

#ifndef CONFIG_H3C_HW_D_NOR_RESCUE
static void upload_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	struct httpd_form_value *form_value;
	struct upload_session *sess;
	enum rt3_md_target metadata_target = RT3_MD_TARGET_DEFAULT;
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
	int has_cdt_target;
#endif
	int idx;

	if (status == HTTP_CB_NEW) {
		sess = calloc(1, sizeof(*sess));
		if (!sess) {
			response->info.code = 500;
			return;
		}

		handle_start_led_state();
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
		rt3_metadata_invalidate();
#endif
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
		failsafe_initramfs_invalidate();
#endif

		response->session_data = sess;

		response->status = HTTP_RESP_CUSTOM;

		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->info.code = 200;

		response->size = http_make_response_header(&response->info,
							sess->header_buf, sizeof(sess->header_buf));

		response->data = sess->header_buf;

		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		sess = response->session_data;

		if (sess->body_sent) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		/* new upload session identifier */
		fs_upload_id = rand();

		for (idx = 0; idx < ARRAY_SIZE(upgrade_types); idx++) {
			form_value = httpd_request_find_value(request, upgrade_types[idx].label);
			if (form_value) {
				upgrade_type = upgrade_types[idx].type;
				break;
			}
		}

#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
		if (metadata_upload_target(request, upgrade_type, &metadata_target,
					   &has_cdt_target) ||
		    request->form.count != (u32)(1 + has_cdt_target)) {
			rt3_metadata_invalidate();
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
			failsafe_initramfs_invalidate();
#endif
			response->data = "{\"status\":\"fail\",\"info\":{\"type\":\"invalid_upload_fields\"}}";
			response->size = strlen(response->data);
			sess->body_sent = 1;
			return;
		}
#endif

		if (idx == ARRAY_SIZE(upgrade_types)) {
			puts("NO supported upgrade type found!\n");

			/* 没有匹配的 upgrade_type，返回 fail*/
			response->data = "{\"status\":\"fail\","
							"\"info\":{\"type\":\"wrong_upgrade_type\"}}";
			response->size = strlen(response->data);
			sess->body_sent = 1;

			httpd_debug("response message: %s\n", response->data);

			return;
		}

		upload_data_id = fs_upload_id;
		upload_data = form_value->data;
		upload_size = form_value->size;

		httpd_debug("upload_data = 0x%lx, upload_size = %lu (0x%lx)\n",
			(ulong)upload_data, (ulong)upload_size, (ulong)upload_size);

		sess->ret = failsafe_validate_image(upgrade_type, form_value->filename,
						metadata_target, upload_data,
						(ulong)upload_size, response);

		sess->body_sent = 1;

		httpd_debug("response message: %s\n", response->data);

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		sess = response->session_data;

		if (sess->ret == RET_SUCCESS)
			handle_success_led_state();
		else
			handle_fail_led_state();

		free(response->session_data);
	}
}

static void result_handler(enum httpd_uri_handler_status status,
        struct httpd_request *request,
        struct httpd_response *response)
{
	struct httpd_form_value *auto_reboot;
	struct result_session *st;
	u32 size;

	if (status == HTTP_CB_NEW) {
#if defined(CONFIG_H3C_HW_C_RAM_READONLY)
		rt3_metadata_invalidate();
		response->status = HTTP_RESP_STD;
		response->info.code = 403;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->data = "{\"status\":\"fail\",\"error\":\"read-only target\"}";
		response->size = strlen(response->data);
		return;
#endif
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		handle_start_led_state();

		auto_reboot = httpd_request_find_value(request, "auto_reboot");

		if (!auto_reboot || !auto_reboot->data || strcmp(auto_reboot->data, "true"))
			st->auto_reboot = false;
		else
			st->auto_reboot = true;

		st->ret = RET_FAILURE;

		response->session_data = st;

		response->status = HTTP_RESP_CUSTOM;

		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		response->info.code = 200;

		size = http_make_response_header(&response->info,
			st->buf, sizeof(st->buf));

		response->data = st->buf;
		response->size = size;

		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		st = response->session_data;

		if (st->body_sent) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		if (upload_data_id == fs_upload_id) {
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
			enum rt3_md_kind kind;
			if (metadata_kind_for_upgrade(upgrade_type,&kind)) {
				char token[65];
				st->auto_reboot = false;
				if (metadata_commit_token(request,token)) {
					st->ret=rt3_metadata_write_target(kind,RT3_MD_TARGET_DEFAULT,NULL,response);
				} else if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_CDT) {
					enum rt3_md_target target;
					int explicit_target;
					if (metadata_upload_target(request,upgrade_type,&target,&explicit_target))
						st->ret=rt3_metadata_write_target(kind,RT3_MD_TARGET_DEFAULT,NULL,response);
					else if (explicit_target)
						st->ret=rt3_metadata_write_target(kind,target,token,response);
					else {
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
						st->ret=rt3_metadata_write(kind,token,response);
#else
						st->ret=rt3_metadata_write_target(kind,RT3_MD_TARGET_DEFAULT,token,response);
#endif
					}
				} else {
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
					st->ret=rt3_metadata_write(kind,token,response);
#else
					st->ret=rt3_metadata_write_target(kind,RT3_MD_TARGET_DEFAULT,token,response);
#endif
				}
			} else {
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
				if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS) {
					rt3_metadata_invalidate();
					if (failsafe_initramfs_is_validated((ulong)upload_data,
								    (ulong)upload_size)) {
						st->auto_reboot = true;
						st->ret = RET_SUCCESS;
					} else {
						failsafe_initramfs_invalidate();
						st->auto_reboot = false;
						st->ret = RET_FAILURE;
						strlcpy(st->buf,
							"{\"status\":\"fail\",\"info\":{\"type\":\"initramfs_not_validated\"}}",
							sizeof(st->buf));
						response->data = st->buf;
					}
				} else {
					rt3_metadata_invalidate();
					failsafe_initramfs_invalidate();
					st->auto_reboot = false;
					st->ret=failsafe_write_image(upgrade_type,(ulong)upload_data,
						(ulong)upload_size,response);
				}
#else
				if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS) {
					st->auto_reboot = true;
					st->ret = RET_SUCCESS;
				} else {
					st->ret=failsafe_write_image(upgrade_type,(ulong)upload_data,
						(ulong)upload_size,response);
				}
#endif
			}
#endif
		} else {
			snprintf(st->buf, sizeof(st->buf),
				"{\"status\":\"fail\","
				"\"info\":{\"type\":\"upload_id_mismatch\","
				"\"upload_data_id\":\"%u\",\"fs_upload_id\":\"%u\"}}",
				upload_data_id, fs_upload_id);
			response->data = st->buf;
			st->ret = RET_UPLOAD_ID_MISMATCH;
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
			rt3_metadata_invalidate();
#endif
#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B
			failsafe_initramfs_invalidate();
#endif
		}

#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
		/* Headers were already streamed; preserve the custom body lifecycle
		 * even when a metadata helper formats a standard error response. */
		response->status = HTTP_RESP_CUSTOM;
#endif

		/* invalidate upload identifier */
		upload_data_id = rand();

		if (st->ret == RET_SUCCESS) {
			snprintf(st->buf, sizeof(st->buf),
				"{\"status\":\"success\",\"info\":{\"reboot\":%s}}",
				st->auto_reboot ? "true" : "false");
			response->data = st->buf;
		}

		response->size = strlen(response->data);
		st->body_sent = 1;

		httpd_debug("response message: %s\n", response->data);

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		bool upgrade_success;

		st = response->session_data;
		upgrade_success = st->ret == RET_SUCCESS ? true : false;
		auto_action_pending = st->auto_reboot && upgrade_success;

		free(response->session_data);

		if (upgrade_success)
			handle_success_led_state();
		else
			handle_fail_led_state();

		if (auto_action_pending)
			tcp_close_all_conn();
	}
}

#endif /* !CONFIG_H3C_HW_D_NOR_RESCUE */

#ifdef CONFIG_H3C_HW_D_NOR_RESCUE
extern void nor_upgrade_register_uri_handlers(struct httpd_instance *inst);
extern void nor_nand_autoecc_register_uri_handlers(struct httpd_instance *inst);
#endif

void upgrade_register_uri_handlers(struct httpd_instance *inst)
{
#ifdef CONFIG_H3C_HW_D_NOR_RESCUE
	nor_upgrade_register_uri_handlers(inst);
	nor_nand_autoecc_register_uri_handlers(inst);
#else
	httpd_register_uri_handler(inst, "/upload", &upload_handler, NULL);
	httpd_register_uri_handler(inst, "/result", &result_handler, NULL);
#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
	/* Metadata commits are token-gated in the result handler. */
	httpd_register_uri_handler(inst, "/write", &result_handler, NULL);
#endif
#endif
}
