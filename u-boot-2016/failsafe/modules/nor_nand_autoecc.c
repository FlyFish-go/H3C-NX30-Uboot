/* SPDX-License-Identifier: GPL-2.0 */
#include <common.h>
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
#include <nand.h>
#include <h3c_nand_ecc.h>
#endif
#include <asm/errno.h>
#include <command.h>
#include <malloc.h>
#include <net/httpd.h>
#include <nx30_nor_nand.h>
#include <nx30_nor_autoecc.h>
#include "modules.h"
#include "nor_nand_autoecc_core.h"

#define NAND_AUTOECC_RETRY_HEADER "X-NX30-NAND-Probe-Retry"
#define NAND_AUTOECC_MAX_RETRIES 2U
#define NAND_AUTOECC_HTTP_BODY_SIZE 4096U
#define NAND_AUTOECC_OOB_CAPACITY 128U

struct nand_autoecc_http_session {
	char body[NAND_AUTOECC_HTTP_BODY_SIZE];
};

static struct nx30_nor_autoecc_result cached_result;
static bool probe_cached;
static u32 retry_count;

static int adapter_init_once(void *ctx)
{
	(void)ctx;
	return nx30_nor_nand_init_once();
}

static int adapter_set_ecc(void *ctx, unsigned int bits)
{
	(void)ctx;
	return nx30_nor_nand_set_read_ecc(bits);
}

static int adapter_read_page(void *ctx, u64 offset, void *data, u32 capacity,
			     struct nx30_nor_autoecc_page_result *result)
{
	u8 oob[NAND_AUTOECC_OOB_CAPACITY];
	struct nx30_nor_nand_read_result driver_result;
	int ret;

	(void)ctx;
	memset(&driver_result, 0, sizeof(driver_result));
	ret = nx30_nor_nand_read_page(offset, data, capacity, oob, sizeof(oob),
				      &driver_result);
	result->status = driver_result.status;
	result->data_len = driver_result.data_len;
	result->failed = driver_result.failed;
	result->corrected = driver_result.corrected;
	return ret;
}

static int adapter_restore4(void *ctx)
{
	(void)ctx;
	return nx30_nor_nand_restore4();
}

static u32 adapter_timer_ms(void *ctx, u32 base)
{
	(void)ctx;
	return (u32)get_timer(base);
}

static const struct nx30_nor_autoecc_ops autoecc_ops = {
	.init_once = adapter_init_once,
	.set_read_ecc = adapter_set_ecc,
	.read_page = adapter_read_page,
	.restore4 = adapter_restore4,
	.get_timer_ms = adapter_timer_ms,
};

static const char *state_name(u32 state)
{
	switch (state) {
	case NX30_NOR_AUTOECC_STATE_ECC4: return "ecc4";
	case NX30_NOR_AUTOECC_STATE_ECC8: return "ecc8";
	case NX30_NOR_AUTOECC_STATE_MIXED: return "mixed";
	default: return "unknown";
	}
}

static const char *reason_name(enum nx30_nor_autoecc_reason reason)
{
	switch (reason) {
	case NX30_NOR_AUTOECC_REASON_NONE: return "none";
	case NX30_NOR_AUTOECC_REASON_INIT_FAILED: return "nand_init_failed";
	case NX30_NOR_AUTOECC_REASON_ECC4_SWITCH_FAILED: return "ecc4_switch_failed";
	case NX30_NOR_AUTOECC_REASON_ECC8_SWITCH_FAILED: return "ecc8_switch_failed";
	case NX30_NOR_AUTOECC_REASON_READ_FAILED: return "nand_read_failed";
	case NX30_NOR_AUTOECC_REASON_SHORT_READ: return "incomplete_page_read";
	case NX30_NOR_AUTOECC_REASON_TIMEOUT: return "scan_timeout";
	case NX30_NOR_AUTOECC_REASON_RESTORE_FAILED: return "ecc4_restore_failed";
	case NX30_NOR_AUTOECC_REASON_NO_UNIQUE_MODE: return "insufficient_unique_samples";
	case NX30_NOR_AUTOECC_REASON_AMBIGUOUS_BLOCK: return "same_block_valid_in_both_modes";
	case NX30_NOR_AUTOECC_REASON_MIXED_BLOCKS: return "different_blocks_valid_in_both_modes";
	default: return "unknown";
	}
}

static void cache_make_unknown(enum nx30_nor_autoecc_reason reason, int error)
{
	cached_result.state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
	cached_result.selected_read_ecc = 0;
	cached_result.reason = reason;
	cached_result.error = error;
}

static void reconcile_cached_mode(void)
{
	int ret;

	if (cached_result.state == NX30_NOR_AUTOECC_STATE_ECC4 ||
	    cached_result.state == NX30_NOR_AUTOECC_STATE_ECC8) {
		ret = nx30_nor_nand_set_read_ecc(cached_result.selected_read_ecc);
		if (!ret)
			return;
		cache_make_unknown(cached_result.selected_read_ecc == 4 ?
				   NX30_NOR_AUTOECC_REASON_ECC4_SWITCH_FAILED :
				   NX30_NOR_AUTOECC_REASON_ECC8_SWITCH_FAILED, ret);
	}
	ret = nx30_nor_nand_restore4();
	if (ret)
		cache_make_unknown(NX30_NOR_AUTOECC_REASON_RESTORE_FAILED, ret);
}

int nx30_nor_nand_autoecc_get_result(bool retry,
				     struct nx30_nor_autoecc_result *out)
{
	int ret;
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
	struct mtd_info *mtd;
	struct mtd_ecc_stats saved_stats;
	unsigned int fixed_mode;
	int profile_kind;
#endif

	if (!out)
		return -EINVAL;
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
	profile_kind = h3c_nand_ecc_autoecc_profile_mode(&fixed_mode);
	if (profile_kind < 0)
		return -ENODEV;
	if (profile_kind > 0) {
		nx30_nor_autoecc_fixed_result(out, fixed_mode);
		return 0;
	}
	mtd = &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
	saved_stats = mtd->ecc_stats;
#endif
	if (probe_cached && retry && retry_count >= NAND_AUTOECC_MAX_RETRIES) {
		*out = cached_result;
		out->retry_limit_reached = 1;
		reconcile_cached_mode();
		*out = cached_result;
		out->retry_limit_reached = 1;
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
        h3c_nand_ecc_probe_complete(out->selected_read_ecc);
        mtd->ecc_stats = saved_stats;
#endif
		return 0;
	}
	if (!probe_cached || retry) {
		if (probe_cached && retry)
			retry_count++;
		ret = nx30_nor_autoecc_probe(&autoecc_ops, NULL, &cached_result);
		probe_cached = true;
		if (ret) {
			cache_make_unknown(NX30_NOR_AUTOECC_REASON_INIT_FAILED, ret);
			nx30_nor_nand_restore4();
		}
	} else {
		reconcile_cached_mode();
	}
	*out = cached_result;
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
    h3c_nand_ecc_probe_complete(out->selected_read_ecc);
    mtd->ecc_stats = saved_stats;
#endif
	return 0;
}

static int json_append(char *buf, u32 capacity, u32 *used, const char *fmt, ...)
{
	va_list args;
	int n;

	if (*used >= capacity)
		return -ENOSPC;
	va_start(args, fmt);
	n = vsnprintf(buf + *used, capacity - *used, fmt, args);
	va_end(args);
	if (n < 0 || (u32)n >= capacity - *used)
		return -ENOSPC;
	*used += n;
	return 0;
}

static int format_json(char *buf, u32 capacity,
		       const struct nx30_nor_autoecc_result *r)
{
	u32 used = 0, i;
	int ret;

	ret = json_append(buf, capacity, &used,
		"{\"status\":\"%s\",\"method\":\"%s\","
		"\"scope\":\"%s\",\"selected_read_ecc\":%u,"
		"\"samples_ecc4\":%u,\"samples_ecc8\":%u,"
		"\"valid_ecc4\":%u,\"valid_ecc8\":%u,"
		"\"ecc_failed_ecc4\":%u,\"ecc_failed_ecc8\":%u,"
		"\"invalid_headers_ecc4\":%u,\"invalid_headers_ecc8\":%u,"
		"\"ambiguous_blocks\":%u,\"error\":%d,"
		"\"reason\":\"%s\",\"reason_code\":%u,"
		"\"retry_limit_reached\":%u,\"scan_first_offset\":0,"
		"\"scan_stride\":%u,\"scan_last_offset\":%u,"
		"\"scan_total_offsets\":%u,\"evidence_ecc4\":[",
		state_name(r->state),
		r->fixed_profile_mode ? "catalog_fixed_mode" : "dual_ecc_sample",
		r->fixed_profile_mode ? "exact_profile_env_and_system_ubi" :
		"sampled_system_ubi_metadata_only",
		r->selected_read_ecc, r->samples_ecc4,
		r->samples_ecc8, r->valid_ecc4, r->valid_ecc8,
		r->ecc_failed_ecc4, r->ecc_failed_ecc8,
		r->invalid_headers_ecc4, r->invalid_headers_ecc8,
		r->ambiguous_blocks, r->error, reason_name(r->reason),
		(unsigned int)r->reason, r->retry_limit_reached,
		r->fixed_profile_mode ? 0U : NX30_NOR_AUTOECC_STRIDE,
		r->fixed_profile_mode ? 0U :
		(NX30_NOR_AUTOECC_SAMPLE_COUNT - 1) * NX30_NOR_AUTOECC_STRIDE,
		r->fixed_profile_mode ? 0U : NX30_NOR_AUTOECC_SAMPLE_COUNT);
	if (ret)
		return ret;
	for (i = 0; i < r->evidence_ecc4_count; i++) {
		ret = json_append(buf, capacity, &used, "%s\"0x%08x\"",
				  i ? "," : "", (unsigned int)r->evidence_ecc4[i]);
		if (ret)
			return ret;
	}
	ret = json_append(buf, capacity, &used, "],\"evidence_ecc8\":[");
	if (ret)
		return ret;
	for (i = 0; i < r->evidence_ecc8_count; i++) {
		ret = json_append(buf, capacity, &used, "%s\"0x%08x\"",
				  i ? "," : "", (unsigned int)r->evidence_ecc8[i]);
		if (ret)
			return ret;
	}
	return json_append(buf, capacity, &used, "]}");
}

static void nor_nand_probe_handler(enum httpd_uri_handler_status status,
				  struct httpd_request *request,
				  struct httpd_response *response)
{
	struct nand_autoecc_http_session *session;
	struct nx30_nor_autoecc_result result;
	const char *retry_header;
	bool retry = false;
	int ret;

	if (status == HTTP_CB_CLOSED) {
		if (response->session_data) {
			free(response->session_data);
			response->session_data = NULL;
		}
		return;
	}
	if (status != HTTP_CB_NEW)
		return;
	session = calloc(1, sizeof(*session));
	response->session_data = session;
	response->status = HTTP_RESP_STD;
	response->info.content_type = "application/json";
	response->info.connection_close = 1;
	if (!session) {
		response->info.code = 500;
		response->data = "{\"status\":\"unknown\",\"reason\":\"out_of_memory\"}";
		response->size = strlen(response->data);
		return;
	}
	response->info.code = 200;
#ifdef CONFIG_H3C_HW_D_RAM_AUTOECC
	if (!request || request->method != HTTP_GET || request->form.count) {
#else
	if (!request || request->method != HTTP_POST || request->form.count) {
#endif
		snprintf(session->body, sizeof(session->body),
			 "{\"status\":\"unknown\",\"reason\":\"probe_requires_empty_POST\"}");
	} else {
		retry_header = httpd_find_header(NAND_AUTOECC_RETRY_HEADER);
		if (retry_header && !strcmp(retry_header, "1"))
			retry = true;
		ret = nx30_nor_nand_autoecc_get_result(retry, &result);
		if (ret) {
			response->info.code = 500;
			{
				int error = ret;
				snprintf(session->body, sizeof(session->body),
					 "{\"status\":\"unknown\",\"error\":%d,"
					 "\"reason\":\"probe_failed\"}", error);
			}
		} else if (format_json(session->body, sizeof(session->body), &result)) {
			response->info.code = 500;
			strlcpy(session->body,
				"{\"status\":\"unknown\",\"reason\":\"response_overflow\"}",
				sizeof(session->body));
		}
	}
	response->data = session->body;
	response->size = strlen(session->body);
}

void nor_nand_autoecc_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/nand/probe", &nor_nand_probe_handler, NULL);
}

static void print_probe_result(const struct nx30_nor_autoecc_result *r)
{
	u32 i;

	if (r->fixed_profile_mode) {
		printf("NAND ECC fixed profile: mode=%u; ENV CRC and system UBI metadata validated; no dual-ECC scan performed.\n",
		       r->selected_read_ecc);
		return;
	}
	printf("NAND ECC sample status: %s; mode=%u; ECC4-valid=%u ECC8-valid=%u; ",
	       state_name(r->state), r->selected_read_ecc,
	       r->valid_ecc4, r->valid_ecc8);
	printf("samples=%u/%u; reason=%s (%d)\n",
	       r->samples_ecc4, r->samples_ecc8,
	       reason_name(r->reason), r->error);
	puts("ECC4 evidence offsets:");
	for (i = 0; i < r->evidence_ecc4_count; i++)
		printf(" 0x%08x", (unsigned int)r->evidence_ecc4[i]);
	puts("\nECC8 evidence offsets:");
	for (i = 0; i < r->evidence_ecc8_count; i++)
		printf(" 0x%08x", (unsigned int)r->evidence_ecc8[i]);
	puts("\nSampled physical pages: 128 offsets from 0x00000000 through 0x07f00000, stride 0x00100000.\n");
	puts("Sampled headers do not establish uniform ECC across all NAND regions.\n");
}

int nx30_nor_nand_autoecc_cli(void)
{
	struct nx30_nor_autoecc_result result;
	int ret = nx30_nor_nand_autoecc_get_result(false, &result);

	if (ret) {
		printf("NAND ECC sampling failed: %d\n", ret);
		return CMD_RET_FAILURE;
	}
	print_probe_result(&result);
	return result.selected_read_ecc == NX30_NOR_AUTOECC_ECC4 ||
	       result.selected_read_ecc == NX30_NOR_AUTOECC_ECC8 ?
		CMD_RET_SUCCESS : CMD_RET_FAILURE;
}

unsigned int nx30_nor_nand_autoecc_mode(void)
{
 struct nx30_nor_autoecc_result r;
 if (nx30_nor_nand_autoecc_get_result(false, &r)) return 0;
 return (r.state == NX30_NOR_AUTOECC_STATE_ECC4 || r.state == NX30_NOR_AUTOECC_STATE_ECC8) ? r.selected_read_ecc : 0;
}
