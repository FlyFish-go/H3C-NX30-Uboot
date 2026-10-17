#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
void *malloc(size_t); void free(void *); int posix_memalign(void **,size_t,size_t);
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
#define main layout_fixture_main
#include "rt3000_b_layout_hosttest.c"
#undef main

static unsigned char flash[0x200000];
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
static int mock_retained_failure;
#endif
static int erase_count, write_count, read_count, bad_count, open_count, close_count;
static uint64_t erase_log[16];
static int fail_erase_at = -1, fail_write_at = -1, short_write_at = -1;
static int fail_read_after_writes = -1, mismatch_read_after_writes = -1, fail_read_at = -1, short_read_at = -1, mismatch_read_at = -1;
static int badblock_at = -1, open_fail, mutate_owner_on_open, mutate_layout_on_open;
static int mutate_primary_after_first_close, mutate_layout_after_first_close;
static int alloc_calls, fail_alloc_at = -1, window_active, mutate_flash_at_read = -1; static uint64_t mutate_flash_offset;
static char *bootcmd_value = "bootipq";
static const char *registered[8]; static unsigned registered_count;

static void *host_malloc(size_t n) { if (++alloc_calls == fail_alloc_at) return NULL; return malloc(n); }
static void host_free(void *p) { free(p); }
static void *host_memalign(size_t a, size_t n) { void *p=NULL; if (++alloc_calls == fail_alloc_at) return NULL; return posix_memalign(&p,a,n) ? NULL : p; }
static const char *host_getenv(const char *name) { return !strcmp(name,"bootcmd") ? bootcmd_value : NULL; }

#define malloc host_malloc
#define free host_free
#define memalign host_memalign
#define getenv host_getenv
#include "boot_default_rt3000_b.c"
#undef malloc
#undef free
#undef memalign
#undef getenv
#include "boot_default_rt3000_b_core.c"

#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
int nx30_d_default_failure_get(struct nx30_d_result *r,u32 *copy)
{ memset(r,0,sizeof(*r));*copy=1;r->status=-EIO;r->stage=5;return mock_retained_failure?0:-ENOENT; }
int nx30_d_default_original_get(unsigned int copy,const void **bytes,size_t *length)
{ if(!mock_retained_failure)return -ENOENT;*bytes=flash+0x100000+copy*BC_SIZE;*length=BC_SIZE;return 0; }
#endif

int getpart_offset_size(char *name, unsigned int *off, unsigned int *size)
{
 if (!strcmp(name,"0:BOOTCONFIG")) { *off=0x100000; *size=0x40000; return 0; }
 if (!strcmp(name,"0:BOOTCONFIG1")) { *off=0x140000; *size=0x40000; return 0; }
 return -1;
}
int nand_block_isbad(struct mtd_info *m, uint64_t off)
{ (void)m; bad_count++; return off==(uint64_t)badblock_at ? 1 : 0; }
int rt3_bootconfig_write_window_open(struct mtd_info *m,const char *name,uint64_t off,uint64_t size)
{ return rt3_named_nand_write_window_open(m, name, off, size); }
int rt3_named_nand_write_window_open(struct mtd_info *m,const char *name,uint64_t off,uint64_t size)
{
 open_count++;
 if (open_fail || m != &nand_info[0] || size != BC_SIZE ||
     (strcmp(name,"0:BOOTCONFIG") && strcmp(name,"0:BOOTCONFIG1")) ||
     (off != 0x100000 && off != 0x140000)) return -EACCES;
 window_active=1;
 if (mutate_owner_on_open) { flash[off+0x240] ^= 0x20; mutate_owner_on_open=0; }
 if (mutate_layout_on_open) { qca_smem_flash_info.flash_secondary_type++; mutate_layout_on_open=0; }
 return 0;
}
void rt3_fw_write_window_close(void)
{
 close_count++; window_active=0;
 if (close_count==1 && mutate_primary_after_first_close) flash[0x100240] ^= 0x10;
 if (close_count==1 && mutate_layout_after_first_close) qca_smem_flash_info.flash_secondary_type++;
}
int rt3_fw_write_window_close_status(void) { return 0; }
int mtd_erase(struct mtd_info *m, struct erase_info *e)
{
 (void)m; if (!window_active) return -EPERM;
 if (erase_count < 16) erase_log[erase_count]=e->addr;
 erase_count++;
 if ((int)e->addr == fail_erase_at) return -EIO;
 memset(flash+e->addr,0xff,e->len); return 0;
}
int mtd_write(struct mtd_info *m,uint64_t off,size_t n,size_t *done,const uint8_t *p)
{
 (void)m; if (!window_active) return -EPERM;
 write_count++;
 if ((int)off == short_write_at) { *done=0; return 0; }
 if ((int)off == fail_write_at) { *done=0; return -EIO; }
 memcpy(flash+off,p,n); *done=n; return 0;
}
int mtd_read(struct mtd_info *m,uint64_t off,size_t n,size_t *done,uint8_t *p)
{
 (void)m; read_count++;
 if ((fail_read_after_writes >= 0 && write_count >= fail_read_after_writes) || read_count==fail_read_at) return -EIO;
 if (read_count==short_read_at) { *done=0; return 0; }
 memcpy(p,flash+off,n); *done=n;
 if (read_count==mutate_flash_at_read) flash[mutate_flash_offset]^=0x40;
 if ((mismatch_read_after_writes >= 0 && write_count >= mismatch_read_after_writes) || read_count==mismatch_read_at) p[0] ^= 1;
 return 0;
}
int httpd_register_uri_handler(struct httpd_instance *i,const char *uri,httpd_uri_handler_cb cb,struct httpd_uri_handler **ret)
{ (void)i;(void)cb;(void)ret;if(registered_count<8)registered[registered_count++]=uri;return 0; }

static unsigned h_rd32(const unsigned char *p) { return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }
static void h_wr32(unsigned char *p,unsigned v) { p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24; }
static void putrow(unsigned char *b,unsigned row,const char *name,unsigned selector)
{ unsigned char *e=b+12+row*20;memset(e,0,20);strncpy((char *)e,name,15);h_wr32(e+16,selector); }
static void bootcfg(unsigned char *b,unsigned age,unsigned selector,unsigned salt)
{
 unsigned i;for(i=0;i<BC_SIZE;i++)b[i]=(unsigned char)(i*31+salt);
 h_wr32(b,RT3_BOOTCFG_MAGIC);h_wr32(b+4,age);h_wr32(b+8,8);
 putrow(b,0,"0:APPSBL",1);putrow(b,1,"0:QSEE",0);putrow(b,2,"0:CDT",1);
 putrow(b,3,"rootfs_1",0);putrow(b,4,"ART",1);putrow(b,5,"rootfs",selector);
 putrow(b,6,"0:DEVCFG",1);putrow(b,7,"0:HLOS",0);
 h_wr32(b+332,RT3_BOOTCFG_END);
}
static void defaults_setup(int swapped,unsigned selector)
{
 struct mtd_info *m;
 fixture(0);if(swapped)screenshot_fixture();
 m=&nand_info[0];m->type=MTD_NANDFLASH;m->size=0x08000000ULL;m->erasesize=0x20000;m->writesize=2048;
 memset(flash,0x5a,sizeof(flash));bootcfg(flash+0x100000,0x0c,selector,0x11);
 bootcfg(flash+0x140000,0x0d,selector,0x72);
 erase_count=write_count=read_count=bad_count=open_count=close_count=alloc_calls=registered_count=0;
 memset(erase_log,0,sizeof(erase_log));fail_erase_at=fail_write_at=short_write_at=-1;
 fail_read_after_writes=mismatch_read_after_writes=fail_read_at=short_read_at=mismatch_read_at=badblock_at=mutate_flash_at_read=-1;
 open_fail=mutate_owner_on_open=mutate_layout_on_open=0;
 mutate_primary_after_first_close=mutate_layout_after_first_close=0;
 fail_alloc_at=-1;window_active=0;partial_update_lock=0;
 host_free(last_backup_snapshot);last_backup_snapshot=NULL;backup_snapshot_ready=0;
 bootcmd_value="bootipq";
}
static int call_post(const char *target,struct httpd_response *resp)
{
 struct httpd_request q;memset(&q,0,sizeof(q));q.method=HTTP_POST;q.form.count=1;
 q.form.values[0].name="target";q.form.values[0].data=target;q.form.values[0].size=strlen(target);
 memset(resp,0,sizeof(*resp));default_handler(HTTP_CB_NEW,&q,resp);return (int)resp->info.code;
}
static int call_get(struct httpd_response *resp)
{ struct httpd_request q;memset(&q,0,sizeof(q));q.method=HTTP_GET;memset(resp,0,sizeof(*resp));default_handler(HTTP_CB_NEW,&q,resp);return (int)resp->info.code; }
static int call_bad_form(unsigned count,int duplicate)
{
 struct httpd_response r={0};struct httpd_request q={0};q.method=HTTP_POST;q.form.count=count;
 q.form.values[0].name="target";q.form.values[0].data="rootfs_1";q.form.values[0].size=8;
 if(duplicate){q.form.count=2;q.form.values[1]=q.form.values[0];}
 default_handler(HTTP_CB_NEW,&q,&r);return r.info.code;
}
static int okjson(const struct httpd_response *r,const char *part,const char *value)
{ char needle[96];snprintf(needle,sizeof(needle),"\"%s\":\"%s\"",part,value);return r->data&&strstr(r->data,needle)!=NULL; }
static int download(unsigned which,const unsigned char *expected)
{
 struct httpd_request q={0};struct httpd_response r={0};q.method=HTTP_GET;
 default_backup_handler(HTTP_CB_NEW,&q,&r,which);
 return r.info.code==200 && r.size==BC_SIZE && !memcmp(r.data,expected,BC_SIZE);
}
static int test_success(int swapped,const char *target,unsigned expected_selector,const char *expected_name)
{
 unsigned char old0[BC_SIZE],old1[BC_SIZE],expect0[BC_SIZE],expect1[BC_SIZE];
 struct httpd_response r={0};unsigned i;
 defaults_setup(swapped,0);
 /* Real-copy tolerance: unrelated name and post-NUL padding differ locally. */
 putrow(flash+0x100000,4,"0:WIFIFW",1);
 putrow(flash+0x140000,4,"\\000\\000\\000",1);
 flash[0x140000+12+4*20+14]=0x6d;
 memcpy(old0,flash+0x100000,BC_SIZE);memcpy(old1,flash+0x140000,BC_SIZE);
 memcpy(expect0,old0,BC_SIZE);memcpy(expect1,old1,BC_SIZE);
 h_wr32(expect0+0x80,expected_selector);h_wr32(expect1+0x80,expected_selector);
 CHECK(call_post(target,&r)==200&&okjson(&r,"status","ok")&&okjson(&r,"default",expected_name));
 CHECK(erase_count==4&&write_count==256&&open_count==2&&close_count==2);
 CHECK(erase_log[0]==0x140000&&erase_log[1]==0x160000&&erase_log[2]==0x100000&&erase_log[3]==0x120000);
 CHECK(!memcmp(flash+0x100000,expect0,BC_SIZE)&&!memcmp(flash+0x140000,expect1,BC_SIZE));
 CHECK(h_rd32(flash+0x100004)==0x0c&&h_rd32(flash+0x140004)==0x0d);
 /* Every byte outside each copy's own rootfs selector remains unchanged. */
 for(i=0;i<BC_SIZE;i++)if(i<0x80||i>=0x84){CHECK(flash[0x100000+i]==old0[i]);CHECK(flash[0x140000+i]==old1[i]);}
 CHECK(download(0,old0)&&download(1,old1));
 CHECK(call_get(&r)==200&&okjson(&r,"default",expected_name));
 return 0;
}
static int test_noop_and_forms(void)
{
 struct httpd_response r={0};defaults_setup(0,0);
 CHECK(call_post("rootfs",&r)==200&&strstr(r.data,"\"changed\":false"));
 CHECK(erase_count==0&&write_count==0&&open_count==0&&bad_count==0);
 defaults_setup(0,0);CHECK(call_bad_form(0,0)==400);CHECK(erase_count==0&&read_count==0);
 defaults_setup(0,0);CHECK(call_bad_form(2,1)==400);CHECK(erase_count==0&&read_count==0);
 defaults_setup(0,0);CHECK(call_post("mtd16",&r)==400&&erase_count==0&&read_count==0);
 defaults_setup(0,0);bootcmd_value="bootipq rootfs";CHECK(call_post("rootfs_1",&r)==409&&strstr(r.data,"custom_bootcmd"));CHECK(erase_count==0);
 return 0;
}
static int test_invalid_images(void)
{
 struct httpd_response r={0};unsigned char *p;
 defaults_setup(0,0);flash[0x100000]=0;CHECK(call_post("rootfs_1",&r)==409&&erase_count==0);
 defaults_setup(0,0);h_wr32(flash+0x100000+332,0);CHECK(call_post("rootfs_1",&r)==409&&erase_count==0);
 defaults_setup(0,0);p=flash+0x140000;putrow(p,4,"rootfs",0);CHECK(call_post("rootfs_1",&r)==409&&erase_count==0);
 defaults_setup(0,0);putrow(flash+0x100000,5,"rootfs",3);putrow(flash+0x140000,5,"rootfs",3);CHECK(call_post("rootfs_1",&r)==409&&erase_count==0);
 defaults_setup(0,0);memset(flash+0x100000+12,'A',16);CHECK(call_post("rootfs_1",&r)==409&&erase_count==0);
 defaults_setup(0,0);h_wr32(flash+0x140000+8,17);CHECK(call_post("rootfs_1",&r)==409&&erase_count==0);
 defaults_setup(0,0);h_wr32(flash+0x140000+12+5*20+16,1);CHECK(call_post("rootfs_1",&r)==409&&erase_count==0);
 return 0;
}
static int test_preflight(void)
{
 struct httpd_response r={0};unsigned char original[BC_SIZE];
 defaults_setup(0,0);fail_read_at=1;CHECK(call_post("rootfs_1",&r)==409&&erase_count==0&&write_count==0);
 defaults_setup(0,0);fail_alloc_at=1;CHECK(call_post("rootfs_1",&r)==500&&erase_count==0&&write_count==0);
 defaults_setup(0,0);fail_alloc_at=2;CHECK(call_post("rootfs_1",&r)==500&&erase_count==0&&write_count==0);
 defaults_setup(0,0);fail_alloc_at=3;CHECK(call_post("rootfs_1",&r)==500&&erase_count==0&&write_count==0);
 defaults_setup(0,0);badblock_at=0x140000;CHECK(call_post("rootfs_1",&r)==409&&erase_count==0&&open_count==0);
 defaults_setup(0,0);mutate_owner_on_open=1;CHECK(call_post("rootfs_1",&r)==409&&erase_count==0&&close_count==1&&open_count==1);
 defaults_setup(0,0);mutate_flash_at_read=257;mutate_flash_offset=0x100200;CHECK(call_post("rootfs_1",&r)==409&&erase_count==0&&write_count==0);
 defaults_setup(0,0);mutate_layout_on_open=1;CHECK(call_post("rootfs_1",&r)==409&&erase_count==0&&close_count==1&&open_count==1);
 defaults_setup(0,0);memcpy(original,flash+0x100000,BC_SIZE);mutate_primary_after_first_close=1;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&erase_count==2&&close_count==2&&partial_update_lock);
 CHECK(download(0,original));
 defaults_setup(0,0);mutate_layout_after_first_close=1;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&erase_count==2&&close_count==1&&partial_update_lock);
 return 0;
}
static int test_failures(void)
{
 struct httpd_response r={0};
 defaults_setup(0,0);fail_erase_at=0x140000;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==1&&partial_update_lock);
 CHECK(call_post("rootfs_1",&r)==409&&erase_count==1);CHECK(download(0,(unsigned char *)last_backup_snapshot));
 defaults_setup(0,0);fail_erase_at=0x100000;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&erase_count==3&&close_count==2);
 defaults_setup(0,0);fail_write_at=0x140000;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==1);
 defaults_setup(0,0);short_write_at=0x140000;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==1);
 defaults_setup(0,0);fail_write_at=0x100000;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==2);
 defaults_setup(0,0);fail_read_after_writes=128;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==1);
 defaults_setup(0,0);fail_read_after_writes=129;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==2);
 defaults_setup(0,0);fail_read_at=1281;CHECK(call_post("rootfs_1",&r)==500&&strstr(r.data,"final_full_readback_failed")&&close_count==2);
 defaults_setup(0,0);mismatch_read_after_writes=128;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==1);
 defaults_setup(0,0);short_read_at=769;CHECK(call_post("rootfs_1",&r)==500&&okjson(&r,"status","partial")&&close_count==1);
 defaults_setup(0,0);mismatch_read_at=1281;CHECK(call_post("rootfs_1",&r)==500&&strstr(r.data,"final_full_readback_failed")&&close_count==2);
 defaults_setup(0,0);open_fail=1;CHECK(call_post("rootfs_1",&r)==409&&erase_count==0&&close_count==0);
 CHECK(!window_active&&open_count==close_count+1);
 defaults_setup(0,0);bootcmd_value="bootipq";CHECK(call_get(&r)==200&&strstr(r.data,"\"can_change\":true"));
 return 0;
}
static int b_main(void)
{
 struct httpd_instance inst;struct httpd_response r={0};
 CHECK(!test_success(0,"rootfs_1",1,"rootfs_1"));
 CHECK(!test_success(1,"rootfs",1,"rootfs"));
 CHECK(!test_noop_and_forms());CHECK(!test_invalid_images());CHECK(!test_preflight());CHECK(!test_failures());
 defaults_setup(0,0);boot_default_rt3000_b_register_uri_handlers(&inst);
 #ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
 CHECK(registered_count==6);
 CHECK(!strcmp(registered[3],"/boot/default/retained-failure"));
 CHECK(!strcmp(registered[4],"/boot/default/retained-original0.bin"));
 CHECK(!strcmp(registered[5],"/boot/default/retained-original1.bin"));
#else
 CHECK(registered_count==3);
#endif
 CHECK(!strcmp(registered[0],"/boot/default")&&
       !strcmp(registered[1],"/boot/default/backup/bootconfig.bin")&&
       !strcmp(registered[2],"/boot/default/backup/bootconfig1.bin"));
 CHECK(call_get(&r)==200&&okjson(&r,"default","rootfs"));
 puts("BOOTCONFIG production transaction host tests PASS");return 0;
}

#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT)
int main(void)
{
 struct httpd_response r={0};
 defaults_setup(0,0); bootcmd_value="bootipq";
 CHECK(call_get(&r)==200 && strstr(r.data,"\"auto_web\":false") &&
       strstr(r.data,"\"default_applies_to\":\"bootipq\"") &&
       strstr(r.data,"\"can_change\":true"));
 bootcmd_value="run custom";
 CHECK(call_get(&r)==200 && strstr(r.data,"\"auto_web\":false") &&
       strstr(r.data,"\"default_applies_to\":\"bootipq\"") &&
       strstr(r.data,"\"can_change\":false") && strstr(r.data,"custom_bootcmd"));
 CHECK(call_post("rootfs_1",&r)==409 && strstr(r.data,"custom_bootcmd") && erase_count==0);
 bootcmd_value="setenv mtdids nand0=nand0 && setenv mtdparts mtdparts=nand0:0x02800000@0x03100000(c-native) && ubi part c-native && ubi read 0x44000000 kernel && bootm 0x44000000#config@mp03.1-c3";
 CHECK(call_get(&r)==200 && strstr(r.data,"\"can_change\":false") && strstr(r.data,"custom_bootcmd"));
 CHECK(call_post("rootfs_1",&r)==409 && strstr(r.data,"custom_bootcmd") && erase_count==0);
 puts("C production BOOTCONFIG GET metadata host test PASS"); return 0;
}
#elif defined(CONFIG_H3C_HW_C_RAM_READONLY)
int main(void)
{
 struct httpd_response r={0}; defaults_setup(0,0);
 CHECK(call_get(&r)==200 && strstr(r.data,"\"auto_web\":true") &&
       strstr(r.data,"\"default_applies_to\":\"bootipq\"") &&
       strstr(r.data,"\"can_change\":false"));
 CHECK(call_post("rootfs_1",&r)==403 && strstr(r.data,"readonly_profile") && erase_count==0);
 puts("C readonly BOOTCONFIG auto-web host test PASS"); return 0;
}
#elif defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
int main(void)
{
 struct httpd_response r={0};
 CHECK(!b_main());
 defaults_setup(0,0);
 CHECK(call_get(&r)==200 && strstr(r.data,"\"auto_web\":true") &&
       strstr(r.data,"\"default_applies_to\":\"bootipq\"") &&
       strstr(r.data,"\"can_change\":true"));
 mock_retained_failure=1;
 CHECK(call_post("rootfs_1",&r)==409 && strstr(r.data,"retained_service_failure") && erase_count==0 && open_count==0 && read_count==256);
 { struct httpd_request request={.method=HTTP_GET};
 retained_failure_handler(HTTP_CB_NEW,&request,&r);CHECK(r.info.code==200 && strstr(r.data,"retained-original0.bin"));
 retained_original0_handler(HTTP_CB_NEW,&request,&r);CHECK(r.info.code==200 && r.size==BC_SIZE && r.data==(const char *)flash+0x100000);
 retained_original1_handler(HTTP_CB_NEW,&request,&r);CHECK(r.info.code==200 && r.size==BC_SIZE && r.data==(const char *)flash+0x140000); }
 mock_retained_failure=0;
 puts("D production cross-entry retained failure lock + HTTP export PASS");
 puts("D production BOOTCONFIG transaction and default-web tests PASS"); return 0;
}
#else
int main(void) { return b_main(); }
#endif
