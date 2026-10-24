#define RT3_METADATA_HOST_TEST 1
#include <common.h>
#include <sys/mman.h>
#include <linux/mtd/mtd.h>
#include <rt3000_b_metadata.h>
#include <rt3000_b_layout.h>
#include <rt3000_b_fw_write.h>
ulong httpd_upload_buffer_base(void) { return 0x41000000UL; }
static int build_id_result,bootconfig_appsbl=-1,reboot_lock;
int h3c_build_id_check(const uint8_t*p,size_t n){(void)p;(void)n;return build_id_result;}
int get_bootconfig(const char*n){return strcmp(n,"0:APPSBL")?-1:bootconfig_appsbl;}
int rt3_layout_resolve_raw(const void*t,size_t n,struct rt3_layout*l){(void)t;(void)n;l->mode=RT3_LAYOUT_DUAL;l->fingerprint=0;return 0;}
void rt3_flash_require_reboot(void){reboot_lock=1;}
int rt3_flash_reboot_required(void){return reboot_lock;}
#define FLASH_SIZE 0x08000000U
struct mtd_info nand_info[1];
static unsigned char *flash;
static int open_fail,badblock,short_write,read_mismatch,fail_offset=-1,open_count,close_count,erase_count,write_count,read_count,badblock_count;
static unsigned int erase_log[64];
static int read_fail_offset=-1,read_mismatch_offset=-1,race_layout_on_open,race_offset_on_open=-1,write_fail_offset=-1,read_mismatch_after_erases=-1;
static char registered_uri[4][64];static httpd_uri_handler_cb registered_cb[4];static unsigned int registered_count;
static int badblock_offset=0x780000;
static int layout_error,layout_wrong_extent;static unsigned int layout_fingerprint=0x10293847;
static unsigned int appsbl_offset=0x500000,appsbl1_offset=0x640000,cdt_offset=0x400000,cdt1_offset=0x440000;
int httpd_register_uri_handler(struct httpd_instance*i,const char*u,httpd_uri_handler_cb cb,void*r){(void)i;(void)r;if(registered_count<4){strncpy(registered_uri[registered_count],u,63);registered_cb[registered_count]=cb;registered_count++;}return 0;}
int rt3_layout_read(struct rt3_layout*l){l->mode=layout_error?RT3_LAYOUT_UNSUPPORTED:RT3_LAYOUT_DUAL;l->fingerprint=layout_fingerprint;return layout_error;}
int getpart_offset_size(char*n,unsigned int*o,unsigned int*s){
 if(!strcmp(n,"0:CDT")){*o=cdt_offset+(layout_wrong_extent?0x20000:0);*s=0x40000;return 0;}
 if(!strcmp(n,"0:CDT_1")){*o=cdt1_offset;*s=0x40000;return 0;}
 if(!strcmp(n,"0:ART")){*o=0x780000;*s=0x100000;return 0;}
 if(!strcmp(n,"0:APPSBL")){*o=appsbl_offset;*s=0x140000;return 0;}
 if(!strcmp(n,"0:APPSBL_1")){*o=appsbl1_offset;*s=0x140000;return 0;}
 return -1;
}
int rt3_metadata_write_window_open(struct mtd_info*m,const char*n,uint64_t o,uint64_t s){unsigned int x,z;open_count++;if(open_fail)return -EACCES;if(m!=nand_info||getpart_offset_size((char*)n,&x,&z)||o!=x||s!=z)return -EINVAL;if(race_layout_on_open){layout_fingerprint++;race_layout_on_open=0;}if(race_offset_on_open>=0){flash[race_offset_on_open]^=1;race_offset_on_open=-1;}return 0;}
void rt3_fw_write_window_close(void){close_count++;}
int nand_block_isbad(struct mtd_info*m,uint64_t o){(void)m;badblock_count++;return badblock&&((int)o==badblock_offset)?1:0;}
int mtd_erase(struct mtd_info*m,struct erase_info*e){(void)m;if(erase_count<64)erase_log[erase_count]=e->addr;erase_count++;if((int)e->addr==fail_offset)return -EIO;memset(flash+e->addr,0xff,e->len);return 0;}
int mtd_write(struct mtd_info*m,uint64_t o,size_t n,size_t*done,const uint8_t*p){(void)m;write_count++;if(short_write){*done=0;short_write=0;return 0;}if((int)o==write_fail_offset){*done=0;return -EIO;}memcpy(flash+o,p,n);*done=n;return 0;}
int mtd_read(struct mtd_info*m,uint64_t o,size_t n,size_t*done,uint8_t*p){(void)m;read_count++;memcpy(p,flash+o,n);*done=n;if((int)o==read_fail_offset)return -EIO;if(read_mismatch&&((read_mismatch_offset<0&&(int)o==0x780000)||(int)o==read_mismatch_offset)&&(read_mismatch_after_erases<0||erase_count>=read_mismatch_after_erases))p[0]^=1;return 0;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d %s\n",__LINE__,#x);return 1;}}while(0)
static void reset_layout(void){layout_error=layout_wrong_extent=0;appsbl_offset=0x500000;appsbl1_offset=0x640000;cdt_offset=0x400000;cdt1_offset=0x440000;layout_fingerprint=0x10293847;}
static int io_zero(void){return erase_count==0&&write_count==0&&read_count==0&&open_count==0&&badblock_count==0;}
static void reset_faults(void){open_fail=badblock=short_write=read_mismatch=layout_error=layout_wrong_extent=0;fail_offset=read_fail_offset=read_mismatch_offset=race_offset_on_open=write_fail_offset=read_mismatch_after_erases=-1;race_layout_on_open=0;badblock_offset=0x780000;open_count=close_count=erase_count=write_count=read_count=badblock_count=0;memset(erase_log,0,sizeof(erase_log));}
static void make_cdt(unsigned char*d){memset(d,0xff,0x40000);memcpy(d,"CDT",3);d[3]=0;d[4]=2;d[5]=0;d[14]=26;d[15]=0;d[16]=5;d[17]=0;d[18]=31;d[19]=0;d[20]=0;d[21]=2;d[22]=0x1f;d[23]=2;d[24]=4;d[25]=0;memcpy(d+26,"\002\010\004\000\000",5);}
static int extent_matches(unsigned int off,unsigned int size,const unsigned char*src,size_t n){size_t i;if(n>size||memcmp(flash+off,src,n))return 0;for(i=n;i<size;i++)if(flash[off+i]!=0xff)return 0;return 1;}
int main(int argc,char**argv){
 FILE*f;long n;unsigned char*upload=(unsigned char*)0x41001000;char tok[65],dig[65];struct httpd_response r={0};int ret;uint32_t off,size,off2,size2;
 flash=malloc(FLASH_SIZE);CHECK(flash);memset(flash,0x5a,FLASH_SIZE);nand_info[0]=(struct mtd_info){MTD_NANDFLASH,FLASH_SIZE,0x800,0x20000,0,8};
 CHECK(argc==2||argc==3);if(mmap((void*)0x41000000,0x200000,3,0x22|0x100000,-1,0)==(void*)-1){perror("mmap");return 2;}
 memset(upload,0x55,0x100000);reset_layout();reset_faults();
 CHECK(!strcmp(rt3_metadata_validation_error(-EXDEV),"partition_layout_mismatch"));
 CHECK(!strcmp(rt3_metadata_validation_error(-ENOMEM),"metadata_staging_unavailable"));
 CHECK(!strcmp(rt3_metadata_validation_error(-EINVAL),"invalid_metadata_image"));
 layout_error=-ENODEV;ret=rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig);CHECK(ret==-EXDEV);
 layout_error=0;layout_wrong_extent=1;ret=rt3_metadata_validate(RT3_MD_CDT,upload,0x40000,tok,dig);CHECK(ret==-EXDEV);
 layout_wrong_extent=0;
 ret=rt3_metadata_validate(RT3_MD_UBOOT,upload,0x100000,tok,dig);CHECK(ret==-EINVAL);
 CHECK(!strcmp(rt3_metadata_validation_error(ret),"invalid_metadata_image"));
 CHECK(rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig)==0);
 CHECK(rt3_metadata_pending_target(RT3_MD_ART,&off,&size,&off2,&size2)==0&&off==0x780000&&size==0x100000&&!off2&&!size2);
 memset(upload,0xaa,0x100000);
 CHECK(rt3_metadata_write(RT3_MD_ART,tok,&r)==0);CHECK(flash[0x780000]==0x55&&flash[0x87ffff]==0x55);CHECK(close_count==1);
 CHECK(rt3_metadata_write(RT3_MD_ART,tok,&r)!=0);
 reset_faults();CHECK(rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig)==0);ret=rt3_metadata_write(RT3_MD_CDT,tok,&r);CHECK(ret!=0&&erase_count==0);CHECK(rt3_metadata_write(RT3_MD_ART,tok,&r)!=0);
 reset_faults();CHECK(rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig)==0);ret=rt3_metadata_write(RT3_MD_ART,NULL,&r);CHECK(ret!=0&&erase_count==0);CHECK(rt3_metadata_write(RT3_MD_ART,tok,&r)!=0&&erase_count==0);
 reset_faults();CHECK(rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig)==0);badblock=1;ret=rt3_metadata_write(RT3_MD_ART,tok,&r);CHECK(ret!=0&&erase_count==0&&open_count==0);
 reset_faults();CHECK(rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig)==0);open_fail=1;ret=rt3_metadata_write(RT3_MD_ART,tok,&r);CHECK(ret!=0&&erase_count==0&&close_count==0);
 reset_faults();CHECK(rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig)==0);short_write=1;ret=rt3_metadata_write(RT3_MD_ART,tok,&r);CHECK(ret!=0&&close_count==1);
 reset_faults();CHECK(rt3_metadata_validate(RT3_MD_ART,upload,0x100000,tok,dig)==0);read_mismatch=1;ret=rt3_metadata_write(RT3_MD_ART,tok,&r);CHECK(ret!=0&&close_count==1);
 reset_layout();reset_faults();make_cdt(upload);CHECK(rt3_metadata_validate(RT3_MD_CDT,upload,0x40000,tok,dig)==0);fail_offset=0x440000;ret=rt3_metadata_write(RT3_MD_CDT,tok,&r);CHECK(ret!=0&&erase_count==1&&erase_log[0]==0x440000&&close_count==1);CHECK(flash[0x400000]==0x5a);
 reset_layout();memset(flash+0x400000,0x5a,0x80000);reset_faults();CHECK(rt3_metadata_validate(RT3_MD_CDT,upload,0x40000,tok,dig)==0);CHECK(rt3_metadata_write(RT3_MD_CDT,tok,&r)==0);CHECK(erase_log[0]==0x440000&&erase_log[2]==0x400000);CHECK(!memcmp(flash+0x440000,upload,0x40000)&&!memcmp(flash+0x400000,upload,0x40000));
 reset_layout();cdt_offset=0x440000;cdt1_offset=0x400000;reset_faults();CHECK(rt3_metadata_validate(RT3_MD_CDT,upload,0x40000,tok,dig)==0);layout_fingerprint++;ret=rt3_metadata_write(RT3_MD_CDT,tok,&r);CHECK(ret==-EXDEV&&strstr(r.data,"partition_layout_mismatch"));CHECK(io_zero());
 reset_layout();cdt_offset=0x440000;cdt1_offset=0x400000;memset(flash+0x400000,0x5a,0x80000);reset_faults();CHECK(rt3_metadata_validate(RT3_MD_CDT,upload,0x40000,tok,dig)==0);CHECK(rt3_metadata_pending_target(RT3_MD_CDT,&off,&size,&off2,&size2)==0&&off==0x440000&&off2==0x400000&&size==0x40000&&size2==0x40000);CHECK(rt3_metadata_write(RT3_MD_CDT,tok,&r)==0);CHECK(erase_count==4&&erase_log[0]==0x400000&&erase_log[2]==0x440000);CHECK(!memcmp(flash+0x440000,upload,0x40000)&&!memcmp(flash+0x400000,upload,0x40000));
 reset_layout();cdt_offset=0x440000;cdt1_offset=0x400000;memset(flash+0x400000,0x5a,0x80000);reset_faults();CHECK(rt3_metadata_validate(RT3_MD_CDT,upload,0x40000,tok,dig)==0);fail_offset=0x440000;ret=rt3_metadata_write(RT3_MD_CDT,tok,&r);CHECK(ret!=0&&erase_count==3&&erase_log[0]==0x400000&&erase_log[2]==0x440000);CHECK(flash[0x440000]==0x5a);
 reset_layout();f=fopen(argv[1],"rb");CHECK(f);fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);CHECK(n>0&&n<0x140000);CHECK(fread(upload,1,n,f)==(size_t)n);fclose(f);
 reset_layout();rt3_metadata_host_test_new_boot();memset(flash+0x500000,0x5a,0x140000);memset(flash+0x640000,0x6b,0x140000);reset_faults();
 {
  unsigned int normalized_size=0;unsigned char*expected=malloc(RT3_MD_APPSBL_SIZE),*original0=malloc(RT3_MD_APPSBL_SIZE),*original1=malloc(RT3_MD_APPSBL_SIZE);
  CHECK(expected&&original0&&original1);CHECK(!rt3_md_prepare_elf(upload,(uint32_t)n,expected,RT3_MD_APPSBL_SIZE,&normalized_size));
  memcpy(original0,flash+0x500000,RT3_MD_APPSBL_SIZE);memcpy(original1,flash+0x640000,RT3_MD_APPSBL_SIZE);
  /* A candidate built for another board, NAND profile or ECC is refused before staging. */
  build_id_result=-EXDEV;reset_faults();ret=rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig);
  CHECK(ret==-ENOEXEC&&!strcmp(rt3_metadata_validation_error(ret),"uboot_build_mismatch")&&io_zero());build_id_result=0;
  /* After a partition table change nothing more is staged until reboot. */
  reboot_lock=1;ret=rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig);
  CHECK(ret==-EAGAIN&&!strcmp(rt3_metadata_validation_error(ret),"reboot_required"));reboot_lock=0;
  CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);
  CHECK(rt3_metadata_pending_target(RT3_MD_UBOOT,&off,&size,&off2,&size2)==0&&off==0x500000&&off2==0x640000&&size==size2&&size==0x140000);
  memset(upload,0xaa,(size_t)n);CHECK(rt3_metadata_write(RT3_MD_UBOOT,tok,&r)==0);
  CHECK(extent_matches(0x500000,0x140000,expected,normalized_size)&&extent_matches(0x640000,0x140000,expected,normalized_size));
  CHECK(erase_count==20&&erase_log[0]==0x640000&&erase_log[10]==0x500000);

  registered_count=0;rt3_metadata_register_uri_handlers(NULL);CHECK(registered_count==2);
  CHECK(!strcmp(registered_uri[0],"/uboot/backup/appsbl.bin")&&!strcmp(registered_uri[1],"/uboot/backup/appsbl_1.bin"));
  {struct httpd_request q={HTTP_GET};struct httpd_response br={0};registered_cb[0](HTTP_CB_NEW,&q,&br);CHECK(br.size==RT3_MD_APPSBL_SIZE&&!memcmp(br.data,original0,br.size));registered_cb[1](HTTP_CB_NEW,&q,&br);CHECK(br.size==RT3_MD_APPSBL_SIZE&&!memcmp(br.data,original1,br.size));}
  rt3_metadata_host_test_new_boot();free(expected);free(original0);free(original1);
 }
 f=fopen(argv[1],"rb");CHECK(f);CHECK(fread(upload,1,n,f)==(size_t)n);fclose(f);
 reset_layout();appsbl_offset=0x640000;appsbl1_offset=0x500000;layout_fingerprint++;rt3_metadata_host_test_new_boot();nand_info[0].ecc_strength=4;memset(flash+0x500000,0x5a,0x140000);memset(flash+0x640000,0x6b,0x140000);reset_faults();
 CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);CHECK(rt3_metadata_pending_target(RT3_MD_UBOOT,&off,&size,&off2,&size2)==0&&off==0x640000&&off2==0x500000);
 CHECK(rt3_metadata_write(RT3_MD_UBOOT,tok,&r)==0);CHECK(erase_log[0]==0x500000&&erase_log[10]==0x640000);CHECK(extent_matches(0x500000,0x140000,upload,(size_t)n)&&extent_matches(0x640000,0x140000,upload,(size_t)n));nand_info[0].ecc_strength=8;
 /* BOOTCONFIG selects APPSBL_1: SBL loads it, so 0:APPSBL is written first. */
 rt3_metadata_host_test_new_boot();reset_layout();memset(flash+0x500000,0x5a,0x280000);reset_faults();bootconfig_appsbl=1;
 CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);CHECK(rt3_metadata_write(RT3_MD_UBOOT,tok,&r)==0);
 CHECK(erase_count==20&&erase_log[0]==0x500000&&erase_log[10]==0x640000);CHECK(extent_matches(0x500000,0x140000,upload,(size_t)n)&&extent_matches(0x640000,0x140000,upload,(size_t)n));bootconfig_appsbl=-1;
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);appsbl1_offset=0x500000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret==-EXDEV&&erase_count==0);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);badblock=1;badblock_offset=0x640000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&erase_count==0&&open_count==0);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);read_fail_offset=0x640000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&erase_count==0);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);rt3_metadata_host_test_fail_alloc_after(5);ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&erase_count==0&&strstr(r.data,"uboot_update_preflight_failed"));rt3_metadata_host_test_fail_alloc_after(-1);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();nand_info[0].ecc_strength=0;CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&erase_count==0);nand_info[0].ecc_strength=16;CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&erase_count==0);nand_info[0].ecc_strength=8;
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);fail_offset=0x640000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_partial")&&strstr(r.data,"0:APPSBL_1")&&erase_count==1&&flash[0x500000]==0x5a);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);reset_faults();ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_locked_after_partial")&&erase_count==0);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);fail_offset=0x500000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_partial")&&strstr(r.data,"0:APPSBL")&&erase_count==11);CHECK(extent_matches(0x640000,0x140000,upload,(size_t)n));CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);reset_faults();CHECK(rt3_metadata_write(RT3_MD_UBOOT,tok,&r)!=0&&strstr(r.data,"uboot_update_locked_after_partial"));
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);write_fail_offset=0x640000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_partial")&&strstr(r.data,"0:APPSBL_1")&&erase_count==10);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);write_fail_offset=0x500000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_partial")&&strstr(r.data,"0:APPSBL")&&erase_count==20);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);read_mismatch=1;read_mismatch_offset=0x640000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_partial")&&strstr(r.data,"0:APPSBL_1"));
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);read_mismatch=1;read_mismatch_offset=0x500000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_partial")&&strstr(r.data,"0:APPSBL"));
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);read_mismatch=1;read_mismatch_offset=0x640000;read_mismatch_after_erases=20;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&strstr(r.data,"uboot_update_partial")&&strstr(r.data,"0:APPSBL_1")&&erase_count==20&&extent_matches(0x500000,0x140000,upload,(size_t)n));
 rt3_metadata_host_test_new_boot();
 /* Race changes after opening the exact window must be caught before erasing. */
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);race_layout_on_open=1;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret==-EXDEV&&erase_count==0);
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);race_offset_on_open=0x640000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&erase_count==0&&strstr(r.data,"uboot_update_failed"));
 rt3_metadata_host_test_new_boot();reset_layout();reset_faults();CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);race_offset_on_open=0x500000;ret=rt3_metadata_write(RT3_MD_UBOOT,tok,&r);CHECK(ret!=0&&erase_count==0&&strstr(r.data,"uboot_update_failed"));
 if(argc==3){
  f=fopen(argv[2],"rb");CHECK(f);fseek(f,0,SEEK_END);n=ftell(f);fseek(f,0,SEEK_SET);CHECK(n==473088);CHECK(fread(upload,1,n,f)==(size_t)n);fclose(f);
  CHECK(rt3_md_uboot_image_name(upload,(uint32_t)n)!=NULL);
  reset_layout();reset_faults();memset(flash+0x500000,0x5a,0x280000);
  CHECK(rt3_metadata_validate(RT3_MD_UBOOT,upload,(uint32_t)n,tok,dig)==0);
  CHECK(rt3_metadata_write(RT3_MD_UBOOT,tok,&r)==0);
  CHECK(extent_matches(0x500000,0x140000,upload,(size_t)n)&&extent_matches(0x640000,0x140000,upload,(size_t)n));
 }
 rt3_metadata_invalidate();free(flash);puts("RT3000-B production metadata writer host tests: PASS");return 0;
}
