// SPDX-License-Identifier: GPL-2.0
/* H3C HW-C observational button status; no action is bound to either pin. */
#ifndef H3C_HW_C_BUTTONS_HOST_TEST
#include <common.h>
#include <asm/io.h>
#include <asm/arch-qca-common/gpio.h>
#include <configs/ipq5018.h>
#include <dt-bindings/qcom/gpio-ipq5018.h>
#include <net/httpd.h>
#include "modules.h"

#else
#include <stdio.h>
#include <string.h>
#endif

#define H3C_HW_C_RESET_GPIO 27
#define H3C_HW_C_WPS_GPIO 38

#define H3C_TLMM_PULL_MASK	(0x3U)
#define H3C_TLMM_FUNC_MASK	(0xfU << 2)
#define H3C_TLMM_DRIVE_MASK	(0x7U << 6)
#define H3C_TLMM_OE_MASK	(1U << 9)

static char h3c_hw_c_buttons_json[256];

/*
 * gpio_tlmm_config() also writes GPIO_IN_OUT_ADDR for func 0. Configure the
 * TLMM input fields directly so sampling never writes an output latch.
 */
static void h3c_hw_c_button_input_pullup(unsigned int gpio)
{
	void *config = (void *)(uintptr_t)GPIO_CONFIG_ADDR(gpio);
	u32 value = readl(config);

	value &= ~(H3C_TLMM_PULL_MASK | H3C_TLMM_FUNC_MASK |
		   H3C_TLMM_DRIVE_MASK | H3C_TLMM_OE_MASK);
	value |= GPIO_PULL_UP | (GPIO_INPUT << 2) |
		 (GPIO_8MA << 6) | (GPIO_OE_DISABLE << 9);
	writel(value, config);
}

static void h3c_hw_c_sample_buttons(int *reset_level, int *wps_level)
{
	h3c_hw_c_button_input_pullup(H3C_HW_C_RESET_GPIO);
	h3c_hw_c_button_input_pullup(H3C_HW_C_WPS_GPIO);
	*reset_level = gpio_get_value(H3C_HW_C_RESET_GPIO);
	*wps_level = gpio_get_value(H3C_HW_C_WPS_GPIO);
}

static void buttons_h3c_hw_c_handler(enum httpd_uri_handler_status status,
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

	h3c_hw_c_sample_buttons(&reset_level, &wps_level);
	snprintf(h3c_hw_c_buttons_json, sizeof(h3c_hw_c_buttons_json),
		 "{\"read_only\":true,\"action_taken\":false,\"buttons\":["
		 "{\"name\":\"RESET\",\"gpio\":27,\"level\":%d,"
		 "\"active_low\":true,\"pressed\":%s},"
		 "{\"name\":\"WPS/MESH\",\"gpio\":38,\"level\":%d,"
		 "\"active_low\":true,\"pressed\":%s}]}",
		 reset_level, reset_level == GPIO_OUT_LOW ? "true" : "false",
		 wps_level, wps_level == GPIO_OUT_LOW ? "true" : "false");
	response->info.code = 200;
	response->data = h3c_hw_c_buttons_json;
	response->size = strlen(response->data);
}

void buttons_h3c_hw_c_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/board/buttons",
				   buttons_h3c_hw_c_handler, NULL);
}
