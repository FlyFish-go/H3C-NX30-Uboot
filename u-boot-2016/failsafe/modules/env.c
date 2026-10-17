/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe environment management
 */

/*
 * Modified by: chenxin527
 */

#include <common.h>
#include <environment.h>
#include <errno.h>
#include <malloc.h>
#include <net/httpd.h>
#include <failsafe_env_text.h>
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
#include <rt3000_b_env.h>
#endif
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
#include <search.h>
#endif

#include "modules.h"

#include <h3c_board_profile.h>
#define RT3_ENV_BOARD_LABEL H3C_BOARD_LABEL

#define ENV_NAME_MAX_LEN 128

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
DECLARE_GLOBAL_DATA_PTR;

static struct failsafe_env_text_buffer *failsafe_env_current_text;

static int failsafe_env_append_entry(ENTRY *entry)
{
	if (!entry)
		return -EINVAL;
	return failsafe_env_text_append(failsafe_env_current_text,
			entry->key, entry->data);
}
#endif

static void failsafe_env_free_session(enum httpd_uri_handler_status status,
	struct httpd_response *response)
{
	if (status != HTTP_CB_CLOSED)
		return;

	if (response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
	}
}

static void failsafe_http_reply_text(struct httpd_response *response, int code,
	const char *text)
{
	response->status = HTTP_RESP_STD;
	response->data = text ? text : "";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

static int failsafe_env_get_form_value(struct httpd_request *request,
	const char *key, char **out, size_t max_len, bool allow_empty)
{
	struct httpd_form_value *v;
	char *buf;
	size_t n;

	if (!request || !key || !out)
		return -EINVAL;

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	{
		u32 i, matches = 0;
		if (request->form.count > MAX_HTTP_FORM_VALUE_ITEMS)
			return -EINVAL;
		v = NULL;
		for (i = 0; i < request->form.count; i++) {
			struct httpd_form_value *candidate = &request->form.values[i];
			if (candidate->name && !strcmp(candidate->name, key)) {
				v = candidate;
				matches++;
			}
		}
		if (matches > 1)
			return -EINVAL;
	}
#else
	v = httpd_request_find_value(request, key);
#endif
	if (!v || !v->data) {
		if (allow_empty) {
			buf = strdup("");
			if (!buf)
				return -ENOMEM;
			*out = buf;
			return 0;
		}
		return -EINVAL;
	}

	n = v->size;
	if (!allow_empty && !n)
		return -EINVAL;
	if (n > max_len)
		return -E2BIG;
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	if (memchr(v->data, '\0', n))
		return -EINVAL;
#endif

	buf = malloc(n + 1);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, v->data, n);
	buf[n] = '\0';
	*out = buf;
	return 0;
}

#if !(defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) && !defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
static char *failsafe_env_export_text(size_t *out_len)
{
	env_t *envbuf;
	char *out;
	size_t out_sz, i, n;

	if (out_len)
		*out_len = 0;

	envbuf = malloc(sizeof(*envbuf));
	if (!envbuf)
		return NULL;

	if (env_export(envbuf)) {
		free(envbuf);
		return NULL;
	}

	out_sz = ENV_SIZE + 2;
	out = malloc(out_sz);
	if (!out) {
		free(envbuf);
		return NULL;
	}

	n = 0;
	for (i = 0; i < ENV_SIZE - 1 && n < out_sz - 1; i++) {
		if (envbuf->data[i] == '\0' && envbuf->data[i + 1] == '\0')
			break;

		if (envbuf->data[i] == '\0')
			out[n++] = '\n';
		else
			out[n++] = envbuf->data[i];
	}

	if (n > 0 && out[n - 1] != '\n' && n < out_sz - 1)
		out[n++] = '\n';

	out[n] = '\0';
	if (out_len)
		*out_len = n;

	free(envbuf);
	return out;
}

#endif /* generic fixed-size ENV export */

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
static bool failsafe_env_ram_ready(void)
{
	return (gd->flags & GD_FLG_ENV_READY) && env_htab.table;
}

static char *failsafe_env_export_ram_text(size_t *out_len)
{
	struct failsafe_env_text_buffer text;
	size_t measured_len;
	char *out;
	int ret;

	if (out_len)
		*out_len = 0;
	if (!failsafe_env_ram_ready())
		return NULL;

	/* Measure first so RAM-only values are not limited by the flash env size. */
	text.data = NULL;
	text.size = 0;
	text.len = 0;
	failsafe_env_current_text = &text;
	ret = hwalk_r(&env_htab, failsafe_env_append_entry);
	failsafe_env_current_text = NULL;
	if (ret || text.len == (size_t)-1)
		return NULL;
	measured_len = text.len;

	out = malloc(measured_len + 1);
	if (!out)
		return NULL;
	text.data = out;
	text.size = measured_len + 1;
	text.len = 0;
	out[0] = '\0';
	failsafe_env_current_text = &text;
	ret = hwalk_r(&env_htab, failsafe_env_append_entry);
	failsafe_env_current_text = NULL;
	if (ret || text.len != measured_len) {
		free(out);
		return NULL;
	}
	if (out_len)
		*out_len = text.len;
	return out;
}

#endif /* A/B shared RAM ENV read path */

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
#define RT3_ENV_PROTECTED_COUNT 6

static const char * const rt3_env_protected_names[RT3_ENV_PROTECTED_COUNT] = {
	"bootcmd", "bootargs", "bootdelay", "ethact", "mtdparts", "mtdids",
};

typedef int (*rt3_env_mutator_fn)(void *arg);

struct rt3_env_protected_snapshot {
	char *value[RT3_ENV_PROTECTED_COUNT];
};

static int rt3_env_name_valid(const char *name)
{
	size_t i, len;
	unsigned char c;

	if (!name)
		return 0;
	len = strnlen(name, ENV_NAME_MAX_LEN + 1);
	if (!len || len > ENV_NAME_MAX_LEN)
		return 0;
	for (i = 0; i < len; i++) {
		c = (unsigned char)name[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_'))
			return 0;
		if (!i && c >= '0' && c <= '9')
			return 0;
	}
	return 1;
}

static int rt3_env_name_protected(const char *name)
{
	unsigned int i;

	for (i = 0; i < RT3_ENV_PROTECTED_COUNT; i++)
		if (!strcmp(name, rt3_env_protected_names[i]))
			return 1;
	return 0;
}

static int rt3_env_value_valid(const char *value)
{
	size_t i, len;
	unsigned char c;

	if (!value)
		return 0;
	len = strnlen(value, ENV_SIZE);
	if (len >= ENV_SIZE)
		return 0;
	for (i = 0; i < len; i++) {
		c = (unsigned char)value[i];
		if (c < 0x20 || c == 0x7f)
			return 0;
	}
	return 1;
}

static int rt3_env_protected_capture(struct rt3_env_protected_snapshot *snap)
{
	unsigned int i;

	memset(snap, 0, sizeof(*snap));
	for (i = 0; i < RT3_ENV_PROTECTED_COUNT; i++) {
		const char *value = getenv(rt3_env_protected_names[i]);
		if (value) {
			snap->value[i] = strdup(value);
			if (!snap->value[i])
				goto fail;
		}
	}
	return 0;
fail:
	while (i)
		free(snap->value[--i]);
	return -ENOMEM;
}

static void rt3_env_protected_free(struct rt3_env_protected_snapshot *snap)
{
	unsigned int i;

	for (i = 0; i < RT3_ENV_PROTECTED_COUNT; i++)
		free(snap->value[i]);
}

static int rt3_env_protected_equal(const struct rt3_env_protected_snapshot *snap)
{
	unsigned int i;

	for (i = 0; i < RT3_ENV_PROTECTED_COUNT; i++) {
		const char *value = getenv(rt3_env_protected_names[i]);
		if ((!value) != (!snap->value[i]) ||
		    (value && strcmp(value, snap->value[i])))
			return 0;
	}
	return 1;
}

static int rt3_env_protected_restore(const struct rt3_env_protected_snapshot *snap)
{
	unsigned int i;

	for (i = 0; i < RT3_ENV_PROTECTED_COUNT; i++) {
		const char *current = getenv(rt3_env_protected_names[i]);
		/* Deleting an already absent key reports failure in this U-Boot.
		 * A preserved absent key needs no mutation. */
		if ((!current && !snap->value[i]) ||
		    (current && snap->value[i] && !strcmp(current, snap->value[i])))
			continue;
		if (setenv(rt3_env_protected_names[i], snap->value[i]))
			return -EIO;
	}
	return 0;
}

static env_t *rt3_env_snapshot(void)
{
	env_t *snapshot;

	if (!failsafe_env_ram_ready())
		return NULL;
	snapshot = (env_t *)memalign(ARCH_DMA_MINALIGN, CONFIG_ENV_SIZE);
	if (!snapshot)
		return NULL;
	memset(snapshot, 0, CONFIG_ENV_SIZE);
	if (env_export(snapshot) ||
	    crc32(0, snapshot->data, ENV_SIZE) != snapshot->crc) {
		free(snapshot);
		return NULL;
	}
	return snapshot;
}

static int rt3_env_rollback_ram(env_t *snapshot)
{
	/* env_import returns one on a complete import, zero on CRC/import failure. */
	if (env_import((const char *)snapshot, 1) != 1) {
		printf("%s ENV RAM rollback failed\\n", RT3_ENV_BOARD_LABEL);
		return -EIO;
	}
	return 0;
}

static int rt3_env_transaction(rt3_env_mutator_fn mutate, void *arg,
			       int preserve_protected)
{
	struct rt3_env_protected_snapshot protected;
	env_t *snapshot;
	int ret, rollback_ret;

#ifdef CONFIG_IPQ5018_ENV
	/* The IPQ backend is a runtime function pointer, including diagnostics. */
	if (!saveenv)
		return -EROFS;
#endif
	snapshot = rt3_env_snapshot();
	if (!snapshot)
		return -ENOMEM;
	ret = rt3_env_protected_capture(&protected);
	if (ret) {
		free(snapshot);
		return ret;
	}
	ret = mutate(arg);
	if (ret >= 0 && preserve_protected)
		ret = rt3_env_protected_restore(&protected) ? -EIO : ret;
	if (ret >= 0 && !preserve_protected && !rt3_env_protected_equal(&protected))
		ret = -EPERM;
	if (ret == 1) {
		ret = 0; /* Unchanged request: no flash write is necessary. */
		goto done;
	}
	if (!ret) {
		ret = saveenv();
		if (!ret)
			goto done;
		ret = -EIO;
	}
	rollback_ret = rt3_env_rollback_ram(snapshot);
	if (rollback_ret)
		ret = rollback_ret;
done:
	rt3_env_protected_free(&protected);
	free(snapshot);
	return ret;
}

struct rt3_env_pair {
	const char *name;
	const char *value;
};

static int rt3_env_mutate_set(void *arg)
{
	struct rt3_env_pair *pair = arg;
	const char *current = getenv(pair->name);

	if (current && !strcmp(current, pair->value))
		return 1;
	return setenv(pair->name, pair->value) ? -EIO : 0;
}

static int rt3_env_mutate_unset(void *arg)
{
	const char *name = arg;

	if (!getenv(name))
		return 1;
	return setenv(name, NULL) ? -EIO : 0;
}

static int rt3_env_mutate_reset_single(void *arg)
{
	const char *name = arg;
	const char *current = getenv(name);
	const char *default_value = getenv_default(name);

	if (!default_value)
		return -ENOENT;
	if (current && !strcmp(current, default_value))
		return 1;
	return setenv(name, default_value) ? -EIO : 0;
}

static int rt3_env_mutate_reset_all(void *arg)
{
	(void)arg;
	set_default_env(NULL);
	return 0;
}

static int rt3_env_mutate_import(void *arg)
{
	return env_import((const char *)arg, 1) == 1 ? 0 : -EBADMSG;
}

static int rt3_env_form_schema(struct httpd_request *request,
			       const char * const *allowed, size_t allowed_count)
{
	unsigned char seen[MAX_HTTP_FORM_VALUE_ITEMS] = {0};
	u32 i, j;

	if (!request || request->form.count > MAX_HTTP_FORM_VALUE_ITEMS ||
	    request->form.count != allowed_count)
		return -EINVAL;
	for (i = 0; i < request->form.count; i++) {
		const char *field = request->form.values[i].name;
		if (!field)
			return -EINVAL;
		for (j = 0; j < allowed_count; j++) {
			if (!strcmp(field, allowed[j]))
				break;
		}
		if (j == allowed_count || seen[j])
			return -EINVAL;
		seen[j] = 1;
	}
	return 0;
}

int failsafe_env_set_persistent(const char *name, const char *value)
{
	struct rt3_env_pair pair;

	if (!rt3_env_name_valid(name) || rt3_env_name_protected(name) ||
	    !rt3_env_value_valid(value))
		return -EINVAL;
	pair.name = name;
	pair.value = value;
	return rt3_env_transaction(rt3_env_mutate_set, &pair, 0);
}

#endif /* B-only persistent mutation helpers */

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
static void env_info_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)
	static const char info[] = "{\"source\":\"current RAM environment\",\"read_only\":true,\"persistent_write_supported\":false}";
#else
	static const char info[] = "{\"source\":\"current RAM environment\",\"read_only\":false,\"persistent_write_supported\":true,\"persistent_target\":\"0:APPSBLENV\",\"persistent_verify\":\"full 512 KiB readback after explicit save\"}";
#endif
	static const char no_backend[] = "{\"source\":\"current RAM environment\",\"read_only\":true,\"persistent_write_supported\":false}";
	static const char unavailable[] = "{\"error\":\"environment unavailable\"}";

	if (status != HTTP_CB_NEW)
		return;
	response->status = HTTP_RESP_STD;
	response->info.code = !request || request->method != HTTP_GET ? 405 :
		(failsafe_env_ram_ready() ? 200 : 503);
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
#ifdef CONFIG_IPQ5018_ENV
	if (response->info.code == 200 && !saveenv) {
		response->data = no_backend;
		response->size = strlen(no_backend);
		return;
	}
#else
	(void)no_backend;
#endif
	response->data = response->info.code == 200 ? info :
		(response->info.code == 405 ? "GET required" : unavailable);
	response->size = strlen(response->data);
}
#endif

static void env_list_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
	char *out;
	size_t out_len = 0;

	if (status == HTTP_CB_CLOSED) {
		failsafe_env_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}

#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
	if (!failsafe_env_ram_ready()) {
		failsafe_http_reply_text(response, 503, "environment unavailable");
		return;
	}
	out = failsafe_env_export_ram_text(&out_len);
#else
	out = failsafe_env_export_text(&out_len);
#endif
	if (!out) {
		failsafe_http_reply_text(response, 500, "export failed");
		return;
	}

	response->status = HTTP_RESP_STD;
	response->data = out;
	response->size = out_len;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	response->session_data = out;
}

static void env_set_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
	int ret;
#if !(defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	char *name = NULL, *value = NULL, *current_value = NULL;
	bool changed = false;
#endif

	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	{
		static const char * const fields[] = { "name", "value" };
		char *name = NULL, *value = NULL;
		if (rt3_env_form_schema(request, fields, ARRAY_SIZE(fields)) ||
		    failsafe_env_get_form_value(request, "name", &name,
						ENV_NAME_MAX_LEN, false)) {
			failsafe_http_reply_text(response, 400, "bad name or fields");
			return;
		}
		if (failsafe_env_get_form_value(request, "value", &value,
					ENV_SIZE - 1, true)) {
			free(name);
			failsafe_http_reply_text(response, 400, "bad value");
			return;
		}
		if (!rt3_env_name_valid(name) || rt3_env_name_protected(name) ||
		    !rt3_env_value_valid(value)) {
			free(value);
			free(name);
			failsafe_http_reply_text(response, 403, "protected or invalid variable");
			return;
		}
		ret = failsafe_env_set_persistent(name, value);
		free(value);
		free(name);
		if (!ret)
			failsafe_http_reply_text(response, 200, "ok");
		else
			failsafe_http_reply_text(response, 500, "save or verification failed; RAM restored");
		return;
	}
#else
	ret = failsafe_env_get_form_value(request, "name", &name,
		ENV_NAME_MAX_LEN, false);
	if (ret) {
		failsafe_http_reply_text(response, 400, "bad name");
		return;
	}
	ret = failsafe_env_get_form_value(request, "value", &value,
		ENV_SIZE - 1, true);
	if (ret) {
		free(name);
		failsafe_http_reply_text(response, 400, "bad value");
		return;
	}
	current_value = getenv(name);
	if (!current_value || strcmp(current_value, value))
		if (!setenv(name, value))
			changed = true;
	if (changed)
		ret = saveenv();
	else
		ret = 0;
	free(name);
	free(value);
	if (!ret)
		failsafe_http_reply_text(response, 200, "ok");
	else
		failsafe_http_reply_text(response, 500, "save failed");
#endif
}
static void env_unset_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
	int ret;
#if !(defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	char *name = NULL;
#endif

	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	{
		static const char * const fields[] = { "name" };
		char *name = NULL;
		if (rt3_env_form_schema(request, fields, ARRAY_SIZE(fields)) ||
		    failsafe_env_get_form_value(request, "name", &name,
						ENV_NAME_MAX_LEN, false)) {
			failsafe_http_reply_text(response, 400, "bad name or fields");
			return;
		}
		if (!rt3_env_name_valid(name) || rt3_env_name_protected(name)) {
			free(name);
			failsafe_http_reply_text(response, 403, "protected or invalid variable");
			return;
		}
		ret = rt3_env_transaction(rt3_env_mutate_unset, name, 0);
		free(name);
	}
#else
	ret = failsafe_env_get_form_value(request, "name", &name,
		ENV_NAME_MAX_LEN, false);
	if (ret) {
		failsafe_http_reply_text(response, 400, "bad name");
		return;
	}
	ret = setenv(name, NULL);
	if (!ret)
		ret = saveenv();
	free(name);
#endif
	if (ret) {
		failsafe_http_reply_text(response, 500, "save or verification failed; RAM restored");
		return;
	}
	failsafe_http_reply_text(response, 200, "ok");
}
static void env_reset_all_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
	int ret;

	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	if (rt3_env_form_schema(request, NULL, 0)) {
		failsafe_http_reply_text(response, 400, "unexpected fields");
		return;
	}
	ret = rt3_env_transaction(rt3_env_mutate_reset_all, NULL, 1);
#else
	set_default_env(NULL);
	ret = saveenv();
#endif
	if (!ret)
		failsafe_http_reply_text(response, 200, "ok");
	else
		failsafe_http_reply_text(response, 500, "save or verification failed; RAM restored");
}
static void env_reset_single_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
	int ret;
#if !(defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	char *name, *current_value, *default_value;
	bool changed = false;
#endif

	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	{
		static const char * const fields[] = { "name" };
		char *name = NULL;
		if (rt3_env_form_schema(request, fields, ARRAY_SIZE(fields)) ||
		    failsafe_env_get_form_value(request, "name", &name,
						ENV_NAME_MAX_LEN, false)) {
			failsafe_http_reply_text(response, 400, "bad name or fields");
			return;
		}
		if (!rt3_env_name_valid(name) || rt3_env_name_protected(name)) {
			free(name);
			failsafe_http_reply_text(response, 403, "protected or invalid variable");
			return;
		}
		ret = rt3_env_transaction(rt3_env_mutate_reset_single, name, 0);
		free(name);
	}
#else
	ret = failsafe_env_get_form_value(request, "name", &name,
		ENV_NAME_MAX_LEN, false);
	if (ret) {
		failsafe_http_reply_text(response, 400, "bad name");
		return;
	}
	default_value = getenv_default(name);
	if (!default_value) {
		failsafe_http_reply_text(response, 406, "no default value");
		free(name);
		return;
	}
	current_value = getenv(name);
	if (!current_value || strcmp(current_value, default_value))
		if (!setenv(name, default_value))
			changed = true;
	if (changed)
		ret = saveenv();
	else
		ret = 0;
	free(name);
#endif
	if (ret == -ENOENT) {
		failsafe_http_reply_text(response, 406, "no default value");
		return;
	}
	if (!ret)
		failsafe_http_reply_text(response, 200, "ok");
	else
		failsafe_http_reply_text(response, 500, "save or verification failed; RAM restored");
}
static void env_restore_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
#if !(defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	struct httpd_form_value *envfile;
	uint32_t crc;
	env_t *envptr;
	bool success = false;
#else
	static const char * const fields[] = { "envfile" };
	struct httpd_form_value *envfile;
	env_t *staged;
	uint32_t crc;
	int ret;
#endif

	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_text(response, 405, "method");
		return;
	}
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY))
	if (rt3_env_form_schema(request, fields, ARRAY_SIZE(fields))) {
		failsafe_http_reply_text(response, 400, "bad file fields");
		return;
	}
	envfile = &request->form.values[0];
	if (!envfile->data ||
	    (envfile->size != CONFIG_ENV_SIZE &&
	     envfile->size != 2 * CONFIG_ENV_SIZE)) {
		failsafe_http_reply_text(response, 400, "file must be exact 256 KiB ENV or 512 KiB APPSBLENV");
		return;
	}
	staged = (env_t *)memalign(ARCH_DMA_MINALIGN, CONFIG_ENV_SIZE);
	if (!staged) {
		failsafe_http_reply_text(response, 500, "no memory");
		return;
	}
	memcpy(staged, envfile->data, CONFIG_ENV_SIZE);
	memcpy(&crc, &staged->crc, sizeof(crc));
	if (crc32(0, staged->data, ENV_SIZE) != crc) {
		free(staged);
		failsafe_http_reply_text(response, 400, "bad crc");
		return;
	}
	if (envfile->size == 2 * CONFIG_ENV_SIZE) {
		ret = rt3_nand_env_tail_matches(envfile->data + CONFIG_ENV_SIZE,
					       CONFIG_ENV_SIZE);
		if (ret) {
			free(staged);
			failsafe_http_reply_text(response, ret == -EINVAL ? 400 : 503,
				ret == -EINVAL ? "APPSBLENV protected tail mismatch" :
				"APPSBLENV geometry/read unavailable");
			return;
		}
	}
	ret = rt3_env_transaction(rt3_env_mutate_import, staged, 0);
	free(staged);
	if (!ret)
		failsafe_http_reply_text(response, 200, "ok");
	else if (ret == -EPERM)
		failsafe_http_reply_text(response, 400, "restore changes protected boot or partition variables");
	else
		failsafe_http_reply_text(response, 500, "restore or verification failed; RAM restored");
#else
	envfile = httpd_request_find_value(request, "envfile");
	if (!envfile || !envfile->data) {
		failsafe_http_reply_text(response, 400, "bad file");
		return;
	}
	envptr = (env_t *)envfile->data;
	memcpy(&crc, &envptr->crc, sizeof(crc));
	if (crc32(0, envptr->data, ENV_SIZE) != crc) {
		failsafe_http_reply_text(response, 400, "bad crc");
		return;
	}
	if (env_import(envfile->data, 0))
		if (!saveenv())
			success = true;
	if (success)
		failsafe_http_reply_text(response, 200, "ok");
	else
		failsafe_http_reply_text(response, 500, "restore failed");
#endif
}
void env_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/env/list", &env_list_handler, NULL);
#if (defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || (defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)
	httpd_register_uri_handler(inst, "/env/info", &env_info_handler, NULL);
#endif
	httpd_register_uri_handler(inst, "/env/set", &env_set_handler, NULL);
	httpd_register_uri_handler(inst, "/env/unset", &env_unset_handler, NULL);
	httpd_register_uri_handler(inst, "/env/reset/all", &env_reset_all_handler, NULL);
	httpd_register_uri_handler(inst, "/env/reset/single", &env_reset_single_handler, NULL);
	httpd_register_uri_handler(inst, "/env/restore", &env_restore_handler, NULL);
}
