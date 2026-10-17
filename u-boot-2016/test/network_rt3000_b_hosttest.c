#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <net/httpd.h>
const char *host_getenv(const char *);
int host_setenv(const char *,const char *);
int host_saveenv(void);
#define getenv host_getenv
#define setenv host_setenv
#define saveenv host_saveenv
#include "../failsafe/modules/network.c"
#undef getenv
#undef setenv
#undef saveenv
struct kv { const char *key; char val[32]; };
static struct kv vars[]={{"ipaddr","192.168.1.1"},{"netmask","255.255.255.0"},{"serverip","192.168.1.2"},{"custom_network","0"}};
static int sets,saves;
static httpd_uri_handler_cb set_cb;
const char *host_getenv(const char *key) { unsigned int i; for(i=0;i<4;i++) if(!strcmp(vars[i].key,key)) return vars[i].val; return NULL; }
int host_setenv(const char *key,const char *value) { unsigned int i; sets++; for(i=0;i<4;i++) if(!strcmp(vars[i].key,key)) { if(!value)vars[i].val[0]=0;else snprintf(vars[i].val,32,"%s",value);return 0;}return -1; }
int host_saveenv(void) { saves++;return 0; }
int httpd_register_uri_handler(struct httpd_instance *x,const char *uri,httpd_uri_handler_cb cb,void *r) { (void)x;(void)r;if(!strcmp(uri,"/network/set"))set_cb=cb;return 0; }
struct httpd_form_value *httpd_request_find_value(struct httpd_request *r,const char *name) { unsigned int i;for(i=0;i<r->form.count;i++)if(r->form.values[i].name&&!strcmp(r->form.values[i].name,name))return &r->form.values[i];return NULL; }
static struct httpd_form_value fv(const char *n,const char *d,size_t z) { struct httpd_form_value v={n,d,NULL,z};return v; }
static void call(struct httpd_form_value *v,unsigned int n,unsigned int code,int mut) { struct httpd_request q={0};struct httpd_response r={0};q.method=HTTP_POST;q.form.count=n;memcpy(q.form.values,v,n*sizeof(*v));sets=saves=0;set_cb(HTTP_CB_NEW,&q,&r);assert(r.info.code==code);assert(sets==(mut?2:0));assert(saves==(mut?1:0)); }
int main(void) {
 struct httpd_instance *inst=NULL;struct httpd_form_value v[3];
 network_register_uri_handlers(inst);assert(set_cb);
 v[0]=fv("ipaddr","999.999.999.999",15);v[1]=fv("netmask","255.255.255.0",13);v[2]=fv("serverip","192.168.10.3",12);call(v,3,400,0);
 v[0]=fv("ipaddr","192.168.10.2",12);v[1]=fv("netmask","255.0.255.0",11);v[2]=fv("serverip","192.168.10.3",12);call(v,3,400,0);
 v[0]=fv("ipaddr","192.168.10.2x",13);v[1]=fv("netmask","255.255.255.0",13);v[2]=fv("serverip","192.168.10.3",12);call(v,3,400,0);
 v[0]=fv("ipaddr","192.168.10.2",12);v[1]=fv("netmask","255.0.255.0",11);v[2]=fv("serverip","192.168.10.3",12);call(v,3,400,0);
 
 
 
 v[0]=fv("ipaddr","192.168.10.2",12);v[1]=fv("netmask","255.255.255.0",13);call(v,2,400,0);
 v[0]=fv("ipaddr","192.168.10.2",12);v[1]=fv("netmask","255.255.255.0",13);v[2]=fv("serverip","192.168.10.3",12);call(v,3,200,1);
 assert(!strcmp(host_getenv("ipaddr"),"192.168.10.2"));assert(!strcmp(host_getenv("serverip"),"192.168.10.3"));
 puts("network handler host tests: PASS");return 0;
}
