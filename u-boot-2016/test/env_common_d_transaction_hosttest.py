#!/usr/bin/env python3
"""Exercise production H3C common HW-D ENV transaction helpers with a RAM mock."""
import pathlib, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
src=(ROOT/"failsafe/modules/env.c").read_text()
def c_function(signature):
 start=src.index(signature); brace=src.index("{",start); depth=0
 for end in range(brace,len(src)):
  if src[end]=="{": depth+=1
  elif src[end]=="}":
   depth-=1
   if depth==0:return src[start:end+1]
 raise AssertionError("unterminated "+signature)
functions="\n".join(c_function(x) for x in [
 "static int rt3_env_name_valid(","static int rt3_env_name_protected(","static int rt3_env_value_valid(",
 "static int rt3_env_protected_capture(","static void rt3_env_protected_free(","static int rt3_env_protected_equal(",
 "static int rt3_env_protected_restore(","static bool failsafe_env_ram_ready(","static env_t *rt3_env_snapshot(","static int rt3_env_rollback_ram(",
 "static int rt3_env_transaction(","static int rt3_env_mutate_set(","static int rt3_env_mutate_unset(",
 "static int rt3_env_mutate_reset_single(","static int rt3_env_mutate_reset_all(","static int rt3_env_mutate_import(",
 "int failsafe_env_set_persistent(","static int failsafe_env_get_form_value(",
 "static void failsafe_http_reply_text(","static int rt3_env_form_schema(",
 "static void env_set_handler(","static void env_unset_handler(",
 "static void env_reset_all_handler(","static void env_reset_single_handler(",
 "static void env_restore_handler(","static void env_info_handler("])

c=r'''#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;
typedef int bool;
#define true 1
#define false 0
#define ENV_NAME_MAX_LEN 128
#define CONFIG_H3C_HW_D_PRODUCTION_PORT 1
#define CONFIG_IPQ5018_ENV 1
#define RT3_ENV_BOARD_LABEL "H3C HW-B"
#define MAX_HTTP_FORM_VALUE_ITEMS 5
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
enum httpd_request_method { HTTP_GET, HTTP_POST };
enum httpd_uri_handler_status { HTTP_CB_NEW, HTTP_CB_RESPONDING, HTTP_CB_CLOSED };
enum httpd_response_status { HTTP_RESP_NONE, HTTP_RESP_STD };
struct httpd_form_value { const char *name,*data,*filename; size_t size; };
struct httpd_form_values { u32 count; struct httpd_form_value values[MAX_HTTP_FORM_VALUE_ITEMS]; };
struct httpd_request { enum httpd_request_method method; struct httpd_form_values form; };
struct http_response_info { u32 code; const char *content_type; int connection_close; };
struct httpd_response { enum httpd_response_status status; struct http_response_info info; const char *data; u32 size; void *session_data; };
typedef struct { uint32_t crc; unsigned char data[(256U<<10)-4]; } env_t;
#define CONFIG_ENV_SIZE (256U<<10)
#define ENV_SIZE (CONFIG_ENV_SIZE-sizeof(uint32_t))
#define ARCH_DMA_MINALIGN 64
#define GD_FLG_ENV_READY 1U
#define DECLARE_GLOBAL_DATA_PTR
struct gd_state { unsigned flags; } gd_state, *gd=&gd_state;
struct { void *table; } env_htab;
void *malloc(size_t); void free(void *); char *strdup(const char *);
char *getenv(const char *); int setenv(const char *,const char *);
static int saveenv_fail, saveenv_calls;
static int env_export(env_t *);
static int env_import(const char *, int);
static int mock_saveenv(void) { saveenv_calls++; return saveenv_fail ? -1 : 0; }
static int (*saveenv)(void)=mock_saveenv;
static int tail_mismatch;
int rt3_nand_env_tail_matches(const void *tail,size_t size) { (void)tail;(void)size;return tail_mismatch ? -EINVAL : 0; }
static const char *getenv_default(const char *);
static void set_default_env(const char *);
#define T(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return __LINE__; } } while (0)
struct item { char key[128]; char value[1024]; } current[32], defaults[32];
static unsigned current_n, default_n;
static int find(struct item*a,unsigned n,const char*k){unsigned i;for(i=0;i<n;i++)if(!strcmp(a[i].key,k))return(int)i;return-1;}
char *getenv(const char*k){int i=find(current,current_n,k);return i<0?NULL:current[i].value;}
int setenv(const char*k,const char*v){int i=find(current,current_n,k);if(i<0){if(current_n==32)return-1;i=(int)current_n++;strncpy(current[i].key,k,sizeof(current[i].key)-1);}if(!v){current[i]=current[--current_n];return 0;}if(strlen(v)>=sizeof(current[i].value))return-1;strcpy(current[i].value,v);return 0;}
static const char *getenv_default(const char*k){int i=find(defaults,default_n,k);return i<0?NULL:defaults[i].value;}
static void set_default_env(const char*x){(void)x;memcpy(current,defaults,sizeof(current));current_n=default_n;}
static void add_default(const char*k,const char*v){int i=(int)default_n++;strcpy(defaults[i].key,k);strcpy(defaults[i].value,v);}
static uint32_t crc32(uint32_t seed,const unsigned char*p,size_t n){uint32_t c=~seed;size_t i;int b;for(i=0;i<n;i++){c^=p[i];for(b=0;b<8;b++)c=(c>>1)^((uint32_t)-(int)(c&1)&0xedb88320U);}return~c;}
static int env_export(env_t*e){size_t pos=0;unsigned i;memset(e,0,CONFIG_ENV_SIZE);for(i=0;i<current_n;i++){size_t a=strlen(current[i].key),b=strlen(current[i].value);if(pos+a+b+2>sizeof(e->data))return-1;memcpy(e->data+pos,current[i].key,a);pos+=a;e->data[pos++]='=';memcpy(e->data+pos,current[i].value,b);pos+=b;e->data[pos++]=0;}e->data[pos++]=0;e->crc=crc32(0,e->data,ENV_SIZE);return 0;}
static int env_import(const char*raw,int flags){const env_t*e=(const env_t*)raw;size_t pos=0;unsigned char*d=(unsigned char*)e->data;(void)flags;if(crc32(0,e->data,ENV_SIZE)!=e->crc)return 0;current_n=0;while(pos<ENV_SIZE&&d[pos]){size_t elen=strlen((char*)d+pos),k,v;char*eq=(char*)memchr(d+pos,'=',elen);if(!eq)return 0;k=(size_t)(eq-((char*)d+pos));v=elen-k-1;if(k>=128||v>=1024||current_n>=32)return 0;memcpy(current[current_n].key,d+pos,k);current[current_n].key[k]=0;memcpy(current[current_n].value,eq+1,v);current[current_n].value[v]=0;current_n++;pos+=elen+1;}return 1;}
static void *memalign(size_t a,size_t n){void*p=NULL;(void)a;extern int posix_memalign(void**,size_t,size_t);if(posix_memalign(&p,64,n))return NULL;return p;}
typedef int (*rt3_env_mutator_fn)(void*);
#define RT3_ENV_PROTECTED_COUNT 6
struct rt3_env_protected_snapshot { char *value[RT3_ENV_PROTECTED_COUNT]; };
static const char * const rt3_env_protected_names[RT3_ENV_PROTECTED_COUNT]={"bootcmd","bootargs","bootdelay","ethact","mtdparts","mtdids"};
struct rt3_env_pair { const char *name; const char *value; };
''' + functions + r'''
static void seed(void){current_n=default_n=0;saveenv_calls=saveenv_fail=0;add_default("bootcmd","factory-boot");add_default("bootargs","factory-args");add_default("bootdelay","3");add_default("ethact","eth0");add_default("mtdparts","factory-parts");add_default("mtdids","nand0=nand0");add_default("version","default");setenv("bootcmd","current-boot");setenv("bootargs","current-args");setenv("bootdelay","5");setenv("ethact","eth1");setenv("mtdparts","current-parts");setenv("mtdids","nand0=nand0");setenv("version","old");gd->flags=GD_FLG_ENV_READY;env_htab.table=current;}
int main(void){env_t restore;seed();
 { struct httpd_request req; struct httpd_response resp; const char *field;
   memset(&req,0,sizeof(req));memset(&resp,0,sizeof(resp));req.method=HTTP_GET;
   env_info_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==200);field=strstr(resp.data,"persistent_write_supported");T(field);T(strstr(field,"true"));
   saveenv=NULL;
   T(failsafe_env_set_persistent("version","blocked")==-EROFS);T(saveenv_calls==0);T(!strcmp(getenv("version"),"old"));
   memset(&resp,0,sizeof(resp));env_info_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==200);field=strstr(resp.data,"persistent_write_supported");T(field);T(strstr(field,"false"));
   req.method=HTTP_POST;req.form.count=2;req.form.values[0]=(struct httpd_form_value){"name","version",NULL,7};req.form.values[1]=(struct httpd_form_value){"value","blocked",NULL,7};
   memset(&resp,0,sizeof(resp));env_set_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==500);T(!strcmp(getenv("version"),"old"));T(saveenv_calls==0);
   saveenv=mock_saveenv;
 }
 T(failsafe_env_set_persistent("version","new")==0);T(!strcmp(getenv("version"),"new"));T(saveenv_calls==1);
 T(failsafe_env_set_persistent("version","new")==0);T(saveenv_calls==1);
 T(failsafe_env_set_persistent("bootcmd","evil")<0);T(!strcmp(getenv("bootcmd"),"current-boot"));
 T(failsafe_env_set_persistent("bad.name","x")<0);T(failsafe_env_set_persistent("version","bad\nvalue")<0);
 saveenv_fail=1;T(failsafe_env_set_persistent("version","failed")<0);T(!strcmp(getenv("version"),"new"));saveenv_fail=0;
 T(rt3_env_transaction(rt3_env_mutate_unset,"version",0)==0);T(getenv("version")==NULL);
 T(rt3_env_transaction(rt3_env_mutate_reset_single,"version",0)==0);T(!strcmp(getenv("version"),"default"));
 T(rt3_env_transaction(rt3_env_mutate_reset_all,NULL,1)==0);T(!strcmp(getenv("version"),"default"));T(!strcmp(getenv("bootcmd"),"current-boot"));T(!strcmp(getenv("bootargs"),"current-args"));T(!strcmp(getenv("bootdelay"),"5"));T(!strcmp(getenv("ethact"),"eth1"));T(!strcmp(getenv("mtdparts"),"current-parts"));T(!strcmp(getenv("mtdids"),"nand0=nand0"));
 setenv("version","before-reset-failure");saveenv_fail=1;T(rt3_env_transaction(rt3_env_mutate_reset_all,NULL,1)<0);T(!strcmp(getenv("version"),"before-reset-failure"));T(!strcmp(getenv("bootcmd"),"current-boot"));saveenv_fail=0;
 memset(&restore,0,sizeof(restore));strcpy((char*)restore.data,"bootcmd=evil\0version=restored\0\0");restore.crc=crc32(0,restore.data,ENV_SIZE);
 T(rt3_env_transaction(rt3_env_mutate_import,&restore,0)==-EPERM);T(!strcmp(getenv("bootcmd"),"current-boot"));T(!strcmp(getenv("version"),"before-reset-failure"));
 setenv("version","restored");T(env_export(&restore)==0);setenv("version","before-reset-failure");
 T(rt3_env_transaction(rt3_env_mutate_import,&restore,0)==0);T(!strcmp(getenv("version"),"restored"));
 { struct httpd_request req; struct httpd_response resp; const char *nm="version",*vl="http-value";
   seed();memset(&req,0,sizeof(req));memset(&resp,0,sizeof(resp));req.method=HTTP_POST;req.form.count=2;
   req.form.values[0]=(struct httpd_form_value){"name",nm,NULL,strlen(nm)};req.form.values[1]=(struct httpd_form_value){"value",vl,NULL,strlen(vl)};
   env_set_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==200);T(!strcmp(getenv("version"),"http-value"));
   memset(&resp,0,sizeof(resp));req.form.count=3;req.form.values[2]=req.form.values[0];env_set_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==400);
   memset(&resp,0,sizeof(resp));req.form.count=2;{char binary[3]={'a',0,'b'};req.form.values[1].data=binary;req.form.values[1].size=sizeof(binary);env_set_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==400);}
   memset(&resp,0,sizeof(resp));req.form.values[0].data="bootcmd";req.form.values[0].size=7;req.form.values[1].data=vl;req.form.values[1].size=strlen(vl);env_set_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==403);T(!strcmp(getenv("bootcmd"),"current-boot"));
   memset(&resp,0,sizeof(resp));req.method=HTTP_GET;env_set_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==405);
   req.method=HTTP_POST;req.form.count=1;req.form.values[0]=(struct httpd_form_value){"name","version",NULL,7};memset(&resp,0,sizeof(resp));env_unset_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==200);T(getenv("version")==NULL);
   req.form.values[0]=(struct httpd_form_value){"name","missing",NULL,7};memset(&resp,0,sizeof(resp));env_reset_single_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==406);
   req.form.count=0;memset(&resp,0,sizeof(resp));env_reset_all_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==200);T(!strcmp(getenv("bootcmd"),"current-boot"));
   req.form.count=1;req.form.values[0]=(struct httpd_form_value){"unexpected","x",NULL,1};memset(&resp,0,sizeof(resp));env_reset_all_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==400);
   seed();setenv("version","uploaded");T(env_export(&restore)==0);setenv("version","before-upload");
   req.form.values[0]=(struct httpd_form_value){"envfile",(const char *)&restore,NULL,CONFIG_ENV_SIZE};memset(&resp,0,sizeof(resp));env_restore_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==200);T(!strcmp(getenv("version"),"uploaded"));
   restore.crc^=1;setenv("version","untouched");memset(&resp,0,sizeof(resp));env_restore_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==400);T(!strcmp(getenv("version"),"untouched"));restore.crc^=1;
   req.form.values[0].size=CONFIG_ENV_SIZE-1;memset(&resp,0,sizeof(resp));env_restore_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==400);
   { unsigned char *full=malloc(2*CONFIG_ENV_SIZE);T(full!=NULL);memcpy(full,&restore,CONFIG_ENV_SIZE);memset(full+CONFIG_ENV_SIZE,0x5a,CONFIG_ENV_SIZE);req.form.values[0].data=(const char *)full;req.form.values[0].size=2*CONFIG_ENV_SIZE;tail_mismatch=1;memset(&resp,0,sizeof(resp));env_restore_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==400);T(!strcmp(getenv("version"),"untouched"));tail_mismatch=0;memset(&resp,0,sizeof(resp));env_restore_handler(HTTP_CB_NEW,&req,&resp);T(resp.info.code==200);T(!strcmp(getenv("version"),"uploaded"));free(full);}
 }
 puts("ENV_TRANSACTION_PASS: production ENV callbacks and transaction helpers, protected variables, validation, and RAM rollback");return 0;}
'''
with tempfile.TemporaryDirectory(prefix="rt3-env-transaction-") as td:
 f=pathlib.Path(td)/"test.c";exe=pathlib.Path(td)/"test";f.write_text(c)
 subprocess.run(["gcc","-std=gnu99","-Wall","-Wextra","-Werror","-Wno-unused-function","-Wno-sign-compare",str(f),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print("ENV_TRANSACTION_SOURCE_PASS")
