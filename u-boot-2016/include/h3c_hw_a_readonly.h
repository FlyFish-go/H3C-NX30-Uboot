/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __H3C_HW_A_READONLY_H
#define __H3C_HW_A_READONLY_H


#ifdef CONFIG_TARGET_IPQ5018_H3C_HW_A
#include <errno.h>
/* Shared hard stop for every low-level NAND write/erase callback. */
static inline int h3c_hw_a_nand_write_rejected(void)
{
	return -EROFS;
}
#endif

static inline int h3c_hw_a_request_rejected(int is_get, int is_post,
						 const char *uri)
{
	static const char * const denied_get[] = {
		"/backup", "/reboot", "/mibib/reload", "/webterm/exec",
		"/webterm/upload", "/upload", "/result", "/write",
		"/env/set", "/env/unset", "/env/reset/all",
		"/env/reset/single", "/env/restore", "/mac/set",
		"/network/set", "/network/reset",
	};
	unsigned int i;

	if (!uri)
		return 1;
	if (is_post)
		return strcmp(uri, "/backup") != 0;
	if (!is_get)
		return 1;
	for (i = 0; i < sizeof(denied_get) / sizeof(denied_get[0]); i++)
		if (!strcmp(uri, denied_get[i]))
			return 1;
	return 0;
}

#endif
