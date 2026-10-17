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
#include <command.h>
#include <malloc.h>
#include <console.h>
#include <membuff.h>
#include <net/httpd.h>
#include <ipq_api.h>

#include "modules.h"

DECLARE_GLOBAL_DATA_PTR;

#define SYSLOG_POLL_MAX	8192

static void handle_response_message(struct httpd_response *response,
    int code, const char *data, int data_size)
{
	response->status = HTTP_RESP_STD;
	response->data = data ? data : "";
	response->size = (data_size != -1) ? data_size : strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

static void syslog_poll_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request,
		struct httpd_response *response)
{
	char *buf = NULL;
	int avail, want, got;

	if (status == HTTP_CB_CLOSED && response->session_data) {
		free(response->session_data);
		response->session_data = NULL;
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

    response->session_data = NULL;

	if (!request || request->method != HTTP_GET) {
        handle_response_message(response, 405, "bad method", -1);
		return;
	}

	if (!gd->console_out.start) {
        handle_response_message(response, 503, "no log", -1);
		return;
	}

	avail = membuff_avail((struct membuff *)&gd->console_out);
	want = min_t(int, avail, SYSLOG_POLL_MAX);

	buf = malloc(want + 1);
	if (!buf) {
        handle_response_message(response, 500, "no mem", -1);
		return;
	}

	got = membuff_get((struct membuff *)&gd->console_out, buf, want);
	buf[got] = '\0';

    handle_response_message(response, 200, buf, got);
	response->session_data = buf;
}

void syslog_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/syslog/poll", &syslog_poll_handler, NULL);
#ifdef CONFIG_H3C_HW_D_NAND_SERVICE
	httpd_register_uri_handler(inst, "/service/logs", &syslog_poll_handler, NULL);
#endif
}
