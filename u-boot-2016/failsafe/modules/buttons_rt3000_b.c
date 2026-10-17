// SPDX-License-Identifier: GPL-2.0
/* RT3000-B read-only physical button state endpoint. */
#include <common.h>
#include <asm/arch-qca-common/gpio.h>
#include <net/httpd.h>
#include "modules.h"

#include <h3c_board_profile.h>

static char buttons_json[128];

static void buttons_rt3000_b_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request, struct httpd_response *response)
{
	static const char method_error[] = "{\"error\":\"GET required\"}";
	int reset_level, wps_level;

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";
	if (!request || request->method != HTTP_GET) {
		response->info.code = 405;
		response->data = method_error;
		response->size = sizeof(method_error) - 1;
		return;
	}

	/* Both pins are configured as inputs in /keys; this handler only samples. */
	reset_level = gpio_get_value(H3C_RESET_GPIO);
	wps_level = gpio_get_value(H3C_WPS_GPIO);
	snprintf(buttons_json, sizeof(buttons_json),
		"{\"buttons\":[{\"name\":\"RESET\",\"pressed\":%s},"
		"{\"name\":\"WPS/MESH\",\"pressed\":%s}]}",
		reset_level == GPIO_OUT_LOW ? "true" : "false",
		wps_level == GPIO_OUT_LOW ? "true" : "false");
	response->info.code = 200;
	response->data = buttons_json;
	response->size = strlen(buttons_json);
}

void buttons_rt3000_b_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/board/buttons",
		buttons_rt3000_b_handler, NULL);
}
