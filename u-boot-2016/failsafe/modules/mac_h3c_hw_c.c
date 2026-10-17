// SPDX-License-Identifier: GPL-2.0
/* HW-C MAC identity is read only from the already-loaded U-Boot environment. */
#ifndef H3C_HW_C_MAC_HOST_TEST
#include <common.h>
#include <asm/errno.h>
#include <net.h>
#include <net/httpd.h>
#include "modules.h"
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char uchar;
#endif

#ifdef CONFIG_H3C_HW_C_PRODUCTION_PORT
#define H3C_HW_C_MAC_WRITABLE "true"
#define H3C_HW_C_MAC_PERSISTENT "true"
#define H3C_HW_C_MAC_READ_ONLY "false"
#else
#define H3C_HW_C_MAC_WRITABLE "false"
#define H3C_HW_C_MAC_PERSISTENT "false"
#define H3C_HW_C_MAC_READ_ONLY "true"
#endif

#define H3C_HW_C_MAC_JSON_SIZE 320

static char h3c_hw_c_mac_json[H3C_HW_C_MAC_JSON_SIZE];

static bool h3c_hw_c_is_placeholder_mac(const uchar *mac)
{
	static const uchar placeholders[][6] = {
		{ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
		{ 0x00, 0x11, 0x22, 0x33, 0x44, 0x56 },
	};

	return !memcmp(mac, placeholders[0], 6) ||
	       !memcmp(mac, placeholders[1], 6);
}

static bool h3c_hw_c_get_ram_ethaddr(uchar *mac)
{
	return eth_getenv_enetaddr("ethaddr", mac) &&
	       is_valid_ethaddr(mac) &&
	       !h3c_hw_c_is_placeholder_mac(mac);
}

static void h3c_hw_c_mac_info_handler(enum httpd_uri_handler_status status,
		struct httpd_request *request, struct httpd_response *response)
{
	uchar mac[6];
	int available;

	if (status != HTTP_CB_NEW)
		return;

	response->session_data = NULL;
	response->status = HTTP_RESP_STD;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	if (!request || request->method != HTTP_GET) {
		static const char method_error[] = "{\"error\":\"GET required\"}";

		response->info.code = 405;
		response->data = method_error;
		response->size = sizeof(method_error) - 1;
		return;
	}

	available = h3c_hw_c_get_ram_ethaddr(mac);
	if (available) {
		snprintf(h3c_hw_c_mac_json, sizeof(h3c_hw_c_mac_json),
			 "{\"part_name\":\"0:APPSBLENV\","
			 "\"source\":\"uboot_environment\","
			 "\"variable\":\"ethaddr\","
			 "\"writable\":%s,\"persistent\":%s,"
			 "\"read_only\":%s,\"available\":true,"
			 "\"macs\":[\"%02X:%02X:%02X:%02X:%02X:%02X\"]}",
			 H3C_HW_C_MAC_WRITABLE,H3C_HW_C_MAC_PERSISTENT,H3C_HW_C_MAC_READ_ONLY,
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		snprintf(h3c_hw_c_mac_json, sizeof(h3c_hw_c_mac_json),
			 "{\"part_name\":\"0:APPSBLENV\","
			 "\"source\":\"uboot_environment\","
			 "\"variable\":\"ethaddr\","
			 "\"writable\":false,\"persistent\":false,"
			 "\"read_only\":true,\"available\":false,"
			 "\"reason\":\"no_valid_ram_ethaddr\",\"macs\":[]}");
	}

	response->info.code = 200;
	response->data = h3c_hw_c_mac_json;
	response->size = strlen(response->data);
}

#ifdef CONFIG_H3C_HW_C_PRODUCTION_PORT
static int mac_nibble(unsigned char c)
{
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static void mac_set_handler(enum httpd_uri_handler_status status,
 struct httpd_request *request,struct httpd_response *response)
{
 struct httpd_form_value *v;uchar mac[6];char formatted[18];int i,a,b,any=0,ret;
 static const char bad[]="invalid MAC";
 static const char ok[]="ok";
 static const char save_failed[]="save or verification failed";
 if(status!=HTTP_CB_NEW)return;
 response->status=HTTP_RESP_STD;response->info.connection_close=1;
 response->info.content_type="text/plain";
 if(!request||request->method!=HTTP_POST||request->form.count!=1){response->info.code=400;response->data=bad;response->size=sizeof(bad)-1;return;}
 v=httpd_request_find_value(request,"mac_data");
 if(!v||!v->data||v->size!=17){response->info.code=400;response->data=bad;response->size=sizeof(bad)-1;return;}
 for(i=0;i<6;i++){
  a=mac_nibble(v->data[i*3]);b=mac_nibble(v->data[i*3+1]);
  if(a<0||b<0||(i<5&&v->data[i*3+2]!=':')){response->info.code=400;response->data=bad;response->size=sizeof(bad)-1;return;}
  mac[i]=(a<<4)|b;any|=mac[i];
 }
 if(!any||(mac[0]&1)||h3c_hw_c_is_placeholder_mac(mac)){response->info.code=400;response->data=bad;response->size=sizeof(bad)-1;return;}
 snprintf(formatted,sizeof(formatted),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
 ret=failsafe_env_set_persistent("ethaddr",formatted);
 response->info.code=ret?500:200;response->data=ret?save_failed:ok;
 response->size=strlen(response->data);
}
#endif

void mac_register_uri_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/mac/info",
				   h3c_hw_c_mac_info_handler, NULL);
#ifdef CONFIG_H3C_HW_C_PRODUCTION_PORT
	httpd_register_uri_handler(inst, "/mac/set", mac_set_handler, NULL);
#endif
}
