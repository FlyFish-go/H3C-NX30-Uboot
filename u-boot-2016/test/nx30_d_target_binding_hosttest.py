from pathlib import Path
import subprocess,ast
src=Path(__file__).resolve().parents[1]
import argparse,tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--output-dir')
args=parser.parse_args()
here=Path(args.output_dir) if args.output_dir else Path(tempfile.mkdtemp(prefix='nx30-host-'))
here.mkdir(parents=True,exist_ok=True)

md=(src/'failsafe/modules/metadata_rt3000_b.c').read_text();ub=(src/'lib/nx30_d_uboot.c').read_text()
def fn(text,name):
 start=text.index(name+'(');start=text.rfind('\n',0,start)+1;b=text.index('{',start);e=b+1;d=1
 while d:d+=(text[e]=='{')-(text[e]=='}');e+=1
 return text[start:e]
prefix='\n#define CONFIG_H3C_HW_D_PRODUCTION_PORT 1\n#define CONFIG_H3C_NAND_ECC_COMPAT 1\n#define NX30_D_UBOOT_HOST_TEST\n#include <nx30_d_uboot_core.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdint.h>\n#include <assert.h>\n#include <errno.h>\n#define RT3_MD_APPSBL_SIZE 0x140000\n#define CONFIG_QPIC_NAND_NAND_INFO_IDX 0\n#define CONFIG_NAND_FLASH_INFO_IDX 0\nenum rt3_md_kind { RT3_MD_UBOOT,RT3_MD_ART,RT3_MD_CDT };\nenum rt3_md_target { RT3_MD_TARGET_DEFAULT,RT3_MD_TARGET_PRIMARY,RT3_MD_TARGET_SECONDARY };\nenum rt3_layout_mode { RT3_LAYOUT_UNSUPPORTED,RT3_LAYOUT_DUAL };\nstruct rt3_layout { enum rt3_layout_mode mode; u32 fingerprint; };\nstruct md_extent{const char*name;u32 off,size;};\nstruct md_pending{u8*data;u32 size,alloc_size;enum rt3_md_kind kind;enum rt3_md_target target;u32 target_off,target_size,second_off,second_size,layout_fingerprint;u8 hash[32],source_hash[32];char token[65];int valid;};\nstatic struct md_pending pending;\nstatic const struct md_extent appsbl={"0:APPSBL",0x500000,0x140000},art={"0:ART",0x780000,0x100000},cdt={"0:CDT",0x400000,0x40000},cdt1={"0:CDT_1",0x440000,0x40000};\nstruct httpd_response { int unused; };\nstruct mtd_info { int unused; } nand_info[1];\nstruct nx30_d_extent { u64 offset,size; };\nstruct nx30_d_layout { u32 generation,nand_size,erase_size,write_size,ecc_bits; struct nx30_d_extent appsbl_high; };\nstruct nx30_d_firmware_info { u32 generation,image_crc32; };\nstruct nx30_d_result { u32 status,stage,generation,crc_before,crc_after,bytes_verified; u64 target_offset,target_size; };\nstruct nx30_d_uboot_mtd_context { struct mtd_info *mtd; };\nstatic u8 bc[256U<<10]; static u64 actual_target; static unsigned backend_calls;\nstatic int nx30_d_uboot_active_target(u64*,u64*);\nstatic int layout_read(struct rt3_layout *l) { l->mode=RT3_LAYOUT_DUAL;l->fingerprint=7;return 0; }\nstatic int getpart_offset_size(char*n,u32*o,u32*s) { *s=0x140000;if(!strcmp(n,"0:APPSBL")){*o=0x500000;return 0;}if(!strcmp(n,"0:APPSBL_1")){*o=0x640000;return 0;}return -1; }\nstatic int nx30_d_layout_get(struct nx30_d_layout*l) { *l=(struct nx30_d_layout){7,0x8000000,0x20000,0x800,4,{0x640000,0x140000}};return 0; }\nstatic int nx30_d_uboot_validate(const void*p,size_t n,struct nx30_d_firmware_info*i) { (void)p;(void)n;u64 o,z;if(nx30_d_uboot_active_target(&o,&z))return -EPERM;i->generation=7;i->image_crc32=42;return 0; }\nstatic int nx30_d_board_gate(void) { return 0; }\nstatic int nx30_d_sbl1_gate(struct mtd_info*m) { (void)m;return 0; }\nstatic int h3c_nand_ecc_write_allowed(struct mtd_info*m,u64 o,u64 n) { (void)m;(void)o;(void)n;return 1; }\nstatic void nx30_d_sha256(const u8*p,size_t n,u8 d[32],void*c) { (void)p;(void)n;(void)d;(void)c; }\nstatic int nx30_d_mtd_open(void*c,u64 o,u64 n,unsigned e) { return 0; }\nstatic void nx30_d_mtd_close(void*c) {}\nstatic int nx30_d_mtd_close_error(void*c) { return 0; }\nstatic int nx30_d_mtd_erase(void*c,u64 o,u64 n) { return 0; }\nstatic int nx30_d_mtd_write(void*c,u64 o,const void*p,size_t n,size_t*d) { return 0; }\nstatic int nx30_d_mtd_read(void*c,u64 o,void*p,size_t n,size_t*d) { return 0; }\nint nx30_d_uboot_program_core(const void*p,size_t n,u32 gen,u32 crc,const struct nx30_d_uboot_geometry*g,nx30_d_uboot_digest_fn d,void*dc,const struct nx30_d_uboot_io*io,void*s,size_t ns,void*v,size_t nv,u32*verified,u32*stage) { backend_calls++;actual_target=g->appsbl_offset;*verified=g->appsbl_size;return 0; }\nstatic void hash(const void*p,u32 n,u8 d[32]) { (void)p;(void)n;memset(d,0,32); }\nstatic int rt3_md_hash_matches(const void*a,const void*b,u32 n) { return !memcmp(a,b,n); }\nstatic int rt3_md_validate_image(enum rt3_md_kind k,const void*p,u32 n) { return 0; }\nstatic void rt3_metadata_invalidate(void) { free(pending.data);memset(&pending,0,sizeof(pending)); }\nstatic void fail(struct httpd_response*r,const char*t,const char*n) { (void)r;fprintf(stderr,"error: %s %s\\n",t,n); }\nstatic int write_extent(const struct md_extent*e,const u8*p,u32 n,u32 g) { abort(); }\nstatic int data_ok(const void*p,u32 n) { return p && n; }\nstatic void hex(const u8*d,char*out) { memset(out,\'0\',64);out[64]=0; }\nstatic u32 minted_target;\nstatic void make_token(enum rt3_md_kind k,enum rt3_md_target t,u32 n,const u8*s,const u8*d,u32 g,const struct md_extent*f,const struct md_extent*b,char out[65]) { minted_target=f->off;memset(out,\'a\',64);out[64]=0; }\nstatic int rt3_md_snapshot_copy(void*d,const void*s,u32 n) { memcpy(d,s,n);return 0; }\n'
prefix=prefix.replace('struct nx30_d_extent appsbl_high;', 'struct nx30_d_extent appsbl_high,appsbl_active;')
prefix=prefix.replace('static u8 bc[256U<<10];', 'static u32 smem_offset=0x640000;\nint nx30_d_uboot_target_check(u64,u64);\nstatic u8 bc[256U<<10];')
prefix=prefix.replace('*o=0x500000;return 0;', '*o=smem_offset;return 0;').replace('*o=0x640000;return 0;', '*o=smem_offset==0x640000?0x500000:0x640000;return 0;')
prefix=prefix.replace('{7,0x8000000,0x20000,0x800,4,{0x640000,0x140000}}', '{7,0x8000000,0x20000,0x800,4,{0x640000,0x140000},{smem_offset,0x140000}}')
prefix=prefix.replace('static int nx30_d_mtd_open(void*c,u64 o,u64 n,unsigned e) { return 0; }','static int nx30_d_mtd_open(void*,u64,u64,unsigned);\nstatic unsigned window_opens;\nstatic int nx30_d_qpic_write_window_open(struct mtd_info *m,u64 o,u64 n,unsigned e) { (void)m;(void)o;(void)n;(void)e;window_opens++;return 0; }')
main=r'''
static int nx30_d_uboot_active_target(u64*o,u64*n) { u64 resolved; if(nx30_d_uboot_active_copy_resolve(bc,bc,sizeof(bc),smem_offset,0x140000,&resolved))return -EPERM;*o=resolved;*n=0x140000;return 0; }
static void put32(u8*p,u32 v) { memcpy(p,&v,4); }
int main(void) {
 struct httpd_response response;char token[65],digest[65];
 put32(bc,0xa3a2a1a0);put32(bc+8,8);put32(bc+332,0xb3b2b1b0);memcpy(bc+0x48,"0:APPSBL",9);
 for(unsigned map=0;map<2;map++) for(unsigned selector=0;selector<2;selector++) {
  smem_offset=map?0x640000:0x500000;put32(bc+0x58,selector);
  u64 target=selector?0x640000:0x500000;int ok=target==smem_offset;unsigned before=backend_calls;
  int rc=rt3_metadata_validate_target(RT3_MD_UBOOT,RT3_MD_TARGET_DEFAULT,bc,16384,token,digest);
  assert(ok?rc==0:rc!=0);
  if(ok) { assert(minted_target==target && pending.target_off==minted_target);assert(!metadata_write_internal(RT3_MD_UBOOT,RT3_MD_TARGET_DEFAULT,token,&response));assert(actual_target==minted_target); }
  assert(backend_calls==before+(ok?1:0));
  struct nx30_d_uboot_mtd_context context={&nand_info[0]};unsigned opened=window_opens;
  int gate=nx30_d_mtd_open(&context,target,0x140000,4);
  assert(ok?gate==0:gate!=0);assert(window_opens==opened+(ok?1:0));
  printf("SMEM=0x%x selector=%u preview=%d backend_calls_delta=%u PASS\n",smem_offset,selector,rc,backend_calls-before);
 }
 for(unsigned change=0;change<2;change++) {
  smem_offset=0x640000;put32(bc+0x58,1);assert(!rt3_metadata_validate_target(RT3_MD_UBOOT,RT3_MD_TARGET_DEFAULT,bc,16384,token,digest));
  if(change)put32(bc+0x58,0);else smem_offset=0x500000;
  unsigned before=backend_calls;assert(metadata_write_internal(RT3_MD_UBOOT,RT3_MD_TARGET_DEFAULT,token,&response));assert(before==backend_calls);
  assert(metadata_write_internal(RT3_MD_UBOOT,RT3_MD_TARGET_DEFAULT,token,&response));
  printf("post-preview %s changed: commit denied, zero backend calls, token consumed PASS\n",change?"selector":"SMEM mapping");
 }
 return 0;
}
'''
functions='\n'.join(fn(ub,n) for n in ['get_le32','nx30_d_uboot_active_copy_resolve','nx30_d_uboot_target_check','nx30_d_mtd_open','nx30_d_uboot_write'])
functions+='\n'+'\n'.join(fn(md,n) for n in ['primary','known_extent','layout_extent','layout_targets','resolve_target','rt3_metadata_validate_target','metadata_write_internal'])
out=here/'target-regression.c';out.write_text(prefix+functions+main)
subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-idirafter',str(src/'include'),str(out),'-o',str(here/'target-regression')],check=True)
subprocess.run([str(here/'target-regression')],check=True)
