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
#include <asm/errno.h>
#include <net/httpd.h>

#include "modules.h"

# if defined(CONFIG_IPADDR)
static const char *default_ipaddr = __stringify(CONFIG_IPADDR);
# else
static const char *default_ipaddr = "192.168.1.1";
# endif /* CONFIG_IPADDR */
# if defined(CONFIG_NETMASK)
static const char *default_netmask = __stringify(CONFIG_NETMASK);
# else
static const char *default_netmask = "255.255.255.0";
# endif /* CONFIG_NETMASK */
# if defined(CONFIG_SERVERIP)
static const char *default_serverip = __stringify(CONFIG_SERVERIP);
# else
static const char *default_serverip = "192.168.1.2";
# endif /* CONFIG_SERVERIP */

static void handle_response_message(struct httpd_response *response,
		int code, const char *msg, const char *content_type)
{
	response->status = HTTP_RESP_STD;
	response->data = msg ? msg : "";
	response->size = strlen(response->data);
	response->info.code = code;
	response->info.connection_close = 1;
	response->info.content_type = content_type ? content_type : "text/plain";
}

static void network_set_env(struct httpd_response *response, bool custom,
		const char *ipaddr, const char *netmask, const char *serverip)
{
	int ret = 0, modified = 0;
	bool custom_state_changed = false;
	const char *custom_state;
	const char *current_ipaddr, *current_netmask, *current_serverip;

	custom_state = getenv("custom_network");
	current_ipaddr = getenv("ipaddr");
	current_netmask = getenv("netmask");
	current_serverip = getenv("serverip");

	if ((custom && !custom_state) || (!custom && custom_state))
		custom_state_changed = true;

	if (custom_state_changed)
		if(!setenv("custom_network", custom ? "1" : NULL))
			modified++;

	if (!current_ipaddr || strcmp(current_ipaddr, ipaddr))
		if(!setenv("ipaddr", ipaddr))
			modified++;

	if (!current_netmask || strcmp(current_netmask, netmask))
		if(!setenv("netmask", netmask))
			modified++;

	if (!current_serverip || strcmp(current_serverip, serverip))
		if(!setenv("serverip", serverip))
			modified++;

	if (modified)
		ret = saveenv();

	if (!ret)
		handle_response_message(response, 200, "ok", NULL);
	else
		handle_response_message(response, 500, "save failed", NULL);
}

static void network_info_handler(enum httpd_uri_handler_status status,
	    struct httpd_request *request,
	    struct httpd_response *response)
{
	static char resp[128];
	const char *ipaddr, *netmask, *serverip;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		handle_response_message(response, 405, "method", NULL);
		return;
	}

	ipaddr = getenv("ipaddr");
	netmask = getenv("netmask");
	serverip = getenv("serverip");

	snprintf(resp, sizeof(resp),
		"{\"ipaddr\":\"%s\",\"netmask\":\"%s\",\"serverip\":\"%s\"}",
		ipaddr ? ipaddr : default_ipaddr,
		netmask ? netmask : default_netmask,
		serverip ? serverip : default_serverip);

	handle_response_message(response, 200, resp, "application/json");
}

static int network_parse_ipv4(const char *s,size_t len,unsigned int *out)
{
 unsigned int value=0,octet,digits;
 size_t i=0;
 int part;
 if(!s||!out||len<7||len>15||memchr(s,0,len)) return -EINVAL;
 for(part=0;part<4;part++){
  octet=0;digits=0;
  while(i<len&&s[i]>='0'&&s[i]<='9'){
   octet=octet*10+(s[i]-'0');digits++;i++;
   if(digits>3||octet>255) return -EINVAL;
  }
  if(!digits) return -EINVAL;
  value=(value<<8)|octet;
  if(part<3){if(i>=len||s[i++]!='.')return -EINVAL;}
  else if(i!=len)return -EINVAL;
 }
 *out=value;return 0;
}
static int network_form_ipv4(struct httpd_request *req,const char *name,
 char text[16],unsigned int *value)
{
 struct httpd_form_value *v;unsigned int i,count=0;
 if(!req||!name||!text||!value)return -EINVAL;
 for(i=0;i<req->form.count;i++)
  if(req->form.values[i].name&&!strcmp(req->form.values[i].name,name))count++;
 if(count!=1)return -EINVAL;
 v=httpd_request_find_value(req,name);
 if(!v||!v->data||v->size>=16||network_parse_ipv4(v->data,v->size,value))return -EINVAL;
 memcpy(text,v->data,v->size);text[v->size]='\0';return 0;
}

static int network_mask_valid(unsigned int mask)
{
 unsigned int hostmask = ~mask;
 return !(hostmask & (hostmask + 1));
}
static int network_values_valid(unsigned int ip, unsigned int mask,
 unsigned int serverip)
{
 (void)ip;
 (void)serverip;
 return network_mask_valid(mask);
}

static void network_set_handler(enum httpd_uri_handler_status status,
	    struct httpd_request *request,
	    struct httpd_response *response)
{
	char ipaddr[16], netmask[16], serverip[16];
	unsigned int ip, mask, server;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		handle_response_message(response, 405, "method", NULL);
		return;
	}

	if (network_form_ipv4(request, "ipaddr", ipaddr, &ip) ||
	    network_form_ipv4(request, "netmask", netmask, &mask) ||
	    network_form_ipv4(request, "serverip", serverip, &server) ||
	    !network_values_valid(ip, mask, server)) {
		handle_response_message(response, 400, "invalid network values", NULL);
		return;
	}

	network_set_env(response, true, ipaddr, netmask, serverip);
}

static void network_reset_handler(enum httpd_uri_handler_status status,
	    struct httpd_request *request,
	    struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		handle_response_message(response, 405, "method", NULL);
		return;
	}

	network_set_env(response, false,
		default_ipaddr, default_netmask, default_serverip);
}

void network_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/network/info", &network_info_handler, NULL);
	httpd_register_uri_handler(inst, "/network/set", &network_set_handler, NULL);
	httpd_register_uri_handler(inst, "/network/reset", &network_reset_handler, NULL);
}
