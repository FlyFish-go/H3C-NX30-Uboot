// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#ifdef __UBOOT__
#include <asm/errno.h>
#else
#include <errno.h>
#endif
#include <asm/unaligned.h>
#include <nx30_d_ubi_core.h>
#ifdef __UBOOT__
#include "../drivers/mtd/ubi/ubi-media.h"
#else
#include <drivers/mtd/ubi/ubi-media.h>
#endif

#define NX30_D_VTBL_REC_SIZE 172U
#define NX30_D_VTBL_SIZE (128U * NX30_D_VTBL_REC_SIZE)
#define NX30_D_LAYOUT_ID (0x7fffffffU - 4096U)
#define NX30_D_UBI_USED_STATIC 0U
#define NX30_D_UBI_USED_DYNAMIC 1U

static u16 nx30_get_be16(const u8 *p) { return ((u16)p[0] << 8) | (u16)p[1]; }
static u16 nx30_get_le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u64 nx30_get_le64(const u8 *p)
{
 return (u64)p[0] | ((u64)p[1]<<8) | ((u64)p[2]<<16) | ((u64)p[3]<<24) |
        ((u64)p[4]<<32) | ((u64)p[5]<<40) | ((u64)p[6]<<48) | ((u64)p[7]<<56);
}
static int nx30_is_ff(const u8 *p, u32 n)
{
 u32 i; for (i=0;i<n;i++) if (p[i]!=0xff) return 0; return 1;
}
static int nx30_is_zero(const u8 *p,u32 n)
{
 u32 i; for (i=0;i<n;i++) if (p[i]) return 0; return 1;
}
static int nx30_crc_ok(nx30_d_ubi_crc_fn f, const u8 *p, u32 n, const u8 *crc)
{
 return f && get_unaligned_be32(crc)==f(UBI_CRC32_INIT,p,n);
}
static int nx30_name_eq(const char *a, const char *b)
{
 return !strcmp(a,b);
}

/* Strict NX30 raw NAND UBI parser: exact 320 PEB image and five OEM volumes.
 * It validates every EC/VID header, both volume tables, all static volume data
 * CRCs, unique mappings, fixed volume names and kernel/rootfs logical extents.
 */
int nx30_d_ubi_validate(const unsigned char *image, u32 size,
                        nx30_d_ubi_crc_fn crc_fn,
                        struct nx30_d_ubi_map *map)
{
 /* Input-backed views are used only during this synchronous validation call. */
 const u8 *vtbl[2]={NULL,NULL};
 static u8 seen[NX30_D_UBI_MAX_VOLUMES];
 static u8 type[NX30_D_UBI_MAX_VOLUMES];
 static u8 used_set[NX30_D_UBI_MAX_VOLUMES];
 static u32 reserved[NX30_D_UBI_MAX_VOLUMES];
 static u32 used[NX30_D_UBI_MAX_VOLUMES];
 static u32 datapad[NX30_D_UBI_MAX_VOLUMES];
 static u8 lseen[NX30_D_UBI_MAX_VOLUMES][(NX30_D_UBI_PEB_COUNT+7)/8];
 static u16 pebmap[2][NX30_D_UBI_PEB_COUNT];
 static char names[NX30_D_UBI_MAX_VOLUMES][32];
 static const char *const required[]={"kernel","wifi_fw","bt_fw","ubi_rootfs","rootfs_data"};
 u32 i,j,copies=0,table_seen=0,image_seq=0,total_reserved=0;
 u32 kernel_id=0xffffffffU,rootfs_id=0xffffffffU,kernel_used=0,rootfs_used=0;
 u32 kernel_bytes=0,rootfs_bytes=0,kernel_lebs=0,rootfs_lebs=0;
 int seq_set=0;
 if (!image || !map || !crc_fn || !size || size>NX30_D_UBI_IMAGE_SIZE ||
     size%NX30_D_UBI_PEB_SIZE) return -EINVAL;
 memset(seen,0,sizeof(seen));
 memset(type,0,sizeof(type)); memset(used_set,0,sizeof(used_set));
 memset(reserved,0,sizeof(reserved)); memset(used,0,sizeof(used));
 memset(datapad,0,sizeof(datapad)); memset(lseen,0,sizeof(lseen));
 memset(pebmap,0xff,sizeof(pebmap)); memset(names,0,sizeof(names));
 memset(map,0,sizeof(*map));
 for (i=0;i<size/NX30_D_UBI_PEB_SIZE;i++) {
  const u8 *p=image+(size_t)i*NX30_D_UBI_PEB_SIZE;
  const u8 *ec=p, *vid=p+NX30_D_UBI_VID_OFFSET;
  u32 vol,lnum,ds,dp;
  if (get_unaligned_be32(ec)!=UBI_EC_HDR_MAGIC || ec[4]!=UBI_VERSION ||
      get_unaligned_be32(ec+16)!=NX30_D_UBI_VID_OFFSET ||
      get_unaligned_be32(ec+20)!=NX30_D_UBI_DATA_OFFSET ||
      !nx30_crc_ok(crc_fn,ec,60,ec+60)) return -EBADMSG;
  if (!seq_set) { image_seq=get_unaligned_be32(ec+24); seq_set=1; }
  else if (image_seq!=get_unaligned_be32(ec+24)) return -EINVAL;
  if (nx30_is_ff(vid,UBI_VID_HDR_SIZE)) continue;
  if (get_unaligned_be32(vid)!=UBI_VID_HDR_MAGIC || vid[4]!=UBI_VERSION ||
      !nx30_crc_ok(crc_fn,vid,60,vid+60)) return -EBADMSG;
  vol=get_unaligned_be32(vid+8); lnum=get_unaligned_be32(vid+12);
  ds=get_unaligned_be32(vid+20); dp=get_unaligned_be32(vid+28);
  if (dp>NX30_D_UBI_LEB_SIZE || ds>NX30_D_UBI_LEB_SIZE-dp) return -EINVAL;
  if (vol==NX30_D_LAYOUT_ID) {
   if (lnum>1 || copies>=2 || (table_seen&(1U<<lnum)) || dp ||
       vid[5]!=UBI_VID_DYNAMIC || vid[7]!=UBI_COMPAT_REJECT ||
       NX30_D_VTBL_SIZE>NX30_D_UBI_LEB_SIZE)
    return -EINVAL;
   vtbl[lnum]=p+NX30_D_UBI_DATA_OFFSET;
   table_seen|=1U<<lnum; copies++; continue;
  }
  if (vol>=NX30_D_UBI_MAX_VOLUMES || lnum>=NX30_D_UBI_PEB_COUNT) return -EINVAL;
 }
 if (copies!=2 || table_seen!=3 || !vtbl[0] || !vtbl[1] ||
     memcmp(vtbl[0],vtbl[1],NX30_D_VTBL_SIZE)) return -EBADMSG;
 for (i=0;i<NX30_D_UBI_MAX_VOLUMES;i++) {
  const u8 *r=vtbl[0]+(size_t)i*NX30_D_VTBL_REC_SIZE;
  u32 res=get_unaligned_be32(r), align=get_unaligned_be32(r+4), dp=get_unaligned_be32(r+8);
  u16 nl=nx30_get_be16(r+14);
  if (!res) {
   if (!nx30_is_ff(r,NX30_D_VTBL_REC_SIZE) &&
       (!nx30_is_zero(r,NX30_D_VTBL_REC_SIZE-4) ||
        !nx30_crc_ok(crc_fn,r,NX30_D_VTBL_REC_SIZE-4,r+NX30_D_VTBL_REC_SIZE-4)))
    return -EBADMSG;
   continue;
  }
  if (!nx30_crc_ok(crc_fn,r,NX30_D_VTBL_REC_SIZE-4,r+NX30_D_VTBL_REC_SIZE-4) ||
      !align || align>NX30_D_UBI_LEB_SIZE || !nl || nl>UBI_VOL_NAME_MAX || nl>=sizeof(names[0]) ||
      r[16+nl]!='\0' || r[12]<UBI_VID_DYNAMIC || r[12]>UBI_VID_STATIC ||
      r[13]>1 || dp!=NX30_D_UBI_LEB_SIZE%align || res>NX30_D_UBI_PEB_COUNT-total_reserved)
   return -EBADMSG;
  seen[i]=1; reserved[i]=res; type[i]=r[12]; datapad[i]=dp;
  memcpy(names[i],r+16,nl); names[i][nl]='\0'; total_reserved+=res;
  for (j=0;j<i;j++) if (seen[j] && !strcmp(names[j],names[i])) return -EINVAL;
  if (nx30_name_eq(names[i],"kernel")) kernel_id=i;
  if (nx30_name_eq(names[i],"ubi_rootfs")) rootfs_id=i;
  if (map->volume_count>=ARRAY_SIZE(map->volumes)) return -EOVERFLOW;
  map->volumes[map->volume_count].id=i;
  map->volumes[map->volume_count].reserved_pebs=res;
  map->volumes[map->volume_count].type=type[i];
  map->volumes[map->volume_count].name_len=nl;
  strcpy(map->volumes[map->volume_count].name,names[i]);
  map->volume_count++;
 }
 if (map->volume_count!=ARRAY_SIZE(required) || kernel_id==0xffffffffU || rootfs_id==0xffffffffU)
  return -EINVAL;
 for (i=0;i<ARRAY_SIZE(required);i++) {
  u32 found=0;
  for (j=0;j<NX30_D_UBI_MAX_VOLUMES;j++)
   if (seen[j] && nx30_name_eq(names[j],required[i])) found++;
  if (found!=1) return -EINVAL;
  for (j=0;j<NX30_D_UBI_MAX_VOLUMES;j++) if (seen[j] && nx30_name_eq(names[j],required[i])) {
   if ((i==4 && type[j]!=UBI_VID_DYNAMIC) ||
       (i<3 && type[j]!=UBI_VID_STATIC) ||
       (i==3 && type[j]!=UBI_VID_DYNAMIC && type[j]!=UBI_VID_STATIC)) return -EINVAL;
  }
 }
 for (i=0;i<size/NX30_D_UBI_PEB_SIZE;i++) {
  const u8 *p=image+(size_t)i*NX30_D_UBI_PEB_SIZE;
  const u8 *vid=p+NX30_D_UBI_VID_OFFSET;
  u32 vol,lnum,ds,ue,dc;
  u8 bit;
  if (nx30_is_ff(vid,UBI_VID_HDR_SIZE)) continue;
  vol=get_unaligned_be32(vid+8); if (vol==NX30_D_LAYOUT_ID) continue;
  lnum=get_unaligned_be32(vid+12); ds=get_unaligned_be32(vid+20);
  ue=get_unaligned_be32(vid+24); dc=get_unaligned_be32(vid+32);
  bit=(u8)(1U<<(lnum&7));
  if (!seen[vol] || lnum>=reserved[vol] || vid[5]!=type[vol] ||
      get_unaligned_be32(vid+28)!=datapad[vol] || (lseen[vol][lnum>>3]&bit)) return -EINVAL;
  lseen[vol][lnum>>3]|=bit;
  if (vol==kernel_id) pebmap[0][lnum]=(u16)i;
  if (vol==rootfs_id) pebmap[1][lnum]=(u16)i;
  if (type[vol]==UBI_VID_DYNAMIC && !vid[6] && (ds || ue || dc)) return -EINVAL;
  if (type[vol]==UBI_VID_STATIC) {
   if (vid[6] || !ue || ue>reserved[vol] || lnum>=ue || !ds ||
       ds>NX30_D_UBI_LEB_SIZE-datapad[vol]) return -EINVAL;
   if (!used_set[vol]) { used[vol]=ue; used_set[vol]=1; }
   else if (used[vol]!=ue) return -EINVAL;
  }
  if (ds && (vid[6] || type[vol]==UBI_VID_STATIC) &&
      crc_fn(UBI_CRC32_INIT,p+NX30_D_UBI_DATA_OFFSET,ds)!=dc) return -EBADMSG;
  if (!lnum && vol==kernel_id) {
   u32 fitlen;
   if (ds<8 || get_unaligned_be32(p+NX30_D_UBI_DATA_OFFSET)!=0xd00dfeed) return -EINVAL;
   fitlen=get_unaligned_be32(p+NX30_D_UBI_DATA_OFFSET+4);
   if (fitlen<40 || fitlen>(u64)reserved[vol]*NX30_D_UBI_LEB_SIZE) return -EINVAL;
   kernel_bytes=fitlen; kernel_lebs=(fitlen+NX30_D_UBI_LEB_SIZE-1)/NX30_D_UBI_LEB_SIZE;
  }
  if (!lnum && vol==rootfs_id) {
   const u8 *d=p+NX30_D_UBI_DATA_OFFSET; u64 sz;
   if ((type[vol]==UBI_VID_STATIC && ds<48) ||
       memcmp(d,"hsqs",4) || nx30_get_le16(d+28)!=4) return -EINVAL;
   sz=nx30_get_le64(d+40);
   if (sz<96 || sz>(u64)reserved[vol]*NX30_D_UBI_LEB_SIZE) return -EINVAL;
   rootfs_bytes=(u32)sz; rootfs_lebs=(rootfs_bytes+NX30_D_UBI_LEB_SIZE-1)/NX30_D_UBI_LEB_SIZE;
  }
 }
 for (i=0;i<NX30_D_UBI_MAX_VOLUMES;i++) {
  u32 n;
  if (!seen[i] || type[i]!=UBI_VID_STATIC) continue;
  if (!used_set[i]) return -EINVAL;
  for (n=0;n<used[i];n++) if (!(lseen[i][n>>3]&(1U<<(n&7)))) return -EINVAL;
  if (i==kernel_id) kernel_used=used[i];
  if (i==rootfs_id) rootfs_used=used[i];
 }
 if (!kernel_bytes || !rootfs_bytes || !kernel_lebs || !rootfs_lebs ||
     kernel_used<kernel_lebs ||
     (type[rootfs_id]==UBI_VID_STATIC && rootfs_used<rootfs_lebs) ||
     (type[rootfs_id]==UBI_VID_DYNAMIC && rootfs_lebs>reserved[rootfs_id])) return -EINVAL;
 for (i=0;i<kernel_lebs;i++) {
  if (!(lseen[kernel_id][i>>3]&(1U<<(i&7))) || pebmap[0][i]==0xffffU) return -EINVAL;
  map->kernel_peb[i]=pebmap[0][i];
  map->kernel_data_size[i]=get_unaligned_be32(image+(size_t)pebmap[0][i]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_VID_OFFSET+20);
 }
 for (i=0;i<rootfs_lebs;i++) {
  if (!(lseen[rootfs_id][i>>3]&(1U<<(i&7))) || pebmap[1][i]==0xffffU) return -EINVAL;
  map->rootfs_peb[i]=pebmap[1][i];
  map->rootfs_data_size[i]=type[rootfs_id]==UBI_VID_DYNAMIC?NX30_D_UBI_LEB_SIZE:get_unaligned_be32(image+(size_t)pebmap[1][i]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_VID_OFFSET+20);
 }
 map->peb_count=size/NX30_D_UBI_PEB_SIZE; map->image_seq=image_seq; map->reserved_pebs=total_reserved;
 map->kernel_id=kernel_id; map->rootfs_id=rootfs_id;
 map->kernel_bytes=kernel_bytes; map->rootfs_bytes=rootfs_bytes;
 map->kernel_lebs=kernel_lebs; map->rootfs_lebs=rootfs_lebs;
 return 0;
}
