#ifndef HOST_HTTPD_H
#define HOST_HTTPD_H
enum httpd_uri_handler_status { HTTP_CB_NEW, HTTP_CB_RESPONDING, HTTP_CB_CLOSED };
enum httpd_response_status { HTTP_RESP_NONE, HTTP_RESP_CUSTOM, HTTP_RESP_STD };
struct httpd_form_value { const char *name; const char *data; const char *filename; unsigned int size; };
struct httpd_form_values { unsigned int count; struct httpd_form_value values[8]; };
struct httpd_request { struct httpd_form_values form; };
struct httpd_response_info { int http_1_0; int content_length; int connection_close; const char *content_type; int code; };
struct httpd_response { enum httpd_response_status status; struct httpd_response_info info; const char *data; unsigned int size; void *session_data; };
struct httpd_instance { int unused; };
typedef void (*httpd_uri_callback)(enum httpd_uri_handler_status,struct httpd_request *,struct httpd_response *);
struct host_uri { const char *path; httpd_uri_callback cb; };
struct httpd_form_value *httpd_request_find_value(struct httpd_request *,const char *);
unsigned int http_make_response_header(struct httpd_response_info *,char *,unsigned int);
void httpd_register_uri_handler(struct httpd_instance *,const char *,httpd_uri_callback,void *);
#endif
