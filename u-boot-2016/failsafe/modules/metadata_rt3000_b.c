/* SPDX-License-Identifier: GPL-2.0+ */
/* RT3000-B metadata writer: owned staging, exact SMEM extents, ECC and full readback. */
#include <common.h>
#include <malloc.h>
#include <asm/errno.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <net/httpd.h>
#include <u-boot/sha256.h>
#include <ipq_api.h>
#include <rt3000_b_layout.h>
#include <rt3000_b_fw_write.h>
#include <rt3000_b_metadata.h>
#include <rt3000_mibib.h>
#include <h3c_build_id.h>
#include <bootconfig.h>
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
#include <h3c_nand_ecc.h>
#endif
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
#include <nx30_d_service.h>
#endif
/* The /upload parser places a single form value within the first 64 KiB. */
#define MD_RAM_PREFIX_MAX (64UL * 1024)
#define MD_CHIP 0x08000000U
/* Every board keeps our U-Boot in both APPSBL copies: SBL may load either,
 * and OEM upgrades flip the BOOTCONFIG APPSBL selector. */
#define RT3_MD_B_DUAL_APPSBL 1
struct md_extent{const char*name;u32 off,size;};
struct md_pending{u8*data;u32 size,alloc_size;enum rt3_md_kind kind;enum rt3_md_target target;u32 target_off,target_size,second_off,second_size,layout_fingerprint;u8 hash[32],source_hash[32];char token[65];int valid;};
static struct md_pending pending;
#ifdef RT3_MD_B_DUAL_APPSBL
static u8 *uboot_backup_primary, *uboot_backup_copy1;
static u8 *uboot_workspace_page, *uboot_workspace_check, *uboot_workspace_want;
static int uboot_partial_locked;
#endif
#ifdef RT3_MD_B_DUAL_APPSBL
static const u32 uboot_partition_size=RT3_MD_APPSBL_SIZE;
#endif
static void fail(struct httpd_response*,const char*,const char*);
#ifdef RT3_METADATA_HOST_TEST
static int md_test_alloc_fail_after=-1;
void rt3_metadata_host_test_fail_alloc_after(int count){md_test_alloc_fail_after=count;}
#endif
#ifdef RT3_MD_B_DUAL_APPSBL
static void *uboot_alloc(size_t size)
{
#ifdef RT3_METADATA_HOST_TEST
 if(md_test_alloc_fail_after==0)return NULL;
 if(md_test_alloc_fail_after>0)md_test_alloc_fail_after--;
#endif
 return malloc(size);
}
static void *uboot_memalign(size_t align,size_t size)
{
#ifdef RT3_METADATA_HOST_TEST
 if(md_test_alloc_fail_after==0)return NULL;
 if(md_test_alloc_fail_after>0)md_test_alloc_fail_after--;
#endif
 return memalign(align,size);
}
#endif
static u32 generation;
static char reply[256];
static const struct md_extent appsbl={"0:APPSBL",0x00500000,RT3_MD_APPSBL_SIZE};
static const struct md_extent art={"0:ART",0x00780000,RT3_MD_ART_SIZE};
static const struct md_extent cdt={"0:CDT",0x00400000,RT3_MD_CDT_SIZE};
static const struct md_extent cdt1={"0:CDT_1",0x00440000,RT3_MD_CDT_SIZE};
static const struct md_extent mibib={"0:MIBIB",RT3_MIBIB_OFFSET,RT3_MIBIB_SIZE};
static const struct md_extent*primary(enum rt3_md_kind k){switch(k){case RT3_MD_UBOOT:return &appsbl;case RT3_MD_ART:return &art;case RT3_MD_CDT:return &cdt;case RT3_MD_MIBIB:return &mibib;default:return NULL;}}
static void hash(const void*p,u32 n,u8 out[32]){sha256_csum_wd(p,n,out,CHUNKSZ_SHA256);}
static void hex(const u8*p,char*out){static const char h[]="0123456789abcdef";int i;for(i=0;i<32;i++){out[2*i]=h[p[i]>>4];out[2*i+1]=h[p[i]&15];}out[64]=0;}
static int layout_read(struct rt3_layout*l)
{
 int r;if(!l)return -EINVAL;r=rt3_layout_read(l);
 return r||l->mode==RT3_LAYOUT_UNSUPPORTED?-EXDEV:0;
}
static int known_extent(const struct md_extent*e,u32 off,u32 size)
{
 if(!e||size!=e->size)return 0;
 if(!strcmp(e->name,"0:APPSBL")||!strcmp(e->name,"0:APPSBL_1"))
  return off==0x00500000||off==0x00640000;
 if(!strcmp(e->name,"0:CDT")||!strcmp(e->name,"0:CDT_1"))
  return off==0x00400000||off==0x00440000;
 if(!strcmp(e->name,"0:ART"))return off==0x00780000;
 if(!strcmp(e->name,"0:MIBIB"))return off==RT3_MIBIB_OFFSET;
 return 0;
}
/* MIBIB structure, then the layout rules our U-Boot boots with: boot-chain
 * rows SBL1..TRAINING unchanged and a supported dual or single rootfs map. */
static int mibib_validate(const void*data,u32 n)
{
 struct rt3_layout l;
 if(rt3_mibib_check(data,n,NULL))return -1;
 if(rt3_layout_resolve_raw((const u8*)data+RT3_MIBIB_TABLE_PAGE(0),RT3_MIBIB_PAGE,&l)||
    l.mode==RT3_LAYOUT_UNSUPPORTED)return -1;
 return 0;
}
static int layout_extent(const struct rt3_layout*l,const struct md_extent*e,struct md_extent*out)
{
 u32 off=0,size=0;int r;if(!l||l->mode==RT3_LAYOUT_UNSUPPORTED||!e||!out)return -EXDEV;
 r=getpart_offset_size((char*)e->name,&off,&size);if(r||!known_extent(e,off,size))return -EXDEV;
 *out=*e;out->off=off;out->size=size;return 0;
}
static int layout_targets(enum rt3_md_kind k,struct rt3_layout*l,struct md_extent*first,struct md_extent*second)
{
 const struct md_extent*e=primary(k),*e2=k==RT3_MD_CDT?&cdt1:NULL;
 struct md_extent sibling_template,sibling;
 if(!e||!l||!first||!second||layout_read(l))return -EXDEV;
 if(layout_extent(l,e,first))return -EXDEV;
 memset(second,0,sizeof(*second));
 if(k==RT3_MD_CDT){
  if(layout_extent(l,e2,second)||first->size!=second->size||first->off==second->off)return -EXDEV;
 }else if(k==RT3_MD_UBOOT){
  sibling_template=(struct md_extent){"0:APPSBL_1",0,RT3_MD_APPSBL_SIZE};
  if(layout_extent(l,&sibling_template,&sibling)||sibling.size!=first->size||sibling.off==first->off)return -EXDEV;
  *second=sibling;
 }
 return 0;
}
static int data_ok(const void*p,u32 n)
{
 uintptr_t s=(uintptr_t)p,base=httpd_upload_buffer_base(),payload_end;
 if(!p||!n||n>RT3_MD_APPSBL_SIZE||s>(uintptr_t)-1-n||
    base>(uintptr_t)-1-MD_RAM_PREFIX_MAX-RT3_MD_APPSBL_SIZE)return 0;
 payload_end=base+MD_RAM_PREFIX_MAX+RT3_MD_APPSBL_SIZE;
 return s>=base&&s<base+MD_RAM_PREFIX_MAX&&s+n<=payload_end;
}
static void make_token(enum rt3_md_kind k,enum rt3_md_target target,u32 n,
                       const u8 source[32],const u8 normalized[32],
                       u32 fingerprint,const struct md_extent*first,
                       const struct md_extent*second,char out[65])
{
 u8 seed[97],d[32];u32 g=++generation,r=(u32)rand();
 memset(seed,0,sizeof(seed));memcpy(seed,source,32);memcpy(seed+32,normalized,32);
 seed[64]=k;seed[65]=n;seed[66]=n>>8;seed[67]=n>>16;seed[68]=n>>24;
 seed[69]=g;seed[70]=g>>8;seed[71]=g>>16;seed[72]=g>>24;seed[73]=r;seed[74]=r>>8;seed[75]=r>>16;
 seed[76]=fingerprint;seed[77]=fingerprint>>8;seed[78]=fingerprint>>16;seed[79]=fingerprint>>24;
 if(first){seed[80]=first->off;seed[81]=first->off>>8;seed[82]=first->off>>16;seed[83]=first->off>>24;seed[84]=first->size;seed[85]=first->size>>8;seed[86]=first->size>>16;seed[87]=first->size>>24;}
 if(second){seed[88]=second->off;seed[89]=second->off>>8;seed[90]=second->off>>16;seed[91]=second->off>>24;seed[92]=second->size;seed[93]=second->size>>8;seed[94]=second->size>>16;seed[95]=second->size>>24;}
 seed[96]=target;
 sha256_csum_wd(seed,sizeof(seed),d,CHUNKSZ_SHA256);hex(d,out);
}
void rt3_metadata_invalidate(void){if(pending.data){memset(pending.data,0,pending.alloc_size);free(pending.data);}memset(&pending,0,sizeof(pending));}
int rt3_metadata_target_parse(const char *data,u32 size,enum rt3_md_target *target)
{
 if(!data||!target)return -EINVAL;
 if(size==5&&!memcmp(data,"0:CDT",5)){*target=RT3_MD_TARGET_PRIMARY;return 0;}
 if(size==7&&!memcmp(data,"0:CDT_1",7)){*target=RT3_MD_TARGET_SECONDARY;return 0;}
 return -EINVAL;
}
const char *rt3_metadata_target_name(enum rt3_md_target target)
{
 switch(target){
 case RT3_MD_TARGET_DEFAULT:return "no_target";
 case RT3_MD_TARGET_PRIMARY:return "0:CDT";
 case RT3_MD_TARGET_SECONDARY:return "0:CDT_1";
 default:return "unknown";
 }
}
static int resolve_target(enum rt3_md_kind kind,enum rt3_md_target requested,
                          struct rt3_layout*l,struct md_extent*first,
                          struct md_extent*second)
{
 struct md_extent primary_target,secondary_target;
 if(!first||!second||!l)return -EINVAL;
 if(layout_targets(kind,l,&primary_target,&secondary_target))return -EXDEV;
 memset(second,0,sizeof(*second));
 if(kind==RT3_MD_CDT){
  if(requested==RT3_MD_TARGET_PRIMARY){*first=primary_target;return 0;}
  if(requested==RT3_MD_TARGET_SECONDARY){*first=secondary_target;return 0;}
#if !defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) && !defined(CONFIG_H3C_HW_A_RAM_READONLY) && \
    !(defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) && !defined(CONFIG_H3C_HW_C_RAM_READONLY)
  if(requested==RT3_MD_TARGET_DEFAULT){*first=primary_target;*second=secondary_target;return 0;}
#endif
  return -EINVAL;
 }
 if(requested!=RT3_MD_TARGET_DEFAULT&&requested!=RT3_MD_TARGET_PRIMARY)return -EINVAL;
 *first=primary_target;
 if(kind==RT3_MD_UBOOT)*second=secondary_target;
 return 0;
}
int rt3_metadata_validate_target(enum rt3_md_kind k,enum rt3_md_target requested,
                                 const void*data,u32 n,char token[65],
                                 char digest_hex[65])
{
 const struct md_extent*e=primary(k);struct rt3_layout l;
 struct md_extent target,second;u8 d[32],src_hash[32],*owned;
 u32 staged_size=n;int r;
 if(token)token[0]=0;
 if(digest_hex)digest_hex[0]=0;
 rt3_metadata_invalidate();
 if(rt3_flash_reboot_required())return -EAGAIN;
 if(!e||!token||!digest_hex||!data_ok(data,n)||n>e->size)return -EINVAL;
 r=resolve_target(k,requested,&l,&target,&second);if(r)return r;
#ifdef CONFIG_H3C_NAND_ECC_COMPAT
 /* Preview is an install authorization boundary too: never mint a token for
  * a target whose front-end ECC mode or prefix compatibility is unverified. */
 if(!h3c_nand_ecc_write_allowed(&nand_info[CONFIG_NAND_FLASH_INFO_IDX],
                                target.off,target.size)||
    (second.size&&!h3c_nand_ecc_write_allowed(
         &nand_info[CONFIG_NAND_FLASH_INFO_IDX],second.off,second.size)))
  return -EPERM;
#endif
 hash(data,n,src_hash);
 if(k==RT3_MD_UBOOT){
  owned=malloc(e->size);if(!owned)return -ENOMEM;
  if(rt3_md_prepare_elf(data,n,owned,e->size,&staged_size)){memset(owned,0,e->size);free(owned);return -EINVAL;}
  /* Same board, NAND profile and ECC as this build, or nothing is written. */
  if(h3c_build_id_check(owned,staged_size)){memset(owned,0,e->size);free(owned);return -ENOEXEC;}
 }else{
  if(k==RT3_MD_MIBIB?mibib_validate(data,n):rt3_md_validate_image(k,data,n))return -EINVAL;
  owned=malloc(n);if(!owned)return -ENOMEM;
  if(rt3_md_snapshot_copy(owned,data,n)){memset(owned,0,n);free(owned);return -EINVAL;}
 }
 hash(owned,staged_size,d);
 pending.data=owned;pending.kind=k;pending.target=requested;pending.size=staged_size;
 pending.alloc_size=k==RT3_MD_UBOOT?e->size:n;pending.target_off=target.off;
 pending.target_size=target.size;pending.second_off=second.off;pending.second_size=second.size;
 pending.layout_fingerprint=l.fingerprint;memcpy(pending.hash,d,32);
 memcpy(pending.source_hash,src_hash,32);pending.valid=1;
 make_token(k,requested,staged_size,src_hash,d,pending.layout_fingerprint,
           &target,second.size?&second:NULL,pending.token);
 memcpy(token,pending.token,65);hex(d,digest_hex);return 0;
}
int rt3_metadata_validate(enum rt3_md_kind k,const void*data,u32 n,
                          char token[65],char digest_hex[65])
{
 return rt3_metadata_validate_target(k,RT3_MD_TARGET_DEFAULT,data,n,token,digest_hex);
}
int rt3_metadata_pending_target(enum rt3_md_kind k,u32*off,u32*size,u32*second_off,u32*second_size)
{
 if(!pending.valid||pending.kind!=k||!off||!size||!second_off||!second_size)return -EINVAL;
 *off=pending.target_off;*size=pending.target_size;*second_off=pending.second_off;*second_size=pending.second_size;return 0;
}
const char *rt3_metadata_validation_error(int error)
{
 switch(error){case -ENOEXEC:return "uboot_build_mismatch";case -EAGAIN:return "reboot_required";case -EXDEV:return "partition_layout_mismatch";case -ENOMEM:return "metadata_staging_unavailable";case -EPERM:return "install_ecc_unverified";default:return "invalid_metadata_image";}
}
const char *rt3_metadata_preview_name(enum rt3_md_kind kind,const void*data,u32 size)
{
 const char*name;
 switch(kind){
 case RT3_MD_UBOOT:
  name=rt3_md_uboot_image_name(data,size);
  return name?name:rt3_md_uboot_board_name();
 case RT3_MD_ART:return "ART image (identity unverified)";
 case RT3_MD_CDT:return "CDT image";
 case RT3_MD_MIBIB:return "MIBIB partition table";
 default:return NULL;
 }
}
static int mtd_ok(struct mtd_info*m,const struct md_extent*e)
{
 if(!m||!e||m->type!=MTD_NANDFLASH||m->size!=MD_CHIP||m->writesize!=RT3_MD_PAGE_SIZE||
 m->erasesize!=RT3_MD_ERASE_SIZE||e->off%m->erasesize||e->size%m->erasesize||
 e->off>m->size||e->size>m->size-e->off)
  return -EINVAL;
 return 0;
}
static int no_bad(struct mtd_info*m,const struct md_extent*e)
{
 u32 x;for(x=0;x<e->size;x+=m->erasesize){int r=nand_block_isbad(m,e->off+x);if(r)return r>0?-EIO:r;}return 0;
}
static void page_from(const u8*d,u32 n,u32 off,u8*out)
{
 u32 take=off<n?n-off:0;if(take>RT3_MD_PAGE_SIZE)take=RT3_MD_PAGE_SIZE;
 memset(out,0xff,RT3_MD_PAGE_SIZE);if(take)memcpy(out,d+off,take);
}
static int read_page(struct mtd_info*m,u32 off,u8*p)
{
 size_t done=0;int r=mtd_read(m,off,RT3_MD_PAGE_SIZE,&done,p);
 if(r==-EUCLEAN&&done==RT3_MD_PAGE_SIZE)r=0;
 return r||done!=RT3_MD_PAGE_SIZE?(r?r:-EIO):0;
}
static int write_extent(const struct md_extent*e,const u8*d,u32 n,u32 fingerprint)
{
 struct mtd_info*m=&nand_info[CONFIG_NAND_FLASH_INFO_IDX];struct erase_info er;struct rt3_layout l;struct md_extent current;
 u8*page=NULL,*check=NULL;u32 off;size_t done;int r,opened=0;
 if(layout_read(&l)||l.fingerprint!=fingerprint||layout_extent(&l,e,&current)||current.off!=e->off||current.size!=e->size)return -EXDEV;
 if((r=mtd_ok(m,e))||(r=no_bad(m,e)))return r;
 page=memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);check=memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);
 if(!page||!check){r=-ENOMEM;goto out;}
 r=rt3_metadata_write_window_open(m,e->name,e->off,e->size);if(r)goto out;opened=1;
 for(off=0;off<e->size;off+=m->erasesize){memset(&er,0,sizeof(er));er.mtd=m;er.addr=e->off+off;er.len=m->erasesize;r=mtd_erase(m,&er);if(r)goto out;}
 for(off=0;off<e->size;off+=RT3_MD_PAGE_SIZE){
  page_from(d,n,off,page);if(memchr_inv(page,0xff,RT3_MD_PAGE_SIZE)){done=0;r=mtd_write(m,e->off+off,RT3_MD_PAGE_SIZE,&done,page);if(r||done!=RT3_MD_PAGE_SIZE){r=r?r:-EIO;goto out;}}
 }
 for(off=0;off<e->size;off+=RT3_MD_PAGE_SIZE){page_from(d,n,off,page);r=read_page(m,e->off+off,check);if(r||memcmp(page,check,RT3_MD_PAGE_SIZE)){r=r?r:-EIO;goto out;}}
 r=0;
 out:if(opened){rt3_fw_write_window_close();
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
 if(rt3_fw_write_window_close_status())r=-EIO;
#endif
 }free(check);free(page);return r;
}
/* MIBIB is rewritten one erase block at a time: the older copy first, then
 * the copy SBL now boots from, then the empty blocks, each read back before
 * the next is erased, so a power cut always leaves one complete table. SMEM
 * still holds the old table, so any erase locks flash writes until reboot. */
static int mibib_write(const struct md_extent*e,const u8*d,u32 n,u32 fingerprint)
{
 struct mtd_info*m=&nand_info[CONFIG_NAND_FLASH_INFO_IDX];struct erase_info er;struct rt3_layout l;struct md_extent current;
 u32 order[4]={0,1,2,3},age[RT3_MIBIB_COPIES],blk,off,i;u8*page=NULL,*check=NULL,*copy=NULL;size_t done;int r=0,opened=0,valid=0;
 if(n!=RT3_MIBIB_SIZE||e->size!=RT3_MIBIB_SIZE)return -EINVAL;
 if(layout_read(&l)||l.fingerprint!=fingerprint||layout_extent(&l,e,&current)||current.off!=e->off||current.size!=e->size)return -EXDEV;
 if((r=mtd_ok(m,e))||(r=no_bad(m,e)))return r;
 page=memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);check=memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);copy=malloc(RT3_MIBIB_BLOCK);
 if(!page||!check||!copy){r=-ENOMEM;goto out;}
 for(i=0;i<RT3_MIBIB_COPIES;i++){
  for(off=0;off<RT3_MIBIB_BLOCK;off+=RT3_MD_PAGE_SIZE){
   if(read_page(m,e->off+i*RT3_MIBIB_BLOCK+off,check))break;
   memcpy(copy+off,check,RT3_MD_PAGE_SIZE);
  }
  if(off==RT3_MIBIB_BLOCK&&!rt3_mibib_block_age(copy,RT3_MIBIB_BLOCK,&age[i]))valid|=1<<i;
 }
 if(valid==3&&age[0]>age[1]){order[0]=1;order[1]=0;}
 r=rt3_metadata_write_window_open(m,e->name,e->off,e->size);if(r)goto out;opened=1;
 for(i=0;i<4;i++){
  blk=e->off+order[i]*RT3_MIBIB_BLOCK;
  memset(&er,0,sizeof(er));er.mtd=m;er.addr=blk;er.len=m->erasesize;
  rt3_flash_require_reboot();
  r=mtd_erase(m,&er);if(r)goto out;
  for(off=0;off<RT3_MIBIB_BLOCK;off+=RT3_MD_PAGE_SIZE){
   page_from(d,n,order[i]*RT3_MIBIB_BLOCK+off,page);
   if(memchr_inv(page,0xff,RT3_MD_PAGE_SIZE)){done=0;r=mtd_write(m,blk+off,RT3_MD_PAGE_SIZE,&done,page);if(r||done!=RT3_MD_PAGE_SIZE){r=r?r:-EIO;goto out;}}
  }
  for(off=0;off<RT3_MIBIB_BLOCK;off+=RT3_MD_PAGE_SIZE){
   page_from(d,n,order[i]*RT3_MIBIB_BLOCK+off,page);r=read_page(m,blk+off,check);
   if(r||memcmp(page,check,RT3_MD_PAGE_SIZE)){r=r?r:-EIO;goto out;}
  }
 }
 r=0;
 out:if(opened){rt3_fw_write_window_close();
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
 if(rt3_fw_write_window_close_status())r=-EIO;
#endif
 }free(copy);free(check);free(page);return r;
}
#ifdef RT3_MD_B_DUAL_APPSBL
static int read_extent(struct mtd_info*m,const struct md_extent*e,u8*out)
{
 u8*page;u32 off;int r=0;
 page=uboot_memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);if(!page)return -ENOMEM;
 for(off=0;off<e->size;off+=RT3_MD_PAGE_SIZE){
  r=read_page(m,e->off+off,page);if(r)break;
  memcpy(out+off,page,RT3_MD_PAGE_SIZE);
 }
 free(page);return r;
}
static int extent_matches_bytes(struct mtd_info*m,const struct md_extent*e,const u8*want)
{
 u32 off;int r=0;if(!uboot_workspace_page)return -ENOMEM;
 for(off=0;off<e->size;off+=RT3_MD_PAGE_SIZE){
  r=read_page(m,e->off+off,uboot_workspace_page);if(r)break;
  if(memcmp(uboot_workspace_page,want+off,RT3_MD_PAGE_SIZE)){r=-EIO;break;}
 }
 return r;
}
static int extent_matches_candidate(struct mtd_info*m,const struct md_extent*e,
                                    const u8*d,u32 n)
{
 u32 off;int r=0;
 if(!uboot_workspace_page||!uboot_workspace_want)return -ENOMEM;
 for(off=0;off<e->size;off+=RT3_MD_PAGE_SIZE){
  page_from(d,n,off,uboot_workspace_want);
  r=read_page(m,e->off+off,uboot_workspace_page);if(r)break;
  if(memcmp(uboot_workspace_page,uboot_workspace_want,RT3_MD_PAGE_SIZE)){r=-EIO;break;}
 }
 return r;
}
static int uboot_preflight(struct mtd_info*m,const struct md_extent*primary_target,
                           const struct md_extent*copy1_target,u8**primary_original,
                           u8**copy1_original)
{
 u8*p0=NULL,*p1=NULL,*page=NULL,*check=NULL,*want=NULL;int r;
 *primary_original=NULL;*copy1_original=NULL;
 if((r=mtd_ok(m,primary_target))||(r=mtd_ok(m,copy1_target)))return r;
 if(m->ecc_strength!=4&&m->ecc_strength!=8)return -EOPNOTSUPP;
 if((r=no_bad(m,primary_target))||(r=no_bad(m,copy1_target)))return r;
 p0=uboot_alloc(uboot_partition_size);p1=uboot_alloc(uboot_partition_size);
 page=uboot_memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);
 check=uboot_memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);
 want=uboot_memalign(ARCH_DMA_MINALIGN,RT3_MD_PAGE_SIZE);
 if(!p0||!p1||!page||!check||!want){r=-ENOMEM;goto fail;}
 if((r=read_extent(m,primary_target,p0))||(r=read_extent(m,copy1_target,p1)))goto fail;
 free(uboot_workspace_page);free(uboot_workspace_check);free(uboot_workspace_want);
 uboot_workspace_page=page;uboot_workspace_check=check;uboot_workspace_want=want;
 *primary_original=p0;*copy1_original=p1;return 0;
 fail:free(want);free(check);free(page);free(p1);free(p0);return r;
}
static int uboot_write_extent(const struct md_extent*target,
                              const struct md_extent*other,
                              const u8*own_baseline,const u8*other_baseline,int other_is_candidate,
                              const u8*d,u32 n,u32 fingerprint,int*mutated)
{
 struct mtd_info*m=&nand_info[CONFIG_NAND_FLASH_INFO_IDX];
 struct erase_info er;struct rt3_layout l;struct md_extent now_target,now_other;
 u8*page=uboot_workspace_page,*check=uboot_workspace_check;u32 off;size_t done;int r,opened=0;
 if((r=mtd_ok(m,target))||(r=mtd_ok(m,other)))return r;
 if(m->ecc_strength!=4&&m->ecc_strength!=8)return -EOPNOTSUPP;
 if(!page||!check){r=-ENOMEM;goto out;}
 r=rt3_metadata_write_window_open(m,target->name,target->off,target->size);
 if(r)goto out;
 opened=1;
 /* Revalidate both dynamic extents and both snapshots inside the exclusive window. */
 if(layout_read(&l)||l.fingerprint!=fingerprint||
    layout_extent(&l,target,&now_target)||layout_extent(&l,other,&now_other)||
    now_target.off!=target->off||now_target.size!=target->size||
    now_other.off!=other->off||now_other.size!=other->size){r=-EXDEV;goto out;}
 r=extent_matches_bytes(m,target,own_baseline);if(r)goto out;
 if(other_is_candidate)
  r=extent_matches_candidate(m,other,d,n);
 else
  r=extent_matches_bytes(m,other,other_baseline);
 if(r)goto out;
 for(off=0;off<target->size;off+=m->erasesize){
  memset(&er,0,sizeof(er));er.mtd=m;er.addr=target->off+off;er.len=m->erasesize;
  *mutated=1;r=mtd_erase(m,&er);if(r)goto out;
 }
 for(off=0;off<target->size;off+=RT3_MD_PAGE_SIZE){
  page_from(d,n,off,page);
  if(memchr_inv(page,0xff,RT3_MD_PAGE_SIZE)){
   done=0;r=mtd_write(m,target->off+off,RT3_MD_PAGE_SIZE,&done,page);
   if(r||done!=RT3_MD_PAGE_SIZE){r=r?r:-EIO;goto out;}
  }
 }
 for(off=0;off<target->size;off+=RT3_MD_PAGE_SIZE){
  page_from(d,n,off,page);r=read_page(m,target->off+off,check);
  if(r||memcmp(page,check,RT3_MD_PAGE_SIZE)){r=r?r:-EIO;goto out;}
 }
 r=0;
 out:if(opened)rt3_fw_write_window_close();return r;
}
static int uboot_update(enum rt3_md_kind k,const struct md_pending*tx,
                        struct httpd_response*r,const struct md_extent*primary_target,
                        const struct md_extent*copy1_target)
{
 struct mtd_info*m=&nand_info[CONFIG_NAND_FLASH_INFO_IDX];
 const struct md_extent*first_target,*last_target;const u8*first_backup,*last_backup;
 u8*original_primary=NULL,*original_copy1=NULL;int ret,mutated=0;u8 digest[32];
 if(uboot_partial_locked){fail(r,"uboot_update_locked_after_partial",primary_target->name);return -EIO;}
 if(!tx->data||!tx->size||rt3_md_validate_image(k,tx->data,tx->size)){
  fail(r,"owned_staging_validation_failed",primary_target->name);return -EINVAL;
 }
 hash(tx->data,tx->size,digest);
 if(!rt3_md_hash_matches(tx->hash,digest,32)){
  fail(r,"owned_staging_validation_failed",primary_target->name);return -EIO;
 }
 if((ret=uboot_preflight(m,primary_target,copy1_target,&original_primary,&original_copy1))){
  fail(r,"uboot_update_preflight_failed",ret==-ENOMEM?"RAM":primary_target->name);return ret;
 }
 /* Publish only a complete, successfully read pair; previews and failed preflight
  * leave the previous pair available to the two backup routes. */
 free(uboot_backup_primary);free(uboot_backup_copy1);
 uboot_backup_primary=original_primary;uboot_backup_copy1=original_copy1;
 original_primary=NULL;original_copy1=NULL;
 /* Write the copy SBL is not loading from first; the selected one goes last. */
 if(get_bootconfig("0:APPSBL")==1){
  first_target=primary_target;first_backup=uboot_backup_primary;
  last_target=copy1_target;last_backup=uboot_backup_copy1;
 }else{
  first_target=copy1_target;first_backup=uboot_backup_copy1;
  last_target=primary_target;last_backup=uboot_backup_primary;
 }
 ret=uboot_write_extent(first_target,last_target,first_backup,
                        last_backup,0,tx->data,tx->size,tx->layout_fingerprint,&mutated);
 if(ret){
  if(mutated){uboot_partial_locked=1;fail(r,"uboot_update_partial",first_target->name);}
  else fail(r,"uboot_update_failed",first_target->name);
  return ret;
 }
 ret=uboot_write_extent(last_target,first_target,last_backup,
                        tx->data,1,tx->data,tx->size,tx->layout_fingerprint,&mutated);
 if(ret){uboot_partial_locked=1;fail(r,"uboot_update_partial",last_target->name);return ret;}
 ret=extent_matches_candidate(m,copy1_target,tx->data,tx->size);
 if(ret){uboot_partial_locked=1;fail(r,"uboot_update_partial",copy1_target->name);return ret;}
 ret=extent_matches_candidate(m,primary_target,tx->data,tx->size);
 if(ret){uboot_partial_locked=1;fail(r,"uboot_update_partial",primary_target->name);return ret;}
 return 0;
}
#endif
static void fail(struct httpd_response*r,const char*type,const char*target)
{
 snprintf(reply,sizeof(reply),"{\"status\":\"fail\",\"info\":{\"type\":\"%s\",\"target\":\"%s\"}}",type,target);
 r->status=HTTP_RESP_STD;r->info.code=200;r->info.connection_close=1;r->info.content_type="application/json";r->data=reply;r->size=strlen(reply);
}
#ifdef CONFIG_H3C_HW_C_RAM_READONLY
static int metadata_readonly(struct httpd_response *r)
{
 if(!r)return -EINVAL;
 r->status=HTTP_RESP_STD;r->info.code=403;r->info.connection_close=1;
 r->info.content_type="application/json";
 r->data="{\"status\":\"fail\",\"error\":\"read-only target\"}";
 r->size=strlen(r->data);return -EINVAL;
}
#endif
static int metadata_write_internal(enum rt3_md_kind k,enum rt3_md_target requested,
                                   const char*token,struct httpd_response*r)
{
 const struct md_extent*e=primary(k);struct md_extent target,second;
 struct md_extent current_primary,current_secondary;struct rt3_layout l;
 struct md_pending tx;u8 d[32];int ret;
#if defined(CONFIG_H3C_HW_C_RAM_READONLY)
 rt3_metadata_invalidate();
 return metadata_readonly(r);
#endif
 if(!r)return -EINVAL;
 if(!pending.valid||!token||strlen(token)!=64||strcmp(token,pending.token)||
    pending.kind!=k||!e||requested!=pending.target){
  rt3_metadata_invalidate();fail(r,"stale_or_mismatched_commit",e?e->name:"none");return -EINVAL;
 }
 tx=pending;memset(&pending,0,sizeof(pending));
 if(!tx.data||!tx.size){ret=-EINVAL;goto invalid;}
 if(k==RT3_MD_MIBIB?mibib_validate(tx.data,tx.size):
    (rt3_md_validate_image(k,tx.data,tx.size)||
     (k==RT3_MD_UBOOT&&h3c_build_id_check(tx.data,tx.size)))){ret=-EINVAL;goto invalid;}
 hash(tx.data,tx.size,d);if(!rt3_md_hash_matches(tx.hash,d,32)){ret=-EIO;goto invalid;}
 ret=layout_targets(k,&l,&current_primary,&current_secondary);
 if(ret||l.fingerprint!=tx.layout_fingerprint){ret=-EXDEV;fail(r,"partition_layout_mismatch",e->name);goto done;}
 memset(&target,0,sizeof(target));memset(&second,0,sizeof(second));
 if(k==RT3_MD_CDT){
  if(tx.target==RT3_MD_TARGET_PRIMARY)target=current_primary;
  else if(tx.target==RT3_MD_TARGET_SECONDARY)target=current_secondary;
#if !defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) && !defined(CONFIG_H3C_HW_A_RAM_READONLY) && \
    !(defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)) && !defined(CONFIG_H3C_HW_C_RAM_READONLY)
  else if(tx.target==RT3_MD_TARGET_DEFAULT){target=current_primary;second=current_secondary;}
#endif
  else {ret=-EXDEV;fail(r,"partition_layout_mismatch",e->name);goto done;}
 }else{
  if(tx.target!=RT3_MD_TARGET_DEFAULT&&tx.target!=RT3_MD_TARGET_PRIMARY){ret=-EXDEV;fail(r,"partition_layout_mismatch",e->name);goto done;}
  target=current_primary;
  if(k==RT3_MD_UBOOT)second=current_secondary;
 }
 if(target.off!=tx.target_off||target.size!=tx.target_size||
    second.off!=tx.second_off||second.size!=tx.second_size){
  ret=-EXDEV;fail(r,"partition_layout_mismatch",target.name);goto done;
 }
 if(k==RT3_MD_UBOOT){
  ret=uboot_update(k,&tx,r,&target,&second);goto done;
 }
 if(k==RT3_MD_MIBIB){
  ret=mibib_write(&target,tx.data,tx.size,tx.layout_fingerprint);
  if(ret)fail(r,"metadata_write_or_readback_failed",target.name);
  goto done;
 }
 if(k==RT3_MD_CDT&&second.size){
  /* B legacy DEFAULT preserves secondary-before-primary paired writes. */
  ret=write_extent(&second,tx.data,tx.size,tx.layout_fingerprint);
  if(ret){fail(r,"metadata_write_or_readback_failed",second.name);goto done;}
 }
 ret=write_extent(&target,tx.data,tx.size,tx.layout_fingerprint);
 if(ret)fail(r,"metadata_write_or_readback_failed",target.name);
 goto done;
invalid:
 fail(r,"owned_staging_validation_failed",e?e->name:"none");
done:
 if(tx.data){memset(tx.data,0,tx.alloc_size);free(tx.data);}
 return ret;
}
int rt3_metadata_write_target(enum rt3_md_kind k,enum rt3_md_target target,
                              const char*token,struct httpd_response*r)
{
 return metadata_write_internal(k,target,token,r);
}
int rt3_metadata_write(enum rt3_md_kind k,const char*token,struct httpd_response*r)
{
 return metadata_write_internal(k,RT3_MD_TARGET_DEFAULT,token,r);
}
#ifdef RT3_MD_B_DUAL_APPSBL
static void uboot_backup_handler(enum httpd_uri_handler_status status,
                                 struct httpd_request*request,
                                 struct httpd_response*response,int copy1)
{
 const u8*data=copy1?uboot_backup_copy1:uboot_backup_primary;
 if(status!=HTTP_CB_NEW)return;
 if(!request||request->method!=HTTP_GET){response->status=HTTP_RESP_STD;response->info.code=405;response->info.connection_close=1;response->info.content_type="text/plain";response->data="GET required";response->size=12;return;}
 if(!data){response->status=HTTP_RESP_STD;response->info.code=404;response->info.connection_close=1;response->info.content_type="text/plain";response->data="backup unavailable";response->size=19;return;}
 response->status=HTTP_RESP_STD;response->info.code=200;response->info.connection_close=1;
 response->info.content_type="application/octet-stream";response->data=(const char*)data;
 response->size=uboot_partition_size;
}
static void uboot_backup_appsbl_handler(enum httpd_uri_handler_status s,
                                        struct httpd_request*q,
                                        struct httpd_response*r)
{uboot_backup_handler(s,q,r,0);}
static void uboot_backup_appsbl1_handler(enum httpd_uri_handler_status s,
                                         struct httpd_request*q,
                                         struct httpd_response*r)
{uboot_backup_handler(s,q,r,1);}
#endif
#ifdef RT3_METADATA_HOST_TEST
void rt3_metadata_host_test_new_boot(void)
{
 rt3_metadata_invalidate();
#ifdef RT3_MD_B_DUAL_APPSBL
 free(uboot_backup_primary);free(uboot_backup_copy1);
 free(uboot_workspace_page);free(uboot_workspace_check);free(uboot_workspace_want);
 uboot_backup_primary=uboot_backup_copy1=NULL;
 uboot_workspace_page=uboot_workspace_check=uboot_workspace_want=NULL;
 uboot_partial_locked=0;
#endif
 md_test_alloc_fail_after=-1;
}
#endif
void rt3_metadata_register_uri_handlers(struct httpd_instance*inst)
{
#ifdef RT3_MD_B_DUAL_APPSBL
 httpd_register_uri_handler(inst,"/uboot/backup/appsbl.bin",uboot_backup_appsbl_handler,NULL);
 httpd_register_uri_handler(inst,"/uboot/backup/appsbl_1.bin",uboot_backup_appsbl1_handler,NULL);
#endif
}
