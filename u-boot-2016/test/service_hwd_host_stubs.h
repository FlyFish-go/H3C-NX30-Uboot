#ifndef SERVICE_HWD_HOST_STUBS_H
#define SERVICE_HWD_HOST_STUBS_H
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "service_hwd_host_api.h"
#define CONFIG_H3C_HW_D_SERVICE_RAM 1
#define SHA256_SUM_LEN 32
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min_t(type,a,b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define get_timer(base) ((unsigned long)((base) + 1))
static inline size_t service_host_strlcpy(char *dst,const char *src,size_t size) {
 size_t n=strlen(src); if(size){size_t c=n>=size?size-1:n;memcpy(dst,src,c);dst[c]=0;}return n;
}
#define strlcpy service_host_strlcpy
static inline unsigned long simple_strtoul(const char *s,char **end,unsigned int base) { return strtoul(s,end,base); }
#define setenv service_host_setenv
static inline int service_host_setenv(const char *name,const char *value) { (void)name;(void)value;return 0; }
enum httpd_method { HTTP_GET, HTTP_POST };
enum httpd_uri_handler_status { HTTP_CB_NEW, HTTP_CB_RESPONDING, HTTP_CB_CLOSED };
enum httpd_response_status { HTTP_RESP_NONE, HTTP_RESP_STD, HTTP_RESP_CUSTOM };
struct httpd_form_value { const char *name; const char *data; const char *filename; size_t size; };
struct httpd_form_values { u32 count; struct httpd_form_value values[5]; };
struct httpd_request { enum httpd_method method; struct httpd_form_values form; };
struct http_response_info { u32 code; const char *content_type; int content_length; const char *location; int connection_close; int chunked_encoding; int http_1_0; const char *content_encoding; const char *vary; };
struct httpd_response { enum httpd_response_status status; const char *data; u32 size; struct http_response_info info; void *session_data; };
struct httpd_instance { int unused; };
static inline struct httpd_form_value *httpd_request_find_value(struct httpd_request *r,const char *name) {
 size_t i; for(i=0;i<r->form.count;i++) if(r->form.values[i].name && !strcmp(r->form.values[i].name,name)) return &r->form.values[i]; return NULL;
}
typedef void (*httpd_handler_fn)(enum httpd_uri_handler_status,struct httpd_request *,struct httpd_response *);
static inline void httpd_register_uri_handler(struct httpd_instance *i,const char *p,httpd_handler_fn h,void *d) {(void)i;(void)p;(void)h;(void)d;}
static inline u32 http_make_response_header(struct http_response_info *info,char *buffer,u32 size) {
 int n=snprintf(buffer,size,"HTTP/1.0 %u OK\r\nContent-Type: %s\r\n\r\n",info->code,info->content_type); return n>0?(u32)n:0;
}
struct in_addr { u32 s_addr; };
extern struct in_addr net_ip, net_netmask, net_server_ip;
static inline struct in_addr string_to_ip(const char *s) { struct in_addr a={0}; if(s) a.s_addr=(u32)strtoul(s,NULL,10); return a; }
typedef struct { u32 state; } sha256_context;
static inline void sha256_starts(sha256_context *c) { c->state=2166136261U; }
static inline void sha256_update(sha256_context *c,const unsigned char *b,unsigned int n) { unsigned int i; for(i=0;i<n;i++) c->state=(c->state^b[i])*16777619U; }
static inline void sha256_finish(sha256_context *c,unsigned char *out) { unsigned int i; for(i=0;i<SHA256_SUM_LEN;i++) out[i]=(u8)(c->state>>(8*(i%4)))^(u8)i; }
int nx30_d_layout_get(struct nx30_d_layout *);
int nx30_d_slots_get(struct nx30_d_slots *);
int nx30_d_object_extent(enum nx30_d_object, struct nx30_d_extent *);
int nx30_d_read_object(enum nx30_d_object,u64,void *,size_t,size_t *);
int nx30_d_boot_slot(enum nx30_d_slot);
int nx30_d_default_prepare(enum nx30_d_slot,char [NX30_D_TOKEN_LEN],struct nx30_d_result *);
int nx30_d_default_commit(const char *,struct nx30_d_result *);
int nx30_d_firmware_validate(const void *,size_t,struct nx30_d_firmware_info *);
int nx30_d_firmware_write_inactive(const void *,size_t,u32,u32,struct nx30_d_result *);
int nx30_d_env_get(struct nx30_d_env_snapshot *);
int nx30_d_env_prepare(const struct nx30_d_env_change *,size_t,u32,char [NX30_D_TOKEN_LEN],struct nx30_d_result *);
int nx30_d_env_commit(const char *,struct nx30_d_result *);
void nx30_d_env_abort(const char *);
int nx30_d_uboot_high_copy_active(void);
int nx30_d_uboot_validate(const void *,size_t,struct nx30_d_firmware_info *);
int nx30_d_uboot_write(const void *,size_t,u32,u32,struct nx30_d_result *);
int gpio_get_value(unsigned int gpio);
void led_on(const char *name);
void led_off(const char *name);
int run_command(const char *cmd,int flag);
#endif
