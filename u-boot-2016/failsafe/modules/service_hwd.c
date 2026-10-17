// SPDX-License-Identifier: GPL-2.0+
/* NX30 HW-D NAND service: fixed-layout, named-object HTTP operations only. */
#ifdef HWD_SERVICE_HOST_TEST
#include "service_hwd_host_stubs.h"
#else
#include <common.h>
#include <asm/errno.h>
#include <asm/arch-qca-common/gpio.h>
#include <command.h>
#include <linux/ctype.h>
#include <ipq_led.h>
#include <malloc.h>
#include <net/httpd.h>
#include <net.h>
#include <u-boot/sha256.h>
#include <nx30_d_service.h>
#include "modules.h"
#endif

#define D_SERVICE_WINDOW (64U * 1024U)
#define D_SERVICE_JSON_MAX 2048U
#define D_SERVICE_TOKEN_CHARS (NX30_D_TOKEN_LEN - 1U)
#define D_UPDATE_TOKEN_TTL_MS 300000U

enum d_session_kind { D_SESSION_JSON = 1, D_SESSION_BACKUP };
enum d_after_close { D_AFTER_NONE, D_AFTER_BOOT_SLOT, D_AFTER_RESET,
		     D_AFTER_CONSOLE, D_AFTER_NETWORK_RAM };

struct d_json_session {
	enum d_session_kind kind;
	enum d_after_close after_close;
	enum nx30_d_slot slot;
	const char *command;
	struct nx30_d_env_change changes[NX30_D_ENV_MAX_CHANGES];
	size_t change_count;
	int body_sent;
	char header[512];
	char body[D_SERVICE_JSON_MAX];
};

enum d_backup_phase { D_BACKUP_HEADER, D_BACKUP_DATA };
struct d_backup_session {
	enum d_session_kind kind;
	enum nx30_d_object object;
	u32 generation;
	struct nx30_d_extent extent;
	u64 total;
	u64 current;
	enum d_backup_phase phase;
	char filename[96];
	char header[512];
	int header_len;
	void *window;
};

struct d_pending_update {
	bool valid;
	u32 created_ms;
	u32 generation;
	u32 image_crc32;
	u32 image_bytes;
	u8 image_sha256[SHA256_SUM_LEN];
	char token[NX30_D_TOKEN_LEN];
};

/* Small one-shot metadata only; the browser reuploads the file for writing. */
static struct d_pending_update d_pending_updates[2];
static u32 d_update_token_serial;

static void d_api_error(struct httpd_response *response, const char *operation,
			int ret);

static struct d_json_session *d_json_begin(struct httpd_response *response)
{
	struct d_json_session *session = calloc(1, sizeof(*session));
	if (!session)
		return NULL;
	session->kind = D_SESSION_JSON;
	response->session_data = session;
	return session;
}

static void d_json_finish(struct httpd_response *response,
			  struct d_json_session *session, int code)
{
	response->status = HTTP_RESP_STD;
	response->data = session->body;
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
}

static struct d_json_session *d_upload_begin(struct httpd_response *response)
{
	struct d_json_session *session = d_json_begin(response);
	u32 header_size;
	if (!session)
		return NULL;
	response->status = HTTP_RESP_CUSTOM;
	response->info.http_1_0 = 1;
	response->info.content_length = -1;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
	response->info.code = 200;
	header_size = http_make_response_header(&response->info, session->header,
						 sizeof(session->header));
	if (!header_size || header_size >= sizeof(session->header)) {
		free(session);
		response->session_data = NULL;
		return NULL;
	}
	response->data = session->header;
	response->size = header_size;
	return session;
}

static void d_upload_finish(struct httpd_response *response,
			    struct d_json_session *session)
{
	response->status = HTTP_RESP_CUSTOM;
	response->data = session->body;
	response->size = strlen(session->body);
	session->body_sent = 1;
}

static void d_upload_error(struct httpd_response *response,
			   const char *operation, int ret)
{
	struct d_json_session *session = response->session_data;
	if (!session || session->kind != D_SESSION_JSON) {
		response->status = HTTP_RESP_NONE;
		return;
	}
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"error\",\"operation\":\"%s\",\"backend_errno\":%d}",
		 operation, ret);
	d_upload_finish(response, session);
}

static void d_json_error(struct httpd_response *response, int code,
			 const char *error)
{
	struct d_json_session *session = d_json_begin(response);
	if (!session) {
		response->status = HTTP_RESP_STD;
		response->data = "{\"status\":\"error\",\"error\":\"no memory\"}";
		response->size = strlen(response->data);
		response->info.code = 500;
		response->info.connection_close = 1;
		response->info.content_type = "application/json";
		return;
	}
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"error\",\"error\":\"%s\"}", error);
	d_json_finish(response, session, code);
}

static void d_close_json(enum httpd_uri_handler_status status,
			 struct httpd_response *response)
{
	struct d_json_session *session;
	enum d_after_close action;
	enum nx30_d_slot slot;
	const char *command;
	struct nx30_d_env_change changes[NX30_D_ENV_MAX_CHANGES];
	size_t change_count;

	if (status != HTTP_CB_CLOSED || !response->session_data)
		return;
	session = response->session_data;
	if (session->kind != D_SESSION_JSON)
		return;
	action = session->after_close;
	slot = session->slot;
	command = session->command;
	change_count = session->change_count;
	if (change_count)
		memcpy(changes, session->changes, change_count * sizeof(changes[0]));
	response->session_data = NULL;
	free(session);
	switch (action) {
	case D_AFTER_BOOT_SLOT:
		nx30_d_boot_slot(slot);
		break;
	case D_AFTER_RESET:
		run_command("reset", 0);
		break;
	case D_AFTER_CONSOLE:
		if (command)
			run_command(command, 0);
		break;
	case D_AFTER_NETWORK_RAM:
		{
			size_t i;
			for (i = 0; i < change_count; i++)
				if (setenv(changes[i].name,
					   changes[i].unset ? NULL : changes[i].value))
					printf("service: RAM network setting apply failed (%d)\n", (int)i);
			if (getenv("ipaddr"))
				net_ip = string_to_ip(getenv("ipaddr"));
			if (getenv("netmask"))
				net_netmask = string_to_ip(getenv("netmask"));
			if (getenv("serverip"))
				net_server_ip = string_to_ip(getenv("serverip"));
		}
		break;
	default:
		break;
	}
}

static int d_get_layout_slots(struct nx30_d_layout *layout,
			      struct nx30_d_slots *slots)
{
	int ret = nx30_d_layout_get(layout);

	if (ret)
		return ret;
	return nx30_d_slots_get(slots);
}

static void d_status_handler(enum httpd_uri_handler_status status,
			 struct httpd_request *request,
			 struct httpd_response *response)
{
	struct nx30_d_layout layout;
	struct nx30_d_slots slots;
	struct d_json_session *session;
	int appsbl_active;
	int ret;

	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_GET) {
		d_json_error(response, 405, "GET required");
		return;
	}
	ret = d_get_layout_slots(&layout, &slots);
	if (ret) {
		d_json_error(response, 503, "validated NX30 NAND layout unavailable");
		return;
	}
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	/* D persistent-write qualification remains closed. */
	appsbl_active = 0;
#ifdef CONFIG_H3C_HW_D_SERVICE_RAM
	snprintf(session->body, sizeof(session->body),
		 "{\"profile\":\"nand-service-ram\",\"ram_only\":true,"
		 "\"read_only\":false,\"layout_generation\":%u,"
		 "\"nand_size\":%u,\"erase_size\":%u,\"write_size\":%u,"
		 "\"ecc_bits\":%u,\"selector\":%u,\"age\":%u,"
		 "\"active_physical\":\"%c\",\"active_logical\":\"%s\","
		 "\"capabilities\":{\"named_backup\":true,\"slot_boot\":true,"
		 "\"env_transaction\":true,\"default_slot\":true,"
		 "\"firmware_write\":true,\"appsbl_write\":%s,"
		 "\"network_ram\":true,\"reboot\":true,\"led\":true,\"console\":true}}",
		 layout.generation, layout.nand_size, layout.erase_size,
		 layout.write_size, slots.ecc_bits, slots.selector, slots.age,
		 slots.active == NX30_D_SLOT_A ? 'A' : 'B', slots.active_logical,
			 appsbl_active ? "true" : "false");
#else
	snprintf(session->body, sizeof(session->body),
		 "{\"profile\":\"nand-service\",\"ram_only\":false,"
		 "\"read_only\":false,\"layout_generation\":%u,"
		 "\"nand_size\":%u,\"erase_size\":%u,\"write_size\":%u,"
		 "\"ecc_bits\":%u,\"selector\":%u,\"age\":%u,"
		 "\"active_physical\":\"%c\",\"active_logical\":\"%s\","
		 "\"capabilities\":{\"named_backup\":true,\"slot_boot\":true,"
		 "\"env_transaction\":true,\"default_slot\":true,"
		 "\"firmware_write\":true,\"appsbl_write\":%s,"
		 "\"network_ram\":true,\"reboot\":true,\"led\":true,\"console\":true}}",
		 layout.generation, layout.nand_size, layout.erase_size,
		 layout.write_size, slots.ecc_bits, slots.selector, slots.age,
		 slots.active == NX30_D_SLOT_A ? 'A' : 'B', slots.active_logical,
			 appsbl_active ? "true" : "false");
#endif
	d_json_finish(response, session, 200);
}

static void d_slots_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	struct nx30_d_layout layout;
	struct nx30_d_slots slots;
	struct d_json_session *session;
	int ret;

	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_GET) {
		d_json_error(response, 405, "GET required");
		return;
	}
	ret = d_get_layout_slots(&layout, &slots);
	if (ret) {
		d_json_error(response, 503, "validated NX30 NAND layout unavailable");
		return;
	}
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	snprintf(session->body, sizeof(session->body),
		 "{\"generation\":%u,\"selector\":%u,\"age\":%u,"
		 "\"ecc_bits\":%u,\"active_slot\":\"%c\","
		 "\"inactive_slot\":\"%c\",\"active_logical\":\"%s\","
		 "\"logical_a\":\"%s\",\"logical_b\":\"%s\","
		 "\"default_slot\":\"%c\",\"slot_a_offset\":%llu,"
		 "\"slot_a_size\":%llu,\"slot_b_offset\":%llu,"
		 "\"slot_b_size\":%llu,\"mutations_ready\":%s}",
		 slots.generation, slots.selector, slots.age, slots.ecc_bits,
		 slots.active == NX30_D_SLOT_A ? 'a' : 'b',
		 slots.inactive == NX30_D_SLOT_A ? 'a' : 'b', slots.active_logical,
		 slots.logical_a, slots.logical_b,
		 slots.active == NX30_D_SLOT_A ? 'a' : 'b',
		 (unsigned long long)layout.slot_a.offset,
		 (unsigned long long)layout.slot_a.size,
		 (unsigned long long)layout.slot_b.offset,
		 (unsigned long long)layout.slot_b.size, (slots.ecc_bits == 4U && layout.ecc_bits == 4U) ? "true" : "false");
	d_json_finish(response, session, 200);
}

static int d_backup_object(const char *name, enum nx30_d_object *object,
			   const char **filename)
{
	if (!name || !object || !filename)
		return -EINVAL;
	if (!strcmp(name, "env")) {
		*object = NX30_D_OBJ_ENV; *filename = "env";
	} else if (!strcmp(name, "bc0")) {
		*object = NX30_D_OBJ_BC0; *filename = "bc0";
	} else if (!strcmp(name, "bc1")) {
		*object = NX30_D_OBJ_BC1; *filename = "bc1";
	} else if (!strcmp(name, "bc-original0")) {
		*object = NX30_D_OBJ_BC_ORIGINAL0; *filename = "bc-original0";
	} else if (!strcmp(name, "bc-original1")) {
		*object = NX30_D_OBJ_BC_ORIGINAL1; *filename = "bc-original1";
	} else if (!strcmp(name, "appsbl-high")) {
		*object = NX30_D_OBJ_APPSBL_HIGH; *filename = "appsbl-high";
	} else if (!strcmp(name, "appsbl-low")) {
		*object = NX30_D_OBJ_APPSBL_LOW; *filename = "appsbl-low";
	} else if (!strcmp(name, "art")) {
		*object = NX30_D_OBJ_ART; *filename = "art";
	} else if (!strcmp(name, "training")) {
		*object = NX30_D_OBJ_TRAINING; *filename = "training";
	} else if (!strcmp(name, "slot-a")) {
		*object = NX30_D_OBJ_SLOT_A; *filename = "slot-a";
	} else if (!strcmp(name, "slot-b")) {
		*object = NX30_D_OBJ_SLOT_B; *filename = "slot-b";
	} else if (!strcmp(name, "sbl1")) {
		*object = NX30_D_OBJ_SBL1; *filename = "sbl1";
	} else if (!strcmp(name, "mibib")) {
		*object = NX30_D_OBJ_MIBIB; *filename = "mibib";
	} else if (!strcmp(name, "qsee")) {
		*object = NX30_D_OBJ_QSEE; *filename = "qsee";
	} else if (!strcmp(name, "qsee-1")) {
		*object = NX30_D_OBJ_QSEE_1; *filename = "qsee-1";
	} else if (!strcmp(name, "devcfg")) {
		*object = NX30_D_OBJ_DEVCFG; *filename = "devcfg";
	} else if (!strcmp(name, "devcfg-1")) {
		*object = NX30_D_OBJ_DEVCFG_1; *filename = "devcfg-1";
	} else if (!strcmp(name, "cdt")) {
		*object = NX30_D_OBJ_CDT; *filename = "cdt";
	} else if (!strcmp(name, "cdt-1")) {
		*object = NX30_D_OBJ_CDT_1; *filename = "cdt-1";
	} else if (!strcmp(name, "pdt-data")) {
		*object = NX30_D_OBJ_PDT_DATA; *filename = "pdt-data";
	} else if (!strcmp(name, "pdt-data-1")) {
		*object = NX30_D_OBJ_PDT_DATA_1; *filename = "pdt-data-1";
	} else if (!strcmp(name, "exp")) {
		*object = NX30_D_OBJ_EXP; *filename = "exp";
	} else if (!strcmp(name, "plugin")) {
		*object = NX30_D_OBJ_PLUGIN; *filename = "plugin";
	} else {
		return -EINVAL;
	}
	return 0;
}

static int d_backup_extent_current(enum nx30_d_object object, u32 generation,
				   const struct nx30_d_extent *expected)
{
	struct nx30_d_layout layout;
	struct nx30_d_extent current;
	int ret = nx30_d_layout_get(&layout);
	if (ret)
		return ret;
	if (layout.generation != generation)
		return -ESTALE;
	ret = nx30_d_object_extent(object, &current);
	if (ret)
		return ret;
	if (current.offset != expected->offset || current.size != expected->size)
		return -ESTALE;
	return 0;
}

static int d_hash_object(enum nx30_d_object object, u64 total, u32 generation,
			 const struct nx30_d_extent *extent,
			 u8 digest[SHA256_SUM_LEN])
{
	sha256_context ctx;
	void *window;
	u64 offset = 0;
	int ret = 0;

	window = malloc(D_SERVICE_WINDOW);
	if (!window)
		return -ENOMEM;
	sha256_starts(&ctx);
	while (offset < total) {
		size_t want = (size_t)min_t(u64, total - offset,
					    D_SERVICE_WINDOW);
		size_t got = 0;

		ret = d_backup_extent_current(object, generation, extent);
		if (ret)
			break;
		ret = nx30_d_read_object(object, offset, window, want, &got);
		if (ret || got != want) {
			ret = ret ? ret : -EIO;
			break;
		}
		sha256_update(&ctx, window, (u32)got);
		offset += got;
	}
	if (!ret)
		ret = d_backup_extent_current(object, generation, extent);
	if (!ret)
		sha256_finish(&ctx, digest);
	free(window);
	return ret;
}

static void d_sha256_buffer(const void *buffer, size_t length,
			    u8 digest[SHA256_SUM_LEN])
{
	sha256_context ctx;
	sha256_starts(&ctx);
	sha256_update(&ctx, buffer, (u32)length);
	sha256_finish(&ctx, digest);
}

static void d_make_update_token(unsigned int kind,
				struct d_pending_update *pending)
{
	sha256_context ctx;
	u8 digest[SHA256_SUM_LEN];
	u32 serial = ++d_update_token_serial;
	u32 now = get_timer(0);
	unsigned int i;
	static const char hex[] = "0123456789abcdef";

	sha256_starts(&ctx);
	sha256_update(&ctx, pending->image_sha256, SHA256_SUM_LEN);
	sha256_update(&ctx, (const u8 *)&pending->generation, sizeof(pending->generation));
	sha256_update(&ctx, (const u8 *)&pending->image_crc32, sizeof(pending->image_crc32));
	sha256_update(&ctx, (const u8 *)&pending->image_bytes, sizeof(pending->image_bytes));
	sha256_update(&ctx, (const u8 *)&now, sizeof(now));
	sha256_update(&ctx, (const u8 *)&serial, sizeof(serial));
	sha256_update(&ctx, (const u8 *)&kind, sizeof(kind));
	sha256_finish(&ctx, digest);
	for (i = 0; i < D_SERVICE_TOKEN_CHARS / 2; i++) {
		pending->token[i * 2] = hex[digest[i] >> 4];
		pending->token[i * 2 + 1] = hex[digest[i] & 0xf];
	}
	pending->token[D_SERVICE_TOKEN_CHARS] = '\0';
	pending->created_ms = now;
	pending->valid = true;
}

static int d_pending_update_store(unsigned int kind, const void *image,
				  size_t length,
				  const struct nx30_d_firmware_info *info)
{
	struct d_pending_update *pending;
	if (kind >= ARRAY_SIZE(d_pending_updates) || !image || !length || !info)
		return -EINVAL;
	pending = &d_pending_updates[kind];
	memset(pending, 0, sizeof(*pending));
	pending->generation = info->generation;
	pending->image_crc32 = info->image_crc32;
	pending->image_bytes = info->image_bytes;
	d_sha256_buffer(image, length, pending->image_sha256);
	d_make_update_token(kind, pending);
	return 0;
}

static struct d_pending_update *d_pending_update_get(unsigned int kind,
						      const char *token,
						      const void *image,
						      size_t length)
{
	struct d_pending_update *pending;
	u8 digest[SHA256_SUM_LEN];
	u32 now;
	if (kind >= ARRAY_SIZE(d_pending_updates) || !token || !image)
		return NULL;
	pending = &d_pending_updates[kind];
	if (!pending->valid || strlen(token) != D_SERVICE_TOKEN_CHARS ||
	    strcmp(token, pending->token))
		return NULL;
	now = get_timer(0);
	if ((u32)(now - pending->created_ms) > D_UPDATE_TOKEN_TTL_MS) {
		pending->valid = false;
		return NULL;
	}
	if (length != pending->image_bytes)
		return NULL;
	d_sha256_buffer(image, length, digest);
	if (memcmp(digest, pending->image_sha256, sizeof(digest)))
		return NULL;
	return pending;
}

static void d_backup_handler(enum httpd_uri_handler_status status,
			     struct httpd_request *request,
			     struct httpd_response *response)
{
	struct d_backup_session *session;
	struct httpd_form_value *value;
	struct nx30_d_layout layout;
	struct nx30_d_extent extent;
	enum nx30_d_object object;
	const char *filebase;
	u8 digest[SHA256_SUM_LEN];
	char hex[SHA256_SUM_LEN * 2 + 1];
	unsigned int i;
	int ret;

	if (status == HTTP_CB_CLOSED) {
		session = response->session_data;
		if (session && session->kind == D_SESSION_BACKUP) {
			free(session->window);
			free(session);
			response->session_data = NULL;
		} else
			d_close_json(status, response);
		return;
	}
	if (status == HTTP_CB_RESPONDING) {
		session = response->session_data;
		if (!session) { response->status = HTTP_RESP_NONE; return; }
		if (session->phase == D_BACKUP_HEADER)
			session->phase = D_BACKUP_DATA;
		if (session->current >= session->total) {
			response->status = HTTP_RESP_NONE;
			return;
		}
		{
			size_t want = (size_t)min_t(u64, session->total - session->current,
						    D_SERVICE_WINDOW);
			size_t got = 0;
			ret = d_backup_extent_current(session->object,
						      session->generation,
						      &session->extent);
			if (ret) {
				response->status = HTTP_RESP_NONE;
				return;
			}
			ret = nx30_d_read_object(session->object, session->current,
						 session->window, want, &got);
			if (ret || got != want) {
				response->status = HTTP_RESP_NONE;
				return;
			}
			session->current += got;
			response->status = HTTP_RESP_CUSTOM;
			response->data = session->window;
			response->size = got;
			return;
		}
	}
	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		d_json_error(response, 405, "POST required");
		return;
	}
	value = httpd_request_find_value(request, "object");
	if (!value || !value->data || d_backup_object(value->data, &object, &filebase)) {
		d_json_error(response, 400, "named object required");
		return;
	}
	ret = nx30_d_layout_get(&layout);
	if (ret) { d_json_error(response, 503, "validated NX30 NAND layout unavailable"); return; }
	ret = nx30_d_object_extent(object, &extent);
	if (ret) { d_api_error(response, "backup_object_extent", ret); return; }
	ret = d_hash_object(object, extent.size, layout.generation,
			    &extent, digest);
	if (ret) { d_json_error(response, 503, "complete object read/hash failed"); return; }
	for (i = 0; i < SHA256_SUM_LEN; i++)
		snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", digest[i]);
	hex[sizeof(hex) - 1] = '\0';
	session = calloc(1, sizeof(*session));
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	session->window = malloc(D_SERVICE_WINDOW);
	if (!session->window) { free(session); d_json_error(response, 500, "no memory"); return; }
	session->object = object;
	session->generation = layout.generation;
	session->extent = extent;
	session->kind = D_SESSION_BACKUP;
	session->total = extent.size;
	session->phase = D_BACKUP_HEADER;
	snprintf(session->filename, sizeof(session->filename),
		 "nx30-%s-ecc%u-decoded.bin", filebase, layout.ecc_bits);
	session->header_len = snprintf(session->header, sizeof(session->header),
		 "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
		 "Content-Length: %llu\r\nContent-Disposition: attachment; filename=\"%s\"\r\n"
		 "Cache-Control: no-store\r\nX-NX30-Data-Format: ECC-decoded-no-OOB\r\n"
		 "X-NX30-SHA256: %s\r\nConnection: close\r\n\r\n",
		 (unsigned long long)extent.size, session->filename, hex);
	if (session->header_len <= 0 || (size_t)session->header_len >= sizeof(session->header)) {
		free(session->window); free(session); d_json_error(response, 500, "response header too long"); return;
	}
	response->session_data = session;
	response->status = HTTP_RESP_CUSTOM;
	response->data = session->header;
	response->size = session->header_len;
}

static void d_api_error(struct httpd_response *response, const char *operation,
			int ret)
{
	struct d_json_session *session = d_json_begin(response);
	int code = 503;
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	if (ret == -EOPNOTSUPP)
		code = 501;
	else if (ret == -EROFS)
		code = 423;
	else if (ret == -EINVAL || ret == -ERANGE)
		code = 400;
	else if (ret == -EPERM || ret == -EACCES)
		code = 403;
	else if (ret == -EAGAIN || ret == -EUCLEAN || ret == -ESTALE)
		code = 409;
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"error\",\"operation\":\"%s\",\"backend_errno\":%d}",
		 operation, ret);
	d_json_finish(response, session, code);
}

static size_t d_json_escape(char *out, size_t cap, const char *in)
{
	size_t used = 0;
	const unsigned char *p = (const unsigned char *)in;
	if (!cap)
		return 0;
	while (p && *p && used + 1 < cap) {
		if (*p == '"' || *p == '\\') {
			if (used + 2 >= cap) break;
			out[used++] = '\\';
			out[used++] = *p++;
		} else if (*p < 0x20) {
			if (used + 6 >= cap) break;
			used += snprintf(out + used, cap - used, "\\u%04x", *p++);
		} else {
			out[used++] = *p++;
		}
	}
	out[used] = '\0';
	return used;
}

static int d_form_u32(struct httpd_request *request, const char *name,
		      u32 *out)
{
	struct httpd_form_value *value = httpd_request_find_value(request, name);
	char *end;
	unsigned long number;
	if (!value || !value->data || !value->size || value->size > 10)
		return -EINVAL;
	number = simple_strtoul(value->data, &end, 10);
	if (end != value->data + value->size || number > 0xffffffffUL)
		return -EINVAL;
	*out = number;
	return 0;
}

static int d_form_token(struct httpd_request *request, char token[D_SERVICE_TOKEN_CHARS + 1])
{
	struct httpd_form_value *value = httpd_request_find_value(request, "token");
	size_t i;
	if (!value || !value->data || value->size != D_SERVICE_TOKEN_CHARS)
		return -EINVAL;
	for (i = 0; i < D_SERVICE_TOKEN_CHARS; i++) {
		unsigned char c = value->data[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return -EINVAL;
		token[i] = c;
	}
	token[D_SERVICE_TOKEN_CHARS] = '\0';
	return 0;
}

#define D_ENV_LINE_MAX 1024
static int d_parse_env_changes(struct httpd_request *request,
			       struct nx30_d_env_change changes[NX30_D_ENV_MAX_CHANGES],
			       size_t *count)
{
	static const char *const allowed[NX30_D_ENV_KEY_COUNT] = {
		"custom_network", "ipaddr", "netmask", "serverip", "ethact", "ethprime"
	};
	struct httpd_form_value *value = httpd_request_find_value(request, "changes");
	char buffer[D_ENV_LINE_MAX];
	char *line, *next;
	size_t used = 0, n = 0, i;
	if (!value || !value->data || !value->size || value->size >= sizeof(buffer))
		return -EINVAL;
	memcpy(buffer, value->data, value->size);
	buffer[value->size] = '\0';
	line = buffer;
	while (*line) {
		char *equals;
		while (*line == '\n' || *line == '\r') line++;
		if (!*line) break;
		next = strchr(line, '\n');
		if (next) *next++ = '\0';
		equals = strchr(line, '=');
		if (!equals || n >= NX30_D_ENV_MAX_CHANGES)
			return -EINVAL;
		*equals++ = '\0';
		if (!*line || strlen(line) >= NX30_D_ENV_NAME_LEN ||
		    strlen(equals) >= NX30_D_ENV_VALUE_LEN || strchr(equals, '\r'))
			return -EINVAL;
		for (i = 0; i < NX30_D_ENV_KEY_COUNT; i++)
			if (!strcmp(line, allowed[i])) break;
		if (i == NX30_D_ENV_KEY_COUNT)
			return -EPERM;
		for (i = 0; i < n; i++)
			if (!strcmp(changes[i].name, line)) return -EINVAL;
		strlcpy(changes[n].name, line, sizeof(changes[n].name));
		strlcpy(changes[n].value, equals, sizeof(changes[n].value));
		changes[n].unset = !*equals;
		n++;
		if (!next) break;
		line = next;
	}
	used = n;
	if (!used) return -EINVAL;
	*count = used;
	return 0;
}

static void d_network_info_handler(enum httpd_uri_handler_status status,
				   struct httpd_request *request,
				   struct httpd_response *response)
{
	static const char *const keys[NX30_D_ENV_KEY_COUNT] = {
		"custom_network", "ipaddr", "netmask", "serverip", "ethact", "ethprime"
	};
	struct d_json_session *session;
	char escaped[6][NX30_D_ENV_VALUE_LEN * 6];
	int i, used = 0;

	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_GET) {
		d_json_error(response, 405, "GET required");
		return;
	}
	for (i = 0; i < NX30_D_ENV_KEY_COUNT; i++)
		d_json_escape(escaped[i], sizeof(escaped[i]), getenv(keys[i]) ?: "");
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	used += snprintf(session->body + used, sizeof(session->body) - used,
		 "{\"ram_only\":true,\"values\":{");
	for (i = 0; i < NX30_D_ENV_KEY_COUNT; i++) {
		int n = snprintf(session->body + used, sizeof(session->body) - used,
			 "%s\"%s\":\"%s\"", i ? "," : "", keys[i], escaped[i]);
		if (n < 0 || (size_t)n >= sizeof(session->body) - used) {
			strlcpy(session->body, "{\"status\":\"error\",\"error\":\"response too large\"}", sizeof(session->body));
			d_json_finish(response, session, 500);
			return;
		}
		used += n;
	}
	used += snprintf(session->body + used, sizeof(session->body) - used, "}}");
	d_json_finish(response, session, 200);
}

static int d_ipv4_valid(const char *text)
{
	unsigned int parts = 0, octet = 0, digits = 0;
	const unsigned char *p = (const unsigned char *)text;
	if (!p || !*p) return 0;
	for (; *p; p++) {
		if (*p >= '0' && *p <= '9') {
			octet = octet * 10 + (*p - '0');
			if (++digits > 3 || octet > 255) return 0;
		} else if (*p == '.' && digits && parts < 3) {
			parts++; octet = 0; digits = 0;
		} else return 0;
	}
	return parts == 3 && digits;
}

static int d_network_value_valid(const struct nx30_d_env_change *change)
{
	if (change->unset) return 1;
	if (!strcmp(change->name, "ipaddr") || !strcmp(change->name, "netmask") ||
	    !strcmp(change->name, "serverip"))
		return d_ipv4_valid(change->value);
	if (!strcmp(change->name, "custom_network"))
		return !strcmp(change->value, "0") || !strcmp(change->value, "1");
	if (!strcmp(change->name, "ethact") || !strcmp(change->name, "ethprime")) {
		/* D service is pinned to the validated LAN management interface. */
		return !strcmp(change->value, "eth1");
	}
	return 0;
}

static void d_network_ram_handler(enum httpd_uri_handler_status status,
				  struct httpd_request *request,
				  struct httpd_response *response)
{
	struct nx30_d_env_change changes[NX30_D_ENV_MAX_CHANGES] = {};
	struct d_json_session *session;
	size_t count = 0, i;
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	ret = d_parse_env_changes(request, changes, &count);
	if (ret) { d_api_error(response, "network_change_validation", ret); return; }
	for (i = 0; i < count; i++)
		if (!d_network_value_valid(&changes[i])) { d_api_error(response, "network_value", -EINVAL); return; }
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	session->after_close = D_AFTER_NETWORK_RAM;
	session->change_count = count;
	memcpy(session->changes, changes, count * sizeof(changes[0]));
	{
		const char *new_ip = getenv("ipaddr") ?: "";
		char escaped[sizeof(session->body) / 2];
		for (i = 0; i < count; i++)
			if (!strcmp(changes[i].name, "ipaddr"))
				new_ip = changes[i].unset ? "" : changes[i].value;
		d_json_escape(escaped, sizeof(escaped), new_ip);
		snprintf(session->body, sizeof(session->body),
			 "{\"status\":\"success\",\"ram_only\":true,"
			 "\"new_management_ip\":\"%s\","
			 "\"message\":\"Network settings will apply after this response; reconnect at the displayed address.\"}",
			 escaped);
	}
	d_json_finish(response, session, 200);
}

static void d_env_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	static const char *const keys[NX30_D_ENV_KEY_COUNT] = {
		"custom_network", "ipaddr", "netmask", "serverip", "ethact", "ethprime"
	};
	struct nx30_d_env_snapshot snapshot;
	struct d_json_session *session;
	char escaped[NX30_D_ENV_KEY_COUNT][NX30_D_ENV_VALUE_LEN * 6];
	int ret, used = 0, i;

	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_GET) {
		d_json_error(response, 405, "GET required");
		return;
	}
	ret = nx30_d_env_get(&snapshot);
	if (ret) { d_api_error(response, "env_get", ret); return; }
	for (i = 0; i < NX30_D_ENV_KEY_COUNT; i++)
		d_json_escape(escaped[i], sizeof(escaped[i]), snapshot.values[i]);
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	used += snprintf(session->body + used, sizeof(session->body) - used,
		 "{\"generation\":%u,\"values\":{", snapshot.generation);
	for (i = 0; i < NX30_D_ENV_KEY_COUNT; i++) {
		int n = snprintf(session->body + used, sizeof(session->body) - used,
			 "%s\"%s\":\"%s\"", i ? "," : "", keys[i], escaped[i]);
		if (n < 0 || (size_t)n >= sizeof(session->body) - used) {
			strlcpy(session->body, "{\"status\":\"error\",\"error\":\"response too large\"}", sizeof(session->body));
			d_json_finish(response, session, 500);
			return;
		}
		used += n;
	}
	used += snprintf(session->body + used, sizeof(session->body) - used, "}}");
	d_json_finish(response, session, 200);
}

static int d_form_slot(struct httpd_request *request, enum nx30_d_slot *slot)
{
	struct httpd_form_value *value = httpd_request_find_value(request, "slot");
	if (!value || !value->data || value->size != 1)
		return -EINVAL;
	if (value->data[0] == 'A' || value->data[0] == 'a')
		*slot = NX30_D_SLOT_A;
	else if (value->data[0] == 'B' || value->data[0] == 'b')
		*slot = NX30_D_SLOT_B;
	else
		return -EINVAL;
	return 0;
}

static void d_env_prepare_handler(enum httpd_uri_handler_status status,
				  struct httpd_request *request,
				  struct httpd_response *response)
{
	struct nx30_d_env_change changes[NX30_D_ENV_MAX_CHANGES] = {};
	struct nx30_d_result result;
	struct d_json_session *session;
	char token[D_SERVICE_TOKEN_CHARS + 1] = {};
	size_t count = 0;
	u32 generation;
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	ret = d_parse_env_changes(request, changes, &count);
	if (ret) { d_api_error(response, "env_change_validation", ret); return; }
	ret = d_form_u32(request, "generation", &generation);
	if (ret) { d_api_error(response, "env_generation", ret); return; }
	memset(&result, 0, sizeof(result));
	ret = nx30_d_env_prepare(changes, count, generation, token, &result);
	if (ret) { d_api_error(response, "env_prepare", ret); return; }
	token[D_SERVICE_TOKEN_CHARS] = '\0';
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"token\":\"%s\","
		 "\"generation\":%u,\"crc_before\":%u,\"summary\":"
		 "\"Network settings passed the protected ENV preflight.\"}",
		 token, result.generation, result.crc_before);
	d_json_finish(response, session, 200);
}

static void d_env_commit_handler(enum httpd_uri_handler_status status,
				 struct httpd_request *request,
				 struct httpd_response *response)
{
	struct nx30_d_result result;
	struct d_json_session *session;
	char token[D_SERVICE_TOKEN_CHARS + 1];
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	ret = d_form_token(request, token);
	if (ret) { d_api_error(response, "env_token", ret); return; }
	memset(&result, 0, sizeof(result));
	ret = nx30_d_env_commit(token, &result);
	if (ret) { d_api_error(response, "env_commit", ret); return; }
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"message\":"
		 "\"Saved network settings; full ENV readback passed.\","
		 "\"generation\":%u,\"crc_before\":%u,\"crc_after\":%u,"
		 "\"bytes_verified\":%u}", result.generation, result.crc_before,
		 result.crc_after, result.bytes_verified);
	d_json_finish(response, session, 200);
}

static void d_env_abort_handler(enum httpd_uri_handler_status status,
				struct httpd_request *request,
				struct httpd_response *response)
{
	char token[D_SERVICE_TOKEN_CHARS + 1];
	struct d_json_session *session;
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	ret = d_form_token(request, token);
	if (ret) { d_api_error(response, "env_token", ret); return; }
	nx30_d_env_abort(token);
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	strlcpy(session->body, "{\"status\":\"success\",\"message\":\"Preflight cancelled.\"}", sizeof(session->body));
	d_json_finish(response, session, 200);
}

static void d_default_abort_handler(enum httpd_uri_handler_status status,
                                    struct httpd_request *request,
                                    struct httpd_response *response)
{
	struct d_json_session *session;
	char token[D_SERVICE_TOKEN_CHARS + 1];
	int ret;

	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) {
		d_json_error(response, 405, "POST required");
		return;
	}
	ret = d_form_token(request, token);
	if (ret) { d_api_error(response, "default_token", ret); return; }
	ret = nx30_d_default_abort(token);
	if (ret) { d_api_error(response, "default_abort", ret); return; }
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	strlcpy(session->body,
		"{\"status\":\"success\",\"message\":\"Default-slot preflight cancelled.\"}",
		sizeof(session->body));
	d_json_finish(response, session, 200);
}

static void d_default_prepare_handler(enum httpd_uri_handler_status status,
				      struct httpd_request *request,
				      struct httpd_response *response)
{
	struct d_json_session *session;
	struct nx30_d_result result;
	char token[D_SERVICE_TOKEN_CHARS + 1] = {};
	enum nx30_d_slot slot;
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	ret = d_form_slot(request, &slot);
	if (ret) { d_api_error(response, "default_slot", ret); return; }
	memset(&result, 0, sizeof(result));
	ret = nx30_d_default_prepare(slot, token, &result);
	if (ret) { d_api_error(response, "default_prepare", ret); return; }
	token[D_SERVICE_TOKEN_CHARS] = '\0';
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"token\":\"%s\","
		 "\"summary\":\"Both BOOTCONFIG copies passed preflight; AGE is preserved.\"}", token);
	d_json_finish(response, session, 200);
}

/* Read-only diagnosis and the existing backup stream remain usable when the
 * on-flash peers differ; they do not call slots_get or open a write window. */
static void d_default_failure_handler(enum httpd_uri_handler_status status,
                                     struct httpd_request *request,
                                     struct httpd_response *response)
{
 struct d_json_session *session;
 struct nx30_d_result result;
 u32 copy;
 int ret;
 if (status==HTTP_CB_CLOSED) { d_close_json(status,response); return; }
 if (status!=HTTP_CB_NEW) return;
 if (!request || request->method!=HTTP_GET) { d_json_error(response,405,"GET required"); return; }
 ret=nx30_d_default_failure_get(&result,&copy);
 if (ret) { d_api_error(response,"default_failure",ret); return; }
 session=d_json_begin(response);
 if (!session) { d_json_error(response,500,"no memory"); return; }
 snprintf(session->body,sizeof(session->body),
  "{\"status\":\"recovery_required\",\"backend_errno\":%d,\"stage\":%u,\"copy\":%u,"
  "\"bytes_verified\":%u,\"backup_endpoint\":\"/service/backup\","
  "\"backup_objects\":[\"bc-original0\",\"bc-original1\"],"
  "\"message\":\"POST object to backup endpoint to export each original. RAM backups survive until reboot; new default transactions are blocked.\"}",
  result.status,result.stage,copy,result.bytes_verified);
 d_json_finish(response,session,200);
}

static void d_default_commit_handler(enum httpd_uri_handler_status status,
				     struct httpd_request *request,
				     struct httpd_response *response)
{
	struct d_json_session *session;
	struct nx30_d_result result;
	char token[D_SERVICE_TOKEN_CHARS + 1];
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	ret = d_form_token(request, token);
	if (ret) { d_api_error(response, "default_token", ret); return; }
	memset(&result, 0, sizeof(result));
	ret = nx30_d_default_commit(token, &result);
	if (ret) {
		u32 copy;
		if (!nx30_d_default_failure_get(&result,&copy)) {
			d_default_failure_handler(HTTP_CB_NEW, &(struct httpd_request){ .method=HTTP_GET }, response);
			response->info.code=409;
		} else d_api_error(response,"default_commit",ret);
		return;
	}
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"message\":"
		 "\"Default slot saved; both copies passed full readback.\","
		 "\"selector\":%u,\"age\":%u,\"bytes_verified\":%u}",
		 result.selector, result.age, result.bytes_verified);
	d_json_finish(response, session, 200);
}

static void d_firmware_handler(enum httpd_uri_handler_status status,
			       struct httpd_request *request,
			       struct httpd_response *response, bool write)
{
	struct httpd_form_value *file;
	struct d_json_session *session;
	struct d_pending_update *pending;
	struct nx30_d_firmware_info info;
	struct nx30_d_result result;
	char token[D_SERVICE_TOKEN_CHARS + 1];
	char sha_hex[SHA256_SUM_LEN * 2 + 1];
	u8 digest[SHA256_SUM_LEN];
	unsigned int i;
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status == HTTP_CB_NEW) {
		if (!d_upload_begin(response)) d_json_error(response, 500, "no memory");
		return;
	}
	if (status != HTTP_CB_RESPONDING) return;
	session = response->session_data;
	if (!session || session->kind != D_SESSION_JSON) { response->status = HTTP_RESP_NONE; return; }
	if (session->body_sent) { response->status = HTTP_RESP_NONE; return; }
	if (!write)
		d_pending_updates[0].valid = false;
	if (!request || request->method != HTTP_POST) { d_upload_error(response, "firmware_request", -EINVAL); return; }
	file = httpd_request_find_value(request, "image");
	if (!file || !file->data || !file->size) { d_upload_error(response, "firmware_image", -EINVAL); return; }
	if (file->size > 0x02800000U) { d_upload_error(response, "firmware_image_size", -EFBIG); return; }
	if (!write) {
		memset(&info, 0, sizeof(info));
		ret = nx30_d_firmware_validate(file->data, file->size, &info);
		if (ret) { d_upload_error(response, "firmware_validate", ret); return; }
		if (info.image_bytes != file->size) { d_upload_error(response, "firmware_image_length", -EUCLEAN); return; }
		ret = d_pending_update_store(0, file->data, file->size, &info);
		if (ret) { d_upload_error(response, "firmware_candidate", ret); return; }
		d_sha256_buffer(file->data, file->size, digest);
		for (i = 0; i < SHA256_SUM_LEN; i++)
			snprintf(sha_hex + i * 2, sizeof(sha_hex) - i * 2, "%02x", digest[i]);
		sha_hex[sizeof(sha_hex) - 1] = '\0';
		snprintf(session->body, sizeof(session->body),
			 "{\"status\":\"success\",\"generation\":%u,"
			 "\"image_crc32\":%u,\"image_bytes\":%u,\"peb_count\":%u,"
			 "\"sha256\":\"%s\",\"token\":\"%s\","
			 "\"target_slot\":\"%c\",\"release\":\"%s\"}",
			 info.generation, info.image_crc32, info.image_bytes,
			 info.peb_count, sha_hex, d_pending_updates[0].token,
			 info.target == NX30_D_SLOT_A ? 'A' : 'B', info.release);
		d_upload_finish(response, session);
		return;
	}
	ret = d_form_token(request, token);
	if (ret) { d_upload_error(response, "firmware_token", ret); return; }
	pending = d_pending_update_get(0, token, file->data, file->size);
	if (!pending) { d_upload_error(response, "firmware_candidate_match", -ESTALE); return; }
	/* Consume before the fixed write window can open; never retry a failed write. */
	pending->valid = false;
	memset(&result, 0, sizeof(result));
	ret = nx30_d_firmware_write_inactive(file->data, file->size,
						     pending->generation, pending->image_crc32, &result);
	if (ret) { d_upload_error(response, "firmware_write", ret); return; }
#if defined(HWD_SERVICE_HOST_TEST) || defined(CONFIG_H3C_IPQ5018_COMMON)
	slots_rt3000_b_invalidate_cache();
#endif
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"message\":"
		 "\"System image installed in the inactive physical slot; default slot unchanged.\","
		 "\"target_offset\":%llu,\"target_size\":%llu,"
		 "\"bytes_verified\":%u}", (unsigned long long)result.target_offset,
		 (unsigned long long)result.target_size, result.bytes_verified);
	d_upload_finish(response, session);
}

static void d_firmware_validate_handler(enum httpd_uri_handler_status status,
					struct httpd_request *request,
					struct httpd_response *response)
{
	d_firmware_handler(status, request, response, false);
}

static void d_firmware_write_handler(enum httpd_uri_handler_status status,
				     struct httpd_request *request,
				     struct httpd_response *response)
{
	d_firmware_handler(status, request, response, true);
}

static void d_uboot_handler(enum httpd_uri_handler_status status,
			    struct httpd_request *request,
			    struct httpd_response *response, bool write)
{
	struct httpd_form_value *file;
	struct d_json_session *session;
	struct d_pending_update *pending;
	struct nx30_d_firmware_info info;
	struct nx30_d_result result;
	char token[D_SERVICE_TOKEN_CHARS + 1];
	char sha_hex[SHA256_SUM_LEN * 2 + 1];
	u8 digest[SHA256_SUM_LEN];
	unsigned int i;
	int ret;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status == HTTP_CB_NEW) {
		if (!d_upload_begin(response)) d_json_error(response, 500, "no memory");
		return;
	}
	if (status != HTTP_CB_RESPONDING) return;
	session = response->session_data;
	if (!session || session->kind != D_SESSION_JSON) { response->status = HTTP_RESP_NONE; return; }
	if (session->body_sent) { response->status = HTTP_RESP_NONE; return; }
	if (!write)
		d_pending_updates[1].valid = false;
	if (!request || request->method != HTTP_POST) { d_upload_error(response, "uboot_request", -EINVAL); return; }
	file = httpd_request_find_value(request, "image");
	if (!file || !file->data || !file->size) { d_upload_error(response, "uboot_image", -EINVAL); return; }
	if (file->size > 0x00140000U) { d_upload_error(response, "uboot_image_size", -EFBIG); return; }
	if (!write) {
		memset(&info, 0, sizeof(info));
		ret = nx30_d_uboot_validate(file->data, file->size, &info);
		if (ret) { d_upload_error(response, "uboot_validate", ret); return; }
		if (info.image_bytes != file->size) { d_upload_error(response, "uboot_image_length", -EUCLEAN); return; }
		ret = d_pending_update_store(1, file->data, file->size, &info);
		if (ret) { d_upload_error(response, "uboot_candidate", ret); return; }
		d_sha256_buffer(file->data, file->size, digest);
		for (i = 0; i < SHA256_SUM_LEN; i++)
			snprintf(sha_hex + i * 2, sizeof(sha_hex) - i * 2, "%02x", digest[i]);
		sha_hex[sizeof(sha_hex) - 1] = '\0';
		snprintf(session->body, sizeof(session->body),
			 "{\"status\":\"success\",\"generation\":%u,"
			 "\"image_crc32\":%u,\"image_bytes\":%u,"
			 "\"sha256\":\"%s\",\"token\":\"%s\","
			 "\"release\":\"%s\",\"target\":\"active APPSBL copy\"}",
			 info.generation, info.image_crc32, info.image_bytes, sha_hex,
			 d_pending_updates[1].token, info.release);
		d_upload_finish(response, session);
		return;
	}
	ret = d_form_token(request, token);
	if (ret) { d_upload_error(response, "uboot_token", ret); return; }
	pending = d_pending_update_get(1, token, file->data, file->size);
	if (!pending) { d_upload_error(response, "uboot_candidate_match", -ESTALE); return; }
	pending->valid = false;
	memset(&result, 0, sizeof(result));
	ret = nx30_d_uboot_write(file->data, file->size,
				 pending->generation, pending->image_crc32, &result);
	if (ret) { d_upload_error(response, "uboot_write", ret); return; }
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"message\":"
		 "\"active APPSBL copy passed complete readback; BOOTCONFIG was preserved.\","
		 "\"target_offset\":%llu,\"target_size\":%llu,"
		 "\"bytes_verified\":%u}", (unsigned long long)result.target_offset,
		 (unsigned long long)result.target_size, result.bytes_verified);
	d_upload_finish(response, session);
}

static void d_uboot_validate_handler(enum httpd_uri_handler_status status,
				    struct httpd_request *request,
				    struct httpd_response *response)
{
	d_uboot_handler(status, request, response, false);
}

static void d_uboot_write_handler(enum httpd_uri_handler_status status,
				   struct httpd_request *request,
				   struct httpd_response *response)
{
	d_uboot_handler(status, request, response, true);
}

static void d_boot_slot_handler(enum httpd_uri_handler_status status,
				struct httpd_request *request,
				struct httpd_response *response)
{
	struct httpd_form_value *value;
	struct d_json_session *session;
	struct nx30_d_slots slots;
	enum nx30_d_slot slot;
	int ret;

	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		d_json_error(response, 405, "POST required");
		return;
	}
	value = httpd_request_find_value(request, "slot");
	if (!value || !value->data) { d_json_error(response, 400, "slot A or B required"); return; }
	ret = d_form_slot(request, &slot);
	if (ret) { d_api_error(response, "boot_slot", ret); return; }
	ret = nx30_d_slots_get(&slots);
	if (ret) { d_api_error(response, "boot_slot_status", ret); return; }
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	session->after_close = D_AFTER_BOOT_SLOT;
	session->slot = slot;
	snprintf(session->body, sizeof(session->body),
		 "{\"status\":\"success\",\"message\":"
		 "\"Starting the selected physical slot once.\"}");
	d_json_finish(response, session, 200);
}

static void d_led_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	struct httpd_form_value *value;
	struct d_json_session *session;
	const char *mode;

	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	value = httpd_request_find_value(request, "mode");
	if (!value || !value->data) { d_json_error(response, 400, "LED mode required"); return; }
	mode = value->data;
	if (!strcmp(mode, "green")) { led_off("power_led"); led_on("system_led"); }
	else if (!strcmp(mode, "red")) { led_off("system_led"); led_on("power_led"); }
	else if (!strcmp(mode, "off")) { led_off("system_led"); led_off("power_led"); }
	else { d_json_error(response, 400, "mode must be green, red, or off"); return; }
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	strlcpy(session->body, "{\"status\":\"success\",\"message\":\"Indicator updated.\"}", sizeof(session->body));
	d_json_finish(response, session, 200);
}

static void d_buttons_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	struct d_json_session *session;
	int reset_level, wps_level;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_GET) { d_json_error(response, 405, "GET required"); return; }
	/* DTS configures these pins as pulled-up inputs; sampling does not alter TLMM. */
	reset_level = gpio_get_value(34);
	wps_level = gpio_get_value(38);
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	snprintf(session->body, sizeof(session->body),
		 "{\"read_only\":true,\"reset\":{\"gpio\":34,\"level\":%d,\"pressed\":%s},"
		 "\"wps\":{\"gpio\":38,\"level\":%d,\"pressed\":%s}}",
		 reset_level, reset_level ? "false" : "true",
		 wps_level, wps_level ? "false" : "true");
	d_json_finish(response, session, 200);
}

static void d_console_handler(enum httpd_uri_handler_status status,
			     struct httpd_request *request,
			     struct httpd_response *response)
{
	static const struct { const char *name; const char *command; } commands[] = {
		{ "version", "version" }, { "bdinfo", "bdinfo" },
		{ "nand-info", "nand info" }, { "mtd-list", "mtd list" },
	};
	struct httpd_form_value *value;
	struct d_json_session *session;
	unsigned int i;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	value = httpd_request_find_value(request, "cmd");
	if (!value || !value->data || value->size > 16) { d_json_error(response, 400, "select a read-only status command"); return; }
	for (i = 0; i < ARRAY_SIZE(commands); i++)
		if (value->size == strlen(commands[i].name) && !memcmp(value->data, commands[i].name, value->size)) break;
	if (i == ARRAY_SIZE(commands)) { d_json_error(response, 403, "command is not in the read-only allowlist"); return; }
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	session->after_close = D_AFTER_CONSOLE;
	session->command = commands[i].command;
	strlcpy(session->body, "{\"status\":\"success\",\"message\":\"Status command queued; read its output in the log panel.\"}", sizeof(session->body));
	d_json_finish(response, session, 200);
}

static void d_reboot_handler(enum httpd_uri_handler_status status,
			     struct httpd_request *request,
			     struct httpd_response *response)
{
	struct d_json_session *session;
	if (status == HTTP_CB_CLOSED) { d_close_json(status, response); return; }
	if (status != HTTP_CB_NEW) return;
	if (!request || request->method != HTTP_POST) { d_json_error(response, 405, "POST required"); return; }
	session = d_json_begin(response);
	if (!session) { d_json_error(response, 500, "no memory"); return; }
	session->after_close = D_AFTER_RESET;
	strlcpy(session->body, "{\"status\":\"success\",\"message\":\"Restart requested.\"}", sizeof(session->body));
	d_json_finish(response, session, 200);
}

void service_hwd_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/service/status", d_status_handler, NULL);
	httpd_register_uri_handler(inst, "/service/buttons", d_buttons_handler, NULL);
	httpd_register_uri_handler(inst, "/service/slots", d_slots_handler, NULL);
	httpd_register_uri_handler(inst, "/service/env", d_env_handler, NULL);
	httpd_register_uri_handler(inst, "/service/boot/slot", d_boot_slot_handler, NULL);
	httpd_register_uri_handler(inst, "/service/backup", d_backup_handler, NULL);
	httpd_register_uri_handler(inst, "/service/led", d_led_handler, NULL);
	httpd_register_uri_handler(inst, "/service/env/prepare", d_env_prepare_handler, NULL);
	httpd_register_uri_handler(inst, "/service/env/commit", d_env_commit_handler, NULL);
	httpd_register_uri_handler(inst, "/service/env/abort", d_env_abort_handler, NULL);
	httpd_register_uri_handler(inst, "/service/network/info", d_network_info_handler, NULL);
	httpd_register_uri_handler(inst, "/service/network/ram", d_network_ram_handler, NULL);
	httpd_register_uri_handler(inst, "/service/boot/default/prepare", d_default_prepare_handler, NULL);
	httpd_register_uri_handler(inst, "/service/boot/default/failure", d_default_failure_handler, NULL);
	httpd_register_uri_handler(inst, "/service/boot/default/commit", d_default_commit_handler, NULL);
	httpd_register_uri_handler(inst, "/service/boot/default/abort", d_default_abort_handler, NULL);
	httpd_register_uri_handler(inst, "/service/firmware/validate", d_firmware_validate_handler, NULL);
	httpd_register_uri_handler(inst, "/service/firmware/write", d_firmware_write_handler, NULL);
	httpd_register_uri_handler(inst, "/service/uboot/validate", d_uboot_validate_handler, NULL);
	httpd_register_uri_handler(inst, "/service/uboot/write", d_uboot_write_handler, NULL);
	httpd_register_uri_handler(inst, "/service/reboot", d_reboot_handler, NULL);
	httpd_register_uri_handler(inst, "/service/console", d_console_handler, NULL);
}
