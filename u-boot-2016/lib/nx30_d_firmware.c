// SPDX-License-Identifier: GPL-2.0+
/* NX30 HW-D exact-layout UBI/FIT preflight and inactive-slot writer. */
#include <common.h>
#include <asm/errno.h>
#include <asm/cache.h>

#include <linux/mtd/mtd.h>
#include <nand.h>
#include <image.h>
#include <libfdt.h>
#include <u-boot/sha256.h>
#include <u-boot/crc.h>
#include <ipq_api.h>
#include <nx30_d_service.h>
#include <nx30_d_ubi_core.h>
#include <nx30_d_fw_txn_core.h>
#include <nx30_d_read_core.h>
#include <nx30_nor_nand.h>
#include <h3c_nand_ecc.h>

#define NX30_D_UPLOAD_BASE 0x44000000UL
#define NX30_D_UPLOAD_END  0x46810000UL
#define NX30_D_FIT_SCRATCH 0x42000000UL
#define NX30_D_SCRATCH_SIZE 0x01000000U
#define NX30_D_SLOT_BYTES 0x02800000U
#define NX30_D_ERASE_SIZE 0x00020000U
#define NX30_D_DATA_OFFSET 0x1000U
#define NX30_D_LEB_BYTES (NX30_D_ERASE_SIZE-NX30_D_DATA_OFFSET)

struct nx30_d_rootfs_identity { const char *sha256; const char *release; };
/* Hashes cover SquashFS bytes_used, not the rest of an UBI volume or PEB stream. */
static const struct nx30_d_rootfs_identity nx30_d_rootfs_known[] = {
 {"fd4886f558d785c769c95723c182cece72017ed7f7283e40a528577b804aee09", "NX30 R004"},
 {"906a1b69214ba91321036168c6180bd34d40ab6a807e6cd9742e81ee4603a534", "NX30 R007"},
 {"7a0c7f2a2b47582c39c2f31a8cf0a3eccb842c9b8fd2fd39310874466a25142c", "NX30 R008"},
 {"5d54a2447b9880523290eba230608b57e03799d2182e4137c668a51815bf8e28", "NX30 R009"},
 {"d1aaecd082d582904c594f449a4f3c5ca363e4bb13a5d52e21313fb8e857b0cd", "NX30 R010"},
 {"4f9c1b1e146d371f2b85bcdfb4ee165823f99126f2025cb3bd58992858e0fa8b", "NX30 R011/R012 shared rootfs"},
};
struct nx30_d_fw_pending {
 int valid;
 u32 generation, image_crc32, image_bytes;
 enum nx30_d_slot target;
 u8 sha256[SHA256_SUM_LEN];
};
static struct nx30_d_fw_pending nx30_d_fw_pending;
static u32 nx30_d_ubi_crc(u32 seed,const unsigned char *data,unsigned int length)
{
 return crc32_no_comp(seed,data,length);
}
static u32 nx30_d_fw_validate_stage;

static void nx30_d_hex(const u8 *in, size_t len, char *out)
{
 static const char h[]="0123456789abcdef"; size_t i;
 for (i=0;i<len;i++) { out[i*2]=h[in[i]>>4]; out[i*2+1]=h[in[i]&15]; }
 out[len*2]='\0';
}
static int nx30_d_input_range(const void *data,size_t len)
{
 uintptr_t s=(uintptr_t)data,e;
 if (!data || !len || len>NX30_D_SLOT_BYTES || s>(uintptr_t)-1-len) return 0;
 e=s+len; return s>=NX30_D_UPLOAD_BASE && e<=NX30_D_UPLOAD_END;
}
static struct mtd_info *nx30_d_nand(void)
{
 return &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
}
static int nx30_d_mtd_ready(struct mtd_info *mtd)
{
 int ret;
 if (!mtd || mtd->type!=MTD_NANDFLASH || mtd->size!=0x08000000ULL ||
     mtd->erasesize!=NX30_D_ERASE_SIZE || mtd->writesize!=NX30_D_UBI_PAGE_SIZE ||
     mtd->ecc_strength!=4) return -ENODEV;
 ret=h3c_nand_ecc_select_mode(4); if (ret) return ret;
 /* System qualification must not borrow APPSBL/preloader write permission.
  * The transaction later revalidates its actual inactive target at open. */
 if (!h3c_nand_ecc_write_allowed(mtd,0x00900000ULL,NX30_D_SLOT_BYTES) ||
     !h3c_nand_ecc_write_allowed(mtd,0x03100000ULL,NX30_D_SLOT_BYTES))
  return -EPERM;
 return 0;
}
static int nx30_d_rebuild(const u8 *image,const struct nx30_d_ubi_map *map,
                          u32 volume,u32 bytes,const u32 *pebs,const u32 *sizes,
                          u8 *dst,u32 limit)
{
 u32 i,pos=0;
 if (!image || !map || !dst || !bytes || bytes>limit) return -EFBIG;
 for (i=0;pos<bytes;i++) {
  u32 take,avail; const u8 *p;
  if (i>=NX30_D_UBI_PEB_COUNT || pebs[i]>=map->peb_count) return -EINVAL;
  take=bytes-pos; if (take>NX30_D_LEB_BYTES) take=NX30_D_LEB_BYTES;
  avail=sizes[i]; if (avail<take) return -EBADMSG;
  p=image+(size_t)pebs[i]*NX30_D_UBI_PEB_SIZE+NX30_D_DATA_OFFSET;
  memcpy(dst+pos,p,take); pos+=take;
 }
 (void)volume;
 return 0;
}
static int nx30_d_rootfs_match(const u8 *image,const struct nx30_d_ubi_map *map,
                               const char **release)
{
 u8 digest[SHA256_SUM_LEN]; char hex[SHA256_SUM_LEN*2+1];
 sha256_context ctx; u32 i,remaining;
 if (!image || !map || !map->rootfs_bytes || !map->rootfs_lebs ||
     map->rootfs_lebs>NX30_D_UBI_PEB_COUNT) return -EINVAL;
 remaining=map->rootfs_bytes;
 sha256_starts(&ctx);
 for (i=0;remaining;i++) {
  u32 take,peb; const u8 *p;
  if (i>=map->rootfs_lebs) return -EBADMSG;
  peb=map->rootfs_peb[i];
  if (peb>=map->peb_count || peb>=NX30_D_UBI_PEB_COUNT) return -EBADMSG;
  take=remaining>NX30_D_LEB_BYTES?NX30_D_LEB_BYTES:remaining;
  if (map->rootfs_data_size[i]<take) return -EBADMSG;
  p=image+(size_t)peb*NX30_D_UBI_PEB_SIZE+NX30_D_DATA_OFFSET;
  sha256_update(&ctx,p,take); remaining-=take;
 }
 if (i!=map->rootfs_lebs) return -EBADMSG;
 sha256_finish(&ctx,digest); nx30_d_hex(digest,sizeof(digest),hex);
 for (i=0;i<ARRAY_SIZE(nx30_d_rootfs_known);i++)
  if (!strcmp(hex,nx30_d_rootfs_known[i].sha256)) { *release=nx30_d_rootfs_known[i].release; return 0; }
 return -EPERM;
}
static int nx30_d_fit_check(const u8 *image,const struct nx30_d_ubi_map *map,
                            u8 *scratch,const char **release)
{
 int ret,conf,kernel,fdt_node; const void *fdt_data; size_t fdt_size; u8 fdt_comp;
 const char *compat; int len;
 nx30_d_fw_validate_stage=10;
 ret=nx30_d_rootfs_match(image,map,release); if (ret) return ret;
 nx30_d_fw_validate_stage=11;
 ret=nx30_d_rebuild(image,map,map->kernel_id,map->kernel_bytes,
                    map->kernel_peb,map->kernel_data_size,scratch,NX30_D_SCRATCH_SIZE);
 if (ret) return ret;
 nx30_d_fw_validate_stage=12;
 ret=fit_check_format(scratch,map->kernel_bytes); if (ret) return -EBADMSG;
 nx30_d_fw_validate_stage=13;
 if (fdt_totalsize(scratch)!=map->kernel_bytes) return -EBADMSG;
 nx30_d_fw_validate_stage=14;
 conf=fit_conf_get_node(scratch,"config@mp02.1"); if (conf<0) return -ENOENT;
 kernel=fit_conf_get_prop_node(scratch,conf,"kernel");
 fdt_node=fit_conf_get_prop_node(scratch,conf,"fdt");
 if (kernel<0 || fdt_node<0) return -EBADMSG;
#ifdef CONFIG_FIT_SIGNATURE
 if (fit_config_verify(scratch,conf)) return -EBADMSG;
#endif
 nx30_d_fw_validate_stage=15;
 if (!fit_all_image_verify(scratch)) return -EBADMSG;
 if (!fit_image_verify(scratch,kernel)) return -EBADMSG;
 nx30_d_fw_validate_stage=16;
 if (!fit_image_verify(scratch,fdt_node)) return -EBADMSG;
 nx30_d_fw_validate_stage=17;
 ret=fit_image_get_comp(scratch,fdt_node,&fdt_comp);
 if (ret || fdt_comp!=IH_COMP_NONE) return -EPERM;
 nx30_d_fw_validate_stage=18;
 ret=fit_image_get_data(scratch,fdt_node,&fdt_data,&fdt_size);
 if (ret || !fdt_data || fdt_size<sizeof(struct fdt_header) || fdt_check_header(fdt_data))
  return -EBADMSG;
 nx30_d_fw_validate_stage=19;
 if (fdt_totalsize(fdt_data)>fdt_size || fdt_check_full(fdt_data,fdt_size))
  return -EBADMSG;
 nx30_d_fw_validate_stage=20;
 compat=fdt_getprop(fdt_data,0,"compatible",&len);
 if (!compat || len<=0 ||
     fdt_node_check_compatible(fdt_data,0,"qcom,ipq5018")) return -EPERM;
 return 0;
}
static int nx30_d_validate(const void *input,size_t length,
                           struct nx30_d_layout *layout,
                           struct nx30_d_slots *slots,
                           struct nx30_d_ubi_map *map,
                           const char **release,u32 *image_crc)
{
 struct mtd_info *mtd=nx30_d_nand(); u8 *scratch=(u8 *)(uintptr_t)NX30_D_FIT_SCRATCH;
 int ret;
 nx30_d_fw_validate_stage=1;
 if (!nx30_d_input_range(input,length) || length%NX30_D_UBI_PEB_SIZE) return -EINVAL;
 nx30_d_fw_validate_stage=2;
 if (!is_memory_region_available(NX30_D_FIT_SCRATCH,NX30_D_SCRATCH_SIZE)) return -ENOMEM;
 nx30_d_fw_validate_stage=3;
 ret=nx30_d_layout_get(layout); if (ret) return ret;
 nx30_d_fw_validate_stage=4;
 ret=nx30_d_slots_get(slots); if (ret) return ret;
 if (slots->generation!=layout->generation || slots->ecc_bits!=4) return -ESTALE;
 nx30_d_fw_validate_stage=5;
 ret=nx30_d_mtd_ready(mtd); if (ret) return ret;
 nx30_d_fw_validate_stage=6;
 ret=nx30_d_ubi_validate(input,length,nx30_d_ubi_crc,map); if (ret) return ret;
 nx30_d_fw_validate_stage=7;
 if (map->peb_count>layout->slot_a.size/mtd->erasesize ||
     map->reserved_pebs+24>layout->slot_a.size/mtd->erasesize ||
     layout->slot_a.size!=NX30_D_SLOT_BYTES || layout->slot_b.size!=NX30_D_SLOT_BYTES)
  return -EFBIG;
 ret=nx30_d_fit_check(input,map,scratch,release); if (ret) return ret;
 *image_crc=crc32(0,input,length); return 0;
}
int nx30_d_firmware_validate(const void *image,size_t length,
                             struct nx30_d_firmware_info *info)
{
 struct nx30_d_layout layout; struct nx30_d_slots slots; struct nx30_d_ubi_map map;
 const char *release=NULL; u32 crc; int ret;
 u8 digest[SHA256_SUM_LEN];
 memset(&nx30_d_fw_pending,0,sizeof(nx30_d_fw_pending));
 if (!info) {
  return -EINVAL;
 }
 memset(info,0,sizeof(*info));
 ret=nx30_d_validate(image,length,&layout,&slots,&map,&release,&crc);
 if (ret) { printf("NX30_D_FW_VALIDATE_FAIL stage=%u ret=%d\n",nx30_d_fw_validate_stage,ret); return ret; }
 sha256_csum_wd(image,length,digest,CHUNKSZ_SHA256);
 info->generation=layout.generation; info->image_crc32=crc; info->image_bytes=length;
 info->peb_count=map.peb_count; info->target=slots.inactive;
 strlcpy(info->release,release,sizeof(info->release));
 nx30_d_fw_pending.generation=layout.generation; nx30_d_fw_pending.image_crc32=crc;
 nx30_d_fw_pending.image_bytes=length; nx30_d_fw_pending.target=slots.inactive;
 memcpy(nx30_d_fw_pending.sha256,digest,sizeof(digest)); nx30_d_fw_pending.valid=1;
 return 0;
}

struct nx30_d_fw_mtd_ctx { struct mtd_info *mtd; };
static int nx30_fw_is_bad(void *cookie,u64 offset)
{
 struct nx30_d_fw_mtd_ctx *c=cookie; return nand_block_isbad(c->mtd,offset);
}
static int nx30_fw_window_open(void *cookie,u64 offset,u64 size)
{
 struct nx30_d_fw_mtd_ctx *c=cookie; return nx30_d_qpic_write_window_open(c->mtd,offset,size,4);
}
static int nx30_fw_erase(void *cookie,u64 offset,u32 size)
{
 struct nx30_d_fw_mtd_ctx *c=cookie; struct erase_info e;
 memset(&e,0,sizeof(e)); e.mtd=c->mtd; e.addr=offset; e.len=size; return mtd_erase(c->mtd,&e);
}
static int nx30_fw_write(void *cookie,u64 offset,const u8 *data,u32 size)
{
 struct nx30_d_fw_mtd_ctx *c=cookie; size_t done=0;
 int ret=mtd_write(c->mtd,offset,size,&done,data);
 if (ret) {
  return ret;
 }
 return done==size?0:-EIO;
}
static int nx30_fw_read_window(void *cookie,u64 offset,size_t length,
                               size_t *read_length,u8 *data)
{
 struct nx30_d_fw_mtd_ctx *c=cookie;
 return mtd_read(c->mtd,offset,length,read_length,data);
}
static int nx30_fw_read(void *cookie,u64 offset,u8 *data,u32 size)
{
 struct nx30_d_fw_mtd_ctx *c=cookie;
 return nx30_d_read_extent(nx30_fw_read_window,c->mtd,offset,size,data);
}
static void nx30_fw_window_close(void *cookie)
{ (void)cookie; nx30_d_qpic_write_window_close(); }
static int nx30_fw_close_status(void *cookie)
{ (void)cookie; return nx30_d_qpic_write_window_close_error(); }

int nx30_d_firmware_write_inactive(const void *image,size_t length,
                                   u32 expected_generation,u32 expected_crc32,
                                   struct nx30_d_result *result)
{
 struct nx30_d_layout layout; struct nx30_d_slots slots; struct nx30_d_ubi_map map;
 struct nx30_d_fw_mtd_ctx ctx; struct nx30_d_fw_txn_io io;
 struct mtd_info *mtd=nx30_d_nand(); const char *release=NULL;
 struct nx30_d_fw_pending pending; u8 digest[SHA256_SUM_LEN];
 u8 *verify=NULL,*writebuf=NULL; u32 crc,verified=0,stage=NX30_D_STAGE_PREFLIGHT;
 u64 target=0; int ret;
 if (result) { memset(result,0,sizeof(*result)); result->stage=NX30_D_STAGE_PREFLIGHT; }
 if (!nx30_d_fw_pending.valid) { ret=-ESTALE; goto out; }
 pending=nx30_d_fw_pending; memset(&nx30_d_fw_pending,0,sizeof(nx30_d_fw_pending));
 if (pending.generation!=expected_generation || pending.image_crc32!=expected_crc32 ||
     pending.image_bytes!=length || !nx30_d_input_range(image,length)) { ret=-ESTALE; goto out; }
 sha256_csum_wd(image,length,digest,CHUNKSZ_SHA256);
 if (memcmp(digest,pending.sha256,sizeof(digest))) { ret=-ESTALE; goto out; }
 ret=nx30_d_validate(image,length,&layout,&slots,&map,&release,&crc); if (ret) goto out;
 if (layout.generation!=pending.generation || crc!=pending.image_crc32 ||
     slots.inactive!=pending.target || layout.generation!=expected_generation || crc!=expected_crc32) { ret=-ESTALE; goto out; }
 target=slots.inactive==NX30_D_SLOT_A?layout.slot_a.offset:layout.slot_b.offset;
 if (result) { result->generation=layout.generation; result->target_offset=target; result->target_size=NX30_D_SLOT_BYTES; result->selector=slots.selector; result->age=slots.age; result->crc_before=crc; }
 if (target==(slots.active==NX30_D_SLOT_A?layout.slot_a.offset:layout.slot_b.offset) ||
     (slots.inactive==NX30_D_SLOT_A?layout.slot_a.size:layout.slot_b.size)!=NX30_D_SLOT_BYTES) { ret=-EPERM; goto out; }
 verify=memalign(ARCH_DMA_MINALIGN,mtd->erasesize); writebuf=memalign(ARCH_DMA_MINALIGN,mtd->erasesize);
 if (!verify || !writebuf) { ret=-ENOMEM; goto out; }
 ctx.mtd=mtd; io.ctx=&ctx; io.is_bad=nx30_fw_is_bad; io.window_open=nx30_fw_window_open;
 io.erase=nx30_fw_erase; io.write=nx30_fw_write; io.read=nx30_fw_read;
 io.window_close=nx30_fw_window_close; io.close_status=nx30_fw_close_status;
 ret=nx30_d_fw_txn_run(&io,target,NX30_D_SLOT_BYTES,image,length,mtd->erasesize,
                       writebuf,verify,&verified,&stage);
out:
 if (result) {
  result->status=ret; result->stage=stage; result->bytes_verified=verified;
  if (!ret) { result->stage=NX30_D_STAGE_COMPLETE; result->generation=layout.generation;
   result->crc_before=crc; result->crc_after=crc; result->target_offset=target;
   result->target_size=NX30_D_SLOT_BYTES; result->selector=slots.selector; result->age=slots.age; }
 }
 free(verify); free(writebuf); return ret;
}
