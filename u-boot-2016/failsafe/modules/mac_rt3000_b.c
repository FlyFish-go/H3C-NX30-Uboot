// SPDX-License-Identifier: GPL-2.0
/* RT3000-B MAC controls only U-Boot ethaddr in verified APPSBLENV. */
#include <common.h>
#include <asm/errno.h>
#include <malloc.h>
#include <net.h>
#include <net/httpd.h>

#include "modules.h"

static void mac_response(struct httpd_response *response, int code,
		const char *data, const char *content_type)
{
	response->status = HTTP_RESP_STD;
	response->data = data;
	response->size = strlen(data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = content_type;
}

static int mac_hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_unicast_mac(const unsigned char *text, size_t size, uchar mac[6])
{
	int i, hi, lo;
	int any = 0;

	if (!text || size != 17 || !mac)
		return -EINVAL;
	for (i = 0; i < 6; i++) {
		int pos = i * 3;

		hi = mac_hex(text[pos]);
		lo = mac_hex(text[pos + 1]);
		if (hi < 0 || lo < 0 || (i < 5 && text[pos + 2] != ':'))
			return -EINVAL;
		mac[i] = (hi << 4) | lo;
		any |= mac[i];
	}
	/* Reject multicast, broadcast, and the all-zero address. */
	if (!any || (mac[0] & 1))
		return -EINVAL;
	return 0;
}

static void mac_info_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request, struct httpd_response *response)
{
	uchar mac[6];
	char *buf;
	int len;

	if (status == HTTP_CB_CLOSED && response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
		return;
	}
	if (status != HTTP_CB_NEW)
		return;
	response->session_data = NULL;
	if (!request || request->method != HTTP_GET) {
		mac_response(response, 405, "bad method", "text/plain");
		return;
	}
	/* Report the RAM-loaded ethaddr. Each accepted change is readback-verified. */
	if (!eth_getenv_enetaddr("ethaddr", mac)) {
		mac_response(response, 500, "valid ethaddr unavailable", "text/plain");
		return;
	}
	buf = malloc(320);
	if (!buf) {
		mac_response(response, 500, "no mem", "text/plain");
		return;
	}
	len = snprintf(buf, 320,
		"{\"part_name\":\"0:APPSBLENV\","
		"\"variable\":\"ethaddr\","
		"\"macs\":[\"%02X:%02X:%02X:%02X:%02X:%02X\"],"
		"\"source\":\"uboot_environment\","
		"\"persistent\":true,\"writable\":true}",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	if (len < 0 || len >= 320) {
		free(buf);
		mac_response(response, 500, "response too large", "text/plain");
		return;
	}
	mac_response(response, 200, buf, "application/json");
	response->session_data = buf;
}

static void mac_set_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request, struct httpd_response *response)
{
	struct httpd_form_value *value;
	uchar mac[6];
	char formatted[18];
	int ret;

	if (status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		mac_response(response, 405, "bad method", "text/plain");
		return;
	}
	if (request->form.count != 1) {
		mac_response(response, 400, "one mac_data field required", "text/plain");
		return;
	}
	value = httpd_request_find_value(request, "mac_data");
	if (!value || !value->data || parse_unicast_mac((const unsigned char *)value->data, value->size, mac)) {
		mac_response(response, 400, "one valid unicast MAC required", "text/plain");
		return;
	}
	snprintf(formatted, sizeof(formatted), "%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	/* This helper changes only ethaddr and returns success after full APPSBLENV readback. */
	ret = failsafe_env_set_persistent("ethaddr", formatted);
	if (ret) {
		mac_response(response, 500, "APPSBLENV write or readback failed", "text/plain");
		return;
	}
	mac_response(response, 200, "ok", "text/plain");
}

void mac_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/mac/info", mac_info_handler, NULL);
	httpd_register_uri_handler(inst, "/mac/set", mac_set_handler, NULL);
}
