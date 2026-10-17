// SPDX-License-Identifier: GPL-2.0
/* H3C RC-A2 HW-A observational button status; no action is bound to either pin. */
#ifndef H3C_HW_A_BUTTONS_HOST_TEST
#include <common.h>
#include <asm/io.h>
#include <asm/arch-qca-common/gpio.h>
#include <configs/ipq5018.h>
#include <dt-bindings/qcom/gpio-ipq5018.h>
#include <net/httpd.h>
#include "modules.h"
#include <ipq_led.h>
#else
#include <stdio.h>
#include <string.h>
#endif

#define H3C_HW_A_RESET_GPIO 27
#define H3C_HW_A_WPS_GPIO 38

#define H3C_TLMM_PULL_MASK	(0x3U)
#define H3C_TLMM_FUNC_MASK	(0xfU << 2)
#define H3C_TLMM_DRIVE_MASK	(0x7U << 6)
#define H3C_TLMM_OE_MASK	(1U << 9)

static char h3c_hw_a_buttons_json[256];

/*
 * gpio_tlmm_config() also writes GPIO_IN_OUT_ADDR for func 0. Configure the
 * TLMM input fields directly so sampling never writes an output latch.
 */
static void h3c_hw_a_button_input_pullup(unsigned int gpio)
{
	void *config = (void *)(uintptr_t)GPIO_CONFIG_ADDR(gpio);
	u32 value = readl(config);

	value &= ~(H3C_TLMM_PULL_MASK | H3C_TLMM_FUNC_MASK |
		   H3C_TLMM_DRIVE_MASK | H3C_TLMM_OE_MASK);
	value |= GPIO_PULL_UP | (GPIO_INPUT << 2) |
		 (GPIO_8MA << 6) | (GPIO_OE_DISABLE << 9);
	writel(value, config);
}

/* Configure both pins before either input is sampled. */
static void h3c_hw_a_restore_green_status(void)
{
	led_off("power_led");
	led_on("system_led");
}

/* Three red 0.5s-on/0.5s-off flashes; early release cancels, full hold latches. */
int h3c_hw_a_reset_held_3s(void)
{
	unsigned int i;
	h3c_hw_a_button_input_pullup(H3C_HW_A_RESET_GPIO);
	if (gpio_get_value(H3C_HW_A_RESET_GPIO) != GPIO_OUT_LOW)
		return 0;

	led_all_off();
	for (i = 0; i < 300; i++) {
		if (gpio_get_value(H3C_HW_A_RESET_GPIO) != GPIO_OUT_LOW) {
			h3c_hw_a_restore_green_status();
			return 0;
		}
		if ((i % 100) < 50)
			led_on("power_led");
		else
			led_off("power_led");
		udelay(10000);
	}
	h3c_hw_a_restore_green_status();
	return 1;
}

static void h3c_hw_a_sample_buttons(int *reset_level, int *wps_level)
{
	h3c_hw_a_button_input_pullup(H3C_HW_A_RESET_GPIO);
	h3c_hw_a_button_input_pullup(H3C_HW_A_WPS_GPIO);
	*reset_level = gpio_get_value(H3C_HW_A_RESET_GPIO);
	*wps_level = gpio_get_value(H3C_HW_A_WPS_GPIO);
}

#ifdef CONFIG_H3C_HW_A_RAM_READONLY
/* Kept reachable for an explicitly invoked RAM test; never called at autoboot. */
static int (*volatile h3c_hw_a_reset_test_entry)(void);
#endif

static void buttons_h3c_hw_a_handler(enum httpd_uri_handler_status status,
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

	h3c_hw_a_sample_buttons(&reset_level, &wps_level);
	snprintf(h3c_hw_a_buttons_json, sizeof(h3c_hw_a_buttons_json),
		 "{\"read_only\":true,\"action_taken\":false,\"buttons\":["
		 "{\"name\":\"RESET\",\"gpio\":27,\"level\":%d,"
		 "\"active_low\":true,\"pressed\":%s},"
		 "{\"name\":\"WPS/MESH\",\"gpio\":38,\"level\":%d,"
		 "\"active_low\":true,\"pressed\":%s}]}",
		 reset_level, reset_level == GPIO_OUT_LOW ? "true" : "false",
		 wps_level, wps_level == GPIO_OUT_LOW ? "true" : "false");
	response->info.code = 200;
	response->data = h3c_hw_a_buttons_json;
	response->size = strlen(response->data);
}

void buttons_h3c_hw_a_register_uri_handlers(struct httpd_instance *inst)
{
#ifdef CONFIG_H3C_HW_A_RAM_READONLY
	h3c_hw_a_reset_test_entry = h3c_hw_a_reset_held_3s;
#endif
	httpd_register_uri_handler(inst, "/board/buttons",
				   buttons_h3c_hw_a_handler, NULL);
}
