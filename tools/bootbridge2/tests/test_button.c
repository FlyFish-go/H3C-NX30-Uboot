// SPDX-License-Identifier: GPL-2.0-or-later
#include "../runtime/button_event.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
	static const char press[] = "pressed@/\0SUBSYSTEM=button\0BUTTON=wps\0ACTION=pressed\0";
	static const char reset[] = "SUBSYSTEM=button\0BUTTON=reset\0ACTION=pressed\0";
	static const char other[] = "SUBSYSTEM=net\0BUTTON=wps\0ACTION=pressed\0";
	struct bb_button_hold h = { .pressed_at = -1 };

	assert(bb_button_parse(press, sizeof(press)) == BB_BUTTON_PRESS);
	assert(bb_button_parse(press, sizeof(press)-2) == BB_BUTTON_NONE);
	assert(bb_button_parse(reset, sizeof(reset)) == BB_BUTTON_NONE);
	assert(bb_button_parse(other, sizeof(other)) == BB_BUTTON_NONE);
	assert(!bb_button_step(&h, BB_BUTTON_RELEASE, 0));
	assert(!bb_button_step(&h, BB_BUTTON_PRESS, 100));
	assert(!bb_button_step(&h, BB_BUTTON_RELEASE, 3099));
	assert(!bb_button_step(&h, BB_BUTTON_NONE, 5000));
	assert(!bb_button_step(&h, BB_BUTTON_PRESS, 6000));
	assert(!bb_button_step(&h, BB_BUTTON_PRESS, 7000));
	assert(!bb_button_step(&h, BB_BUTTON_NONE, 8999));
	assert(bb_button_step(&h, BB_BUTTON_NONE, 9000));
	assert(!bb_button_step(&h, BB_BUTTON_NONE, 10000));
	assert(!bb_button_step(&h, BB_BUTTON_RELEASE, 11000));
	assert(!bb_button_step(&h, BB_BUTTON_PRESS, 12000));
	assert(bb_button_step(&h, BB_BUTTON_RELEASE, 15001));
	puts("button events: OEM uevent parsing, truncated/other keys ignored, short/long holds and single fire PASS");
	return 0;
}
