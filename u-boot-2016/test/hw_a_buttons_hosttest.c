// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define H3C_HW_A_BUTTONS_HOST_TEST 1
typedef uint32_t u32;
#define GPIO_CONFIG_ADDR(gpio) (0x1000U + (gpio) * 0x10U)
#define GPIO_IN_OUT_ADDR(gpio) (0x2000U + (gpio) * 0x10U)
#define GPIO_PULL_UP 3
#define GPIO_INPUT 0
#define GPIO_8MA 3
#define GPIO_OE_DISABLE 0
#define GPIO_OUT_LOW 0
#define GPIO_OUT_HIGH 1
#define HTTP_CB_NEW 0
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_RESP_STD 0

struct httpd_instance { int unused; };
struct httpd_request { int method; };
struct httpd_response_info {
	int code;
	int connection_close;
	const char *content_type;
};
struct httpd_response {
	int status;
	struct httpd_response_info info;
	const char *data;
	unsigned int size;
};
enum httpd_uri_handler_status { TEST_HTTP_CB_NEW = HTTP_CB_NEW };
typedef void (*uri_handler_t)(enum httpd_uri_handler_status,
			      struct httpd_request *, struct httpd_response *);

static uint32_t registers[0x4000];
static unsigned int config_writes;
static unsigned int input_reads;
static unsigned int output_writes;
static unsigned int delay_calls;
static unsigned int release_after;
static unsigned int trace_led;
static unsigned int led_trace[300];
static int led_green;
static int led_red;
static const char *registered_uri;
static uri_handler_t registered_handler;

static uint32_t readl(const void *address)
{
	uintptr_t index = (uintptr_t)address;
	assert(index < sizeof(registers) / sizeof(registers[0]));
	return registers[index];
}

static void writel(uint32_t value, void *address)
{
	uintptr_t index = (uintptr_t)address;
	assert(index < sizeof(registers) / sizeof(registers[0]));
	if (index == GPIO_IN_OUT_ADDR(27) || index == GPIO_IN_OUT_ADDR(38))
		output_writes++;
	else if (index == GPIO_CONFIG_ADDR(27) ||
		 index == GPIO_CONFIG_ADDR(38))
		config_writes++;
	registers[index] = value;
}

static int gpio_get_value(unsigned int gpio)
{
	assert((registers[GPIO_CONFIG_ADDR(gpio)] & 0x3U) == GPIO_PULL_UP);
	input_reads++;
	return registers[GPIO_IN_OUT_ADDR(gpio)] & 1U;
}
static void led_all_off(void)
{
	led_green = 0;
	led_red = 0;
}

static void led_on(const char *label)
{
	if (!strcmp(label, "power_led"))
		led_red = 1;
	else if (!strcmp(label, "system_led") || !strcmp(label, "blink_led"))
		led_green = 1;
}

static void led_off(const char *label)
{
	if (!strcmp(label, "power_led"))
		led_red = 0;
	else if (!strcmp(label, "system_led") || !strcmp(label, "blink_led"))
		led_green = 0;
}

static void udelay(unsigned int usec)
{
	assert(usec == 10000);
	delay_calls++;
	if (trace_led && delay_calls <= 300)
		led_trace[delay_calls - 1] = (led_red ? 1U : 0U) |
			(led_green ? 2U : 0U);
	if (release_after && delay_calls == release_after)
		registers[GPIO_IN_OUT_ADDR(27)] = 1;
}

static void httpd_register_uri_handler(struct httpd_instance *inst,
				      const char *uri, uri_handler_t handler,
				      void *data)
{
	(void)inst;
	(void)data;
	registered_uri = uri;
	registered_handler = handler;
}

#include "../failsafe/modules/buttons_h3c_hw_a.c"

static void run_get(int reset_level, int wps_level)
{
	struct httpd_request request = { .method = HTTP_GET };
	struct httpd_response response;
	unsigned int writes_before = config_writes;
	unsigned int reads_before = input_reads;

	memset(&response, 0, sizeof(response));
	registers[GPIO_IN_OUT_ADDR(27)] = reset_level;
	registers[GPIO_IN_OUT_ADDR(38)] = wps_level;
	registers[GPIO_CONFIG_ADDR(27)] = 0xc1U | (1U << 20);
	registers[GPIO_CONFIG_ADDR(38)] = 0xc1U | (1U << 21);

	registered_handler(HTTP_CB_NEW, &request, &response);

	assert(response.info.code == 200);
	assert(response.status == HTTP_RESP_STD);
	assert(response.info.connection_close == 1);
	assert(response.info.content_type &&
	       strcmp(response.info.content_type, "application/json") == 0);
	assert(config_writes == writes_before + 2);
	assert(input_reads == reads_before + 2);
	assert(output_writes == 0);
	assert((registers[GPIO_CONFIG_ADDR(27)] & 0x3U) == GPIO_PULL_UP);
	assert((registers[GPIO_CONFIG_ADDR(38)] & 0x3U) == GPIO_PULL_UP);
	assert((registers[GPIO_CONFIG_ADDR(27)] & ((1U << 9) | (0xfU << 2))) == 0);
	assert((registers[GPIO_CONFIG_ADDR(38)] & ((1U << 9) | (0xfU << 2))) == 0);
	assert(((registers[GPIO_CONFIG_ADDR(27)] >> 6) & 0x7U) == GPIO_8MA);
	assert(((registers[GPIO_CONFIG_ADDR(38)] >> 6) & 0x7U) == GPIO_8MA);
	assert(strstr(response.data, "\"read_only\":true"));
	assert(strstr(response.data, "\"action_taken\":false"));
	assert(strstr(response.data, "\"name\":\"RESET\",\"gpio\":27"));
	assert(strstr(response.data, "\"name\":\"WPS/MESH\",\"gpio\":38"));
	assert(strstr(response.data, "\"active_low\":true"));
	if (reset_level == 0)
		assert(strstr(response.data, "\"name\":\"RESET\",\"gpio\":27,\"level\":0,\"active_low\":true,\"pressed\":true"));
	else
		assert(strstr(response.data, "\"name\":\"RESET\",\"gpio\":27,\"level\":1,\"active_low\":true,\"pressed\":false"));
	if (wps_level == 0)
		assert(strstr(response.data, "\"name\":\"WPS/MESH\",\"gpio\":38,\"level\":0,\"active_low\":true,\"pressed\":true"));
	else
		assert(strstr(response.data, "\"name\":\"WPS/MESH\",\"gpio\":38,\"level\":1,\"active_low\":true,\"pressed\":false"));
}

int main(void)
{
	struct httpd_instance instance = { 0 };
	struct httpd_request request = { .method = HTTP_POST };
	struct httpd_response response;

	buttons_h3c_hw_a_register_uri_handlers(&instance);
	assert(registered_uri && strcmp(registered_uri, "/board/buttons") == 0);
	assert(registered_handler != NULL);

	memset(&response, 0, sizeof(response));
	registered_handler(HTTP_CB_NEW, &request, &response);
	assert(response.info.code == 405);
	assert(config_writes == 0 && input_reads == 0 && output_writes == 0);

	/* Active-low press and release are derived only from sampled input levels. */
	run_get(0, 1);
	run_get(1, 0);
	assert(output_writes == 0);
	/* No press does not flash or disturb the initialized steady-green state. */
	led_green = 1; led_red = 0; delay_calls = 0; trace_led = 1;
	registers[GPIO_IN_OUT_ADDR(27)] = 1;
	assert(h3c_hw_a_reset_held_3s() == 0);
	assert(delay_calls == 0 && led_green && !led_red);

	/* Early release cancels and restores steady green after a partial pattern. */
	registers[GPIO_IN_OUT_ADDR(27)] = 0; delay_calls = 0; release_after = 75;
	assert(h3c_hw_a_reset_held_3s() == 0);
	assert(delay_calls == 75 && led_green && !led_red);
	for (unsigned int i = 0; i < 75; i++)
		assert(led_trace[i] == (i % 100 < 50 ? 1U : 0U));

	/* Full hold gives exactly three 0.5s red pulses, then green and a latched decision. */
	registers[GPIO_IN_OUT_ADDR(27)] = 0; delay_calls = 0; release_after = 0;
	assert(h3c_hw_a_reset_held_3s() == 1);
	assert(delay_calls == 300 && led_green && !led_red);
	for (unsigned int i = 0; i < 300; i++)
		assert(led_trace[i] == (i % 100 < 50 ? 1U : 0U));
	assert(output_writes == 0);
	puts("HW-A buttons observation and RESET hold host tests: PASS");
	return 0;
}
