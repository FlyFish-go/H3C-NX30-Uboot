// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define H3C_HW_A_MAC_HOST_TEST 1
#define HTTP_CB_NEW 0
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_RESP_STD 0
typedef unsigned char uchar;
struct httpd_instance { int unused; };
struct httpd_request { int method; };
struct httpd_response_info { int code, connection_close; const char *content_type; };
struct httpd_response { int status; struct httpd_response_info info; const char *data; unsigned int size; void *session_data; };
enum httpd_uri_handler_status { TEST_HTTP_CB_NEW=HTTP_CB_NEW };
typedef void (*uri_handler_t)(enum httpd_uri_handler_status, struct httpd_request *, struct httpd_response *);
static const char *route;
static uri_handler_t handler;
static unsigned int reads, flash_reads, flash_writes, env_writes;
static bool available;
static uchar value[6];
static bool is_valid_ethaddr(const uchar *m)
{
 unsigned int i;
 if (!m || (m[0]&1)) return false;
 for(i=0;i<6;i++) if(m[i]) return true;
 return false;
}
static int eth_getenv_enetaddr(const char *name, uchar *mac)
{
 assert(strcmp(name,"ethaddr")==0); reads++;
 if(!available) return 0;
 memcpy(mac,value,6); return is_valid_ethaddr(mac);
}
static void httpd_register_uri_handler(struct httpd_instance *i,const char *u,uri_handler_t h,void *d)
{ (void)i;(void)d;route=u;handler=h; }
#include "../failsafe/modules/mac_h3c_hw_a.c"
static struct httpd_response call(int method)
{
 struct httpd_request q={.method=method}; struct httpd_response r;
 memset(&r,0,sizeof(r)); handler(HTTP_CB_NEW,&q,&r); return r;
}
int main(void)
{
 struct httpd_instance i={0}; struct httpd_response r;
 mac_register_uri_handlers(&i); assert(route && strcmp(route,"/mac/info")==0);
 r=call(HTTP_POST); assert(r.info.code==405 && reads==0);
 available=true; value[0]=0x10;value[1]=0x9f;value[2]=0x4f;value[3]=0xca;value[4]=0xa5;value[5]=0x10;
 r=call(HTTP_GET); assert(r.info.code==200 && r.size==strlen(r.data));
 assert(strstr(r.data,"\"part_name\":\"0:APPSBLENV\""));
 assert(strstr(r.data,"\"source\":\"uboot_environment\""));
 assert(strstr(r.data,"\"variable\":\"ethaddr\""));
 assert(strstr(r.data,"\"writable\":false") && strstr(r.data,"\"persistent\":false"));
 assert(strstr(r.data,"\"read_only\":true") && strstr(r.data,"\"available\":true"));
 assert(strstr(r.data,"10:9F:4F:CA:A5:10"));
 available=false;r=call(HTTP_GET);
 assert(r.info.code==200 && strstr(r.data,"\"available\":false"));
 assert(strstr(r.data,"\"macs\":[]") && strstr(r.data,"no_valid_ram_ethaddr"));
 available=true;value[0]=0;value[1]=0x11;value[2]=0x22;value[3]=0x33;value[4]=0x44;value[5]=0x55;
 r=call(HTTP_GET);assert(strstr(r.data,"\"available\":false") && strstr(r.data,"\"macs\":[]"));
 assert(flash_reads==0 && flash_writes==0 && env_writes==0);
 puts("HW-A MAC production-handler read-only host tests: PASS"); return 0;
}
