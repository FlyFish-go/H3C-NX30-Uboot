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
#include <errno.h>
#include <fdtdec.h>
#include <dt-bindings/gpio/gpio.h>
#include <ipq_led.h>

DECLARE_GLOBAL_DATA_PTR;

static bool led_node_has_label(int node, const char *label)
{
	int strings_count;
	const char *node_label;

	strings_count = fdt_count_strings(gd->fdt_blob, node, "label");
	for (int i = 0; i < strings_count; i++) {
		node_label = NULL;
		if (!fdt_get_string_index(gd->fdt_blob, node, "label", i,
					  &node_label) &&
		    node_label && !strcmp(node_label, label))
			return true;
	}
	return false;
}

static int led_get_out_value(led_op_t op, unsigned int gpio,
        unsigned int active_level, unsigned int *value)
{
	if (!value) {
		return -EINVAL;
	}

	switch(op) {
	case LED_OP_ON:
		*value = (active_level == GPIO_ACTIVE_HIGH) ? GPIO_OUT_HIGH : GPIO_OUT_LOW;
		break;
	case LED_OP_OFF:
		*value = (active_level == GPIO_ACTIVE_HIGH) ? GPIO_OUT_LOW : GPIO_OUT_HIGH;
		break;
	case LED_OP_TOGGLE:
		*value = !gpio_get_value(gpio);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void led_control(led_op_t op, const char *label)
{
	int parent, node;
	unsigned int gpio, active_level, value;

	if (!label)
		return;
	parent = fdt_path_offset(gd->fdt_blob, "/leds");
	if (parent < 0)
		return;

	/* Apply a logical label to every matching physical LED GPIO. */
	fdt_for_each_subnode(gd->fdt_blob, node, parent) {
		if (!led_node_has_label(node, label))
			continue;
		gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", GPIO_NOT_FOUND);
		if (gpio == GPIO_NOT_FOUND)
			continue;
		active_level = fdtdec_get_uint(gd->fdt_blob, node, "active_level",
					       GPIO_ACTIVE_HIGH);
		if (!led_get_out_value(op, gpio, active_level, &value))
			gpio_set_value(gpio, value);
	}
}

void led_control_all(led_op_t op)
{
	int parent, node;
	unsigned int gpio, active_level, value;

	parent = fdt_path_offset(gd->fdt_blob, "/leds");
	if (parent < 0)
		return;

	fdt_for_each_subnode(gd->fdt_blob, node, parent) {
		gpio = fdtdec_get_uint(gd->fdt_blob, node, "gpio", GPIO_NOT_FOUND);
		active_level = fdtdec_get_uint(gd->fdt_blob, node, "active_level", GPIO_ACTIVE_HIGH);
		if (gpio != GPIO_NOT_FOUND && !led_get_out_value(op, gpio, active_level, &value))
			gpio_set_value(gpio, value);
	}
}
