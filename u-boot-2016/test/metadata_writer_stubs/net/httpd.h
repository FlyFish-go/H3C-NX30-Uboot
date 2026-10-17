#ifndef RT3_TEST_HTTPD_H
#define RT3_TEST_HTTPD_H
#include <stdint.h>
struct httpd_instance;
ulong httpd_upload_buffer_base(void);
struct httpd_request { int method; };
struct http_response_info { unsigned int code; int connection_close; const char *content_type; };
enum httpd_response_status { HTTP_RESP_STD };
struct httpd_response { enum httpd_response_status status; struct http_response_info info; const char *data; unsigned int size; };
enum httpd_uri_handler_status { HTTP_CB_NEW, HTTP_CB_RESPONDING, HTTP_CB_CLOSED };
#define HTTP_GET 0
typedef void (*httpd_uri_handler_cb)(enum httpd_uri_handler_status,struct httpd_request*,struct httpd_response*);
int httpd_register_uri_handler(struct httpd_instance*,const char*,httpd_uri_handler_cb,void*);

#endif
