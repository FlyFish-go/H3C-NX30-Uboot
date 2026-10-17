#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#include <common.h>
#include <net/httpd.h>
#include <failsafe/failsafe.h>
#include <rt3000_b_metadata.h>
#include <net/tcp.h>
struct host_uri uris[8];unsigned int uri_count;
static int staged,write_calls,invalidations;
static unsigned int initramfs_invalidations;
void failsafe_initramfs_invalidate(void){initramfs_invalidations++;}
bool failsafe_initramfs_is_validated(ulong data_addr,ulong data_size){(void)data_addr;(void)data_size;return false;}
void handle_start_led_state(void){} void handle_success_led_state(void){} void handle_fail_led_state(void){}
void tcp_close_all_conn(void){}
void httpd_register_uri_handler(struct httpd_instance*i,const char*p,httpd_uri_callback cb,void*d){(void)i;(void)d;uris[uri_count++]=(struct host_uri){p,cb};}
struct httpd_form_value *httpd_request_find_value(struct httpd_request*r,const char*n){unsigned i;for(i=0;i<r->form.count;i++)if(r->form.values[i].name&&!strcmp(r->form.values[i].name,n))return &r->form.values[i];return NULL;}
unsigned int http_make_response_header(struct httpd_response_info*i,char*b,unsigned int n){(void)i;(void)b;(void)n;return 0;}
int failsafe_validate_image(upgrade_type_t t,const char*f,enum rt3_md_target mt,const void*d,unsigned long n,struct httpd_response*r){(void)mt;(void)t;(void)f;(void)d;(void)n;staged=1;r->data="{\"status\":\"success\",\"info\":{\"target\":\"0:APPSBL\",\"commit_token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}";r->size=strlen(r->data);return RET_SUCCESS;}
int rt3_metadata_target_parse(const char*d,unsigned int n,enum rt3_md_target*t){if(n==5&&!memcmp(d,"0:CDT",5)){*t=RT3_MD_TARGET_PRIMARY;return 0;}if(n==7&&!memcmp(d,"0:CDT_1",7)){*t=RT3_MD_TARGET_SECONDARY;return 0;}return -1;}
int rt3_metadata_write(enum rt3_md_kind k,const char*token,struct httpd_response*r){return rt3_metadata_write_target(k,RT3_MD_TARGET_DEFAULT,token,r);}
int failsafe_write_image(upgrade_type_t t,unsigned long d,unsigned long n,struct httpd_response*r){(void)t;(void)d;(void)n;(void)r;return RET_FAILURE;}
void rt3_metadata_invalidate(void){staged=0;invalidations++;}
int rt3_metadata_write_target(enum rt3_md_kind k,enum rt3_md_target target,const char*token,struct httpd_response*r){(void)target;(void)k;write_calls++;if(!staged||!token||strcmp(token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")){r->status=HTTP_RESP_STD;r->data="{\"status\":\"fail\",\"info\":{\"type\":\"stale_or_mismatched_commit\"}}";r->size=strlen(r->data);return -1;}staged=0;return 0;}
#include "../failsafe/modules/upgrade.c"
static struct host_uri *find(const char*p){unsigned i;for(i=0;i<uri_count;i++)if(!strcmp(uris[i].path,p))return &uris[i];return NULL;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 struct httpd_instance inst={0};struct httpd_request up={0},wr={0};struct httpd_response r={0};struct host_uri*u,*w;char bytes[]="ELF";
 upgrade_register_uri_handlers(&inst);u=find("/upload");w=find("/write");
 CHECK(u&&w&&find("/result")&&w->cb==find("/result")->cb);
 up.form.count=1;up.form.values[0]=(struct httpd_form_value){"uboot",bytes,"x.mbn",3};
 u->cb(HTTP_CB_NEW,&up,&r);u->cb(HTTP_CB_RESPONDING,&up,&r);CHECK(staged&&strstr(r.data,"commit_token"));u->cb(HTTP_CB_CLOSED,&up,&r);
 wr.form.count=2;wr.form.values[0]=(struct httpd_form_value){"commit_token","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",NULL,64};wr.form.values[1]=(struct httpd_form_value){"auto_reboot","true",NULL,4};
 memset(&r,0,sizeof(r));w->cb(HTTP_CB_NEW,&wr,&r);w->cb(HTTP_CB_RESPONDING,&wr,&r);CHECK(write_calls==1);CHECK(strstr(r.data,"\"reboot\":false"));w->cb(HTTP_CB_CLOSED,&wr,&r);CHECK(!auto_action_pending);
 memset(&r,0,sizeof(r));struct host_uri*legacy=find("/result");upload_data_id=fs_upload_id;legacy->cb(HTTP_CB_NEW,&wr,&r);legacy->cb(HTTP_CB_RESPONDING,&wr,&r);CHECK(write_calls==2);CHECK(r.status==HTTP_RESP_CUSTOM);CHECK(strstr(r.data,"stale_or_mismatched_commit"));legacy->cb(HTTP_CB_RESPONDING,&wr,&r);CHECK(r.status==HTTP_RESP_NONE);legacy->cb(HTTP_CB_CLOSED,&wr,&r);
 wr.form.count=1;wr.form.values[0].name="auto_reboot";wr.form.values[0].data="true";wr.form.values[0].size=4;
 memset(&r,0,sizeof(r));upload_data_id=fs_upload_id;w->cb(HTTP_CB_NEW,&wr,&r);w->cb(HTTP_CB_RESPONDING,&wr,&r);CHECK(strstr(r.data,"stale_or_mismatched_commit"));CHECK(!auto_action_pending);w->cb(HTTP_CB_CLOSED,&wr,&r);
 up.form.count=2;memset(&r,0,sizeof(r));u->cb(HTTP_CB_NEW,&up,&r);u->cb(HTTP_CB_RESPONDING,&up,&r);CHECK(!staged&&strstr(r.data,"invalid_upload_fields"));u->cb(HTTP_CB_CLOSED,&up,&r);
 puts("RT3000-B upgrade callback route/token tests: PASS");return 0;
}
