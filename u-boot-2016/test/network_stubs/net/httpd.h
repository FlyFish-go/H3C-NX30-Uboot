#ifndef STUB_HTTPD_H
#define STUB_HTTPD_H
#include <stddef.h>
#include <stdint.h>
typedef unsigned int u32;
#define MAX_HTTP_FORM_VALUE_ITEMS 5
struct httpd_form_value { const char *name,*data,*filename; size_t size; };
struct httpd_form_values { u32 count; struct httpd_form_value values[MAX_HTTP_FORM_VALUE_ITEMS]; };
struct httpd_instance;
enum httpd_request_method { HTTP_GET,HTTP_POST };
enum httpd_uri_handler_status { HTTP_CB_NEW,HTTP_CB_RESPONDING,HTTP_CB_CLOSED };
struct httpd_request { enum httpd_request_method method; const void *urih; struct httpd_form_values form; };
enum httpd_response_status { HTTP_RESP_NONE,HTTP_RESP_STD,HTTP_RESP_CUSTOM };
struct http_response_info { u32 code; const char *content_type; int content_length; const char *location; int connection_close; };
struct httpd_response { enum httpd_response_status status; struct http_response_info info; const char *data; u32 size; };
typedef void (*httpd_uri_handler_cb)(enum httpd_uri_handler_status,struct httpd_request *,struct httpd_response *);
int httpd_register_uri_handler(struct httpd_instance *,const char *,httpd_uri_handler_cb,void *);
struct httpd_form_value *httpd_request_find_value(struct httpd_request *,const char *);
#endif
