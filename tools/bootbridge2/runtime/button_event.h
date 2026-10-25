/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_BUTTON_EVENT_H
#define BB_BUTTON_EVENT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum bb_button_event { BB_BUTTON_NONE, BB_BUTTON_PRESS, BB_BUTTON_RELEASE };

/* OpenWrt gpio-button-hotplug: NUL-separated kernel uevent properties. */
static inline enum bb_button_event bb_button_parse(const char *buf, size_t len)
{
	int button = 0, subsystem = 0;
	enum bb_button_event event = BB_BUTTON_NONE;

	while (len) {
		size_t n = strnlen(buf, len);

		if (n == len)
			return BB_BUTTON_NONE;
		if (!strcmp(buf, "SUBSYSTEM=button")) subsystem = 1;
		if (!strcmp(buf, "BUTTON=wps")) button = 1;
		if (!strcmp(buf, "ACTION=pressed")) event = BB_BUTTON_PRESS;
		if (!strcmp(buf, "ACTION=released")) event = BB_BUTTON_RELEASE;
		buf += n + 1;
		len -= n + 1;
	}
	return button && subsystem ? event : BB_BUTTON_NONE;
}

struct bb_button_hold { int64_t pressed_at; int fired; };

static inline int bb_button_step(struct bb_button_hold *h, enum bb_button_event ev, int64_t now)
{
	int trigger;

	if (ev == BB_BUTTON_PRESS && h->pressed_at < 0) {
		h->pressed_at = now;
		h->fired = 0;
	}
	trigger = h->pressed_at >= 0 && !h->fired && now - h->pressed_at >= 3000;
	if (trigger) h->fired = 1;
	if (ev == BB_BUTTON_RELEASE) h->pressed_at = -1;
	return trigger;
}
#endif
