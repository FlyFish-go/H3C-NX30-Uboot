from pathlib import Path
import subprocess
src=Path(__file__).resolve().parents[1]
import argparse,tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--output-dir')
args=parser.parse_args()
here=Path(args.output_dir) if args.output_dir else Path(tempfile.mkdtemp(prefix='nx30-host-'))
here.mkdir(parents=True,exist_ok=True)

s=(src/'lib/nx30_d_service.c').read_text()
def fn(name):
 start=s.index(name+'(');start=s.rfind('\n',0,start)+1
 b=s.index('{',start);end=b+1;depth=1
 while depth: depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
prefix=r'''
#define _POSIX_C_SOURCE 200809L
#define NX30_D_BC_HOST_TEST
#include <nx30_d_bc_core.h>
#include "service_hwd_host_api.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
typedef unsigned long ulong;
#include <bootconfig.h>
#define NX30_D_BC_BYTES (256U << 10)
#define CONFIG_QPIC_NAND_NAND_INFO_IDX 0
#define ARCH_DMA_MINALIGN 16
#define NX30_D_STAGE_PREFLIGHT 1
#define NX30_D_STAGE_REVALIDATE 2
#define NX30_D_STAGE_COMPLETE 7
struct mtd_info { int unused; } nand_info[1];
static struct { int valid,recovery; struct nx30_d_result failure;u32 failed_copy;u32 layout_generation; char token[17];enum nx30_d_slot target;u32 old_selector;u8 *original[2],*candidate; } nx30_d_default_pending;
static u32 nx30_d_default_nonce;
struct nx30_d_bc_commit_ctx { struct mtd_info *mtd;struct nx30_d_extent extent[2];struct nx30_d_result *result; };
static u8 flash[2][NX30_D_BC_BYTES];
static int fail_copy, layout_reads, calls, stale, slots_calls; static unsigned test_ecc_bits=4;
static void *memalign(size_t a,size_t n) { (void)a;return malloc(n); }
static u32 crc32(u32 a,const void *b,size_t n) { (void)b;(void)n;return a; }
static u32 get_timer(int n) { return n+1; }
int nx30_d_layout_get(struct nx30_d_layout *l) { layout_reads++;memset(l,0,sizeof(*l));l->generation=stale?8:7;l->ecc_bits=test_ecc_bits;l->bc0=(struct nx30_d_extent){0,NX30_D_BC_BYTES};l->bc1=(struct nx30_d_extent){1,NX30_D_BC_BYTES};return 0; }
int nx30_d_slots_get(struct nx30_d_slots *s) { slots_calls++;memset(s,0,sizeof(*s));s->generation=7;s->ecc_bits=test_ecc_bits;return memcmp(flash[0],flash[1],NX30_D_BC_BYTES)?-EUCLEAN:0; }
static int nx30_d_bc_read(struct mtd_info *m,struct nx30_d_extent e,u8 *p) { (void)m;memcpy(p,flash[e.offset],e.size);return 0; }
static int nx30_d_bc_program_callback(void *cookie,unsigned int copy,const void *bytes) {
 struct nx30_d_bc_commit_ctx *ctx=cookie;calls++;nx30_d_default_pending.failed_copy=copy;
 ctx->result->stage=5;
 if ((int)copy==fail_copy) { memset(flash[copy],0xff,NX30_D_BC_BYTES);memcpy(flash[copy],bytes,2048);return -EIO; }
 memcpy(flash[copy],bytes,NX30_D_BC_BYTES);ctx->result->bytes_verified+=NX30_D_BC_BYTES;return 0;
}
'''
# Fetch the definition rather than the earlier forward declaration.
read_start=s.index('static int nx30_d_default_original_read(unsigned int copy,u64 offset,\n')
def read_def():
 b=s.index('{',read_start);e=s.index('\n}',b)+2;return s[read_start:e]
http=(src/'failsafe/modules/boot_default_rt3000_b.c').read_text()
def hfn(name):
 global s
 save=s;s=http
 if name=="answer":s=s[s.rindex("static void answer("):]
 try:return fn(name)
 finally:s=save
http_prefix=r'''
enum httpd_uri_handler_status { HTTP_CB_NEW,HTTP_CB_CLOSED };
enum { HTTP_GET,HTTP_POST,HTTP_RESP_STD };
struct httpd_request { int method; };
struct httpd_response { int status;struct { int code,connection_close;const char *content_type; } info;const char *data;size_t size; };
static char reply[512];
'''
http_functions=http_prefix+'\n'+hfn('answer')+'\n'+'\n'.join(hfn(n) for n in ['retained_failure_handler','retained_original_handler','retained_original0_handler','retained_original1_handler'])
main=r'''
int main(void) {
 for(int scenario=0;scenario<5;scenario++) {
  struct bootconfig_info *bc=(void*)flash[0];struct nx30_d_result r,failed;
  char token[17],again[17];u32 copy;u8 window[65536];size_t got;
  memset(flash,0x37,sizeof(flash));bc->magic_start=BOOTCONFIG_MAGIC_START;bc->magic_end=BOOTCONFIG_MAGIC_END;bc->numaltpart=1;memset(bc->per_part_entry,0,sizeof(bc->per_part_entry));strcpy(bc->per_part_entry[0].name,"rootfs");bc->per_part_entry[0].primaryboot=0;memcpy(flash[1],flash[0],NX30_D_BC_BYTES);
  calls=0;stale=0;test_ecc_bits=scenario==4?8:4;fail_copy=scenario<2?scenario:-1;
  if(scenario==4) { int before=slots_calls; assert(nx30_d_default_prepare(NX30_D_SLOT_B,token,&r)==-EOPNOTSUPP); assert(!nx30_d_default_pending.valid && slots_calls==before && !calls); printf("ECC8 prepare refused before slot/mode selection and before writes PASS\n"); nx30_d_default_pending_clear(); continue; }
  assert(!nx30_d_default_prepare(NX30_D_SLOT_B,token,&r));
  if(scenario==3)stale=1;
  int ret=nx30_d_default_commit(token,scenario==1?NULL:&r);
  assert(nx30_d_default_commit(token,&r)==-EINVAL);
  if(scenario<2) {
   assert(ret==-EIO && nx30_d_default_pending.recovery && !nx30_d_default_pending.valid);
   assert(!nx30_d_default_failure_get(&failed,&copy));assert(copy==(u32)scenario && failed.stage==5 && failed.status==-EIO);
   int reads=layout_reads;assert(nx30_d_default_prepare(NX30_D_SLOT_A,again,&r)==-EBUSY);assert(reads==layout_reads);
   assert(nx30_d_default_abort(token)==-EINVAL);
   for(unsigned c=0;c<2;c++) for(u64 off=0;off<NX30_D_BC_BYTES;off+=sizeof(window)) {
    assert(!nx30_d_default_original_read(c,off,window,sizeof(window),&got));assert(got==sizeof(window));
    assert(!memcmp(window,nx30_d_default_pending.original[c]+off,got));
   }
   assert(nx30_d_default_original_read(0,NX30_D_BC_BYTES,window,1,&got)==-ERANGE);
   struct httpd_request request={HTTP_GET};struct httpd_response response={0};
   retained_failure_handler(HTTP_CB_NEW,&request,&response);
   assert(response.info.code==200 && strstr(response.data,"retained-original0.bin") && strstr(response.data,"recovery_required"));
   retained_original0_handler(HTTP_CB_NEW,&request,&response);assert(response.info.code==200 && response.size==NX30_D_BC_BYTES);assert(!memcmp(response.data,nx30_d_default_pending.original[0],response.size));
   const char *first=response.data;
   retained_original1_handler(HTTP_CB_NEW,&request,&response);assert(response.info.code==200 && response.size==NX30_D_BC_BYTES);assert(!memcmp(response.data,nx30_d_default_pending.original[1],response.size));assert(first!=response.data);
   request.method=HTTP_POST;retained_original0_handler(HTTP_CB_NEW,&request,&response);assert(response.info.code==405);
   printf("production public HTTP diagnosis + both 256KiB preimage downloads PASS\n");
   printf("partial-copy%u: original exports intact, stage=%u status=%d, replay/abort/new-prepare blocked PASS\n",copy,failed.stage,failed.status);
  } else { assert(ret==(scenario==3?-ESTALE:0));assert(!nx30_d_default_pending.recovery);assert(!nx30_d_default_pending.original[0]);assert(calls==(scenario==3?0:2));printf("%s cleanup PASS\n",scenario==3?"prewrite stale":"success"); }
  nx30_d_default_pending_clear();
 }
 return 0;
}
'''
code=prefix+'\n'+ '\n'.join(fn(n) for n in ['nx30_d_parse_bc','nx30_d_default_pending_clear','nx30_d_default_failure_get','nx30_d_default_original_get'])+'\n'+read_def()+'\n'+'\n'.join(fn(n) for n in ['nx30_d_default_abort','nx30_d_bc_update_selector','nx30_d_default_prepare','nx30_d_default_commit'])+http_functions+main
out=here/'bc-regression.c';out.write_text(code)
subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-idirafter',str(src/'include'),'-I'+str(src/'test'),str(out),'-o',str(here/'bc-regression')],check=True)
subprocess.run([str(here/'bc-regression')],check=True)
