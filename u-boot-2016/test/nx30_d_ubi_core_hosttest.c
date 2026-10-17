#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <nx30_d_ubi_core.h>
#include <drivers/mtd/ubi/ubi-media.h>
#include <u-boot/crc.h>
#define PEB NX30_D_UBI_PEB_SIZE
#define VOFF NX30_D_UBI_VID_OFFSET
#define DOFF NX30_D_UBI_DATA_OFFSET
#define LEB NX30_D_UBI_LEB_SIZE

static void be32(unsigned char *p,u32 v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static void be16(unsigned char *p,u16 v) { p[0]=v>>8;p[1]=v; }
static void le16(unsigned char *p,u16 v) { p[0]=v;p[1]=v>>8; }
static void le64(unsigned char *p,u64 v) { int i;for(i=0;i<8;i++)p[i]=v>>(8*i); }
static void set_crc(unsigned char *p) { be32(p+60,crc32_no_comp(UBI_CRC32_INIT,p,60)); }
static void ec(unsigned char *p)
{
 be32(p,UBI_EC_HDR_MAGIC);p[4]=UBI_VERSION;be32(p+16,VOFF);be32(p+20,DOFF);be32(p+24,0x12345678);set_crc(p);
}
static void vid(unsigned char *p,u32 id,u32 lnum,u32 type,u32 bytes,const unsigned char *data)
{
 be32(p,UBI_VID_HDR_MAGIC);p[4]=UBI_VERSION;p[5]=type;p[6]=0;p[7]=0;
 be32(p+8,id);be32(p+12,lnum);be32(p+20,bytes);be32(p+24,type==UBI_VID_STATIC?1:0);be32(p+28,0);
 be32(p+32,bytes?crc32_no_comp(UBI_CRC32_INIT,data,bytes):0);set_crc(p);
}
static void vtbl_record(unsigned char *p,u32 id,const char *name,u32 type)
{
 u16 n=strlen(name);be32(p,id?1:1);be32(p+4,LEB);be32(p+8,0);p[12]=type;p[13]=0;be16(p+14,n);memcpy(p+16,name,n);p[16+n]=0;be32(p+168,crc32_no_comp(UBI_CRC32_INIT,p,168));
}
static void move_kernel_rootfs_to_high_ids(unsigned char *im)
{
 unsigned char saved_kernel[172],saved_rootfs[172];unsigned int copy;
 for(copy=0;copy<2;copy++) {
  unsigned char *table=im+copy*PEB+DOFF;
  memcpy(saved_kernel,table,172);memcpy(saved_rootfs,table+3*172,172);
  memset(table,0,172);be32(table+168,crc32_no_comp(UBI_CRC32_INIT,table,168));
  memset(table+3*172,0,172);be32(table+3*172+168,crc32_no_comp(UBI_CRC32_INIT,table+3*172,168));
  memcpy(table+126*172,saved_kernel,172);
  memcpy(table+127*172,saved_rootfs,172);
 }
 be32(im+2*PEB+VOFF+8,126);set_crc(im+2*PEB+VOFF);
 be32(im+5*PEB+VOFF+8,127);set_crc(im+5*PEB+VOFF);
}

static unsigned char *make_image(u32 *size_out)
{
 const char *names[]={"kernel","wifi_fw","bt_fw","ubi_rootfs","rootfs_data"};
 unsigned char *im=malloc(7U*PEB);unsigned int i;u32 bytes=128;
 assert(im);memset(im,0xff,7U*PEB);
 for(i=0;i<7;i++) ec(im+i*PEB);
 /* Two redundant UBI volume tables. */
 for(i=0;i<2;i++) {
  unsigned char *table=im+i*PEB+DOFF;unsigned int v;
  memset(table,0,128U*172U);
  for(v=0;v<128;v++) be32(table+v*172+168,crc32_no_comp(UBI_CRC32_INIT,table+v*172,168));
  for(v=0;v<5;v++) vtbl_record(table+v*172,v,names[v],v>=3?UBI_VID_DYNAMIC:UBI_VID_STATIC);
  vid(im+i*PEB+VOFF,0x7fffffffU-4096U,i,UBI_VID_DYNAMIC,0,NULL);
  im[i*PEB+VOFF+7]=UBI_COMPAT_REJECT;set_crc(im+i*PEB+VOFF);
 }
 /* A bounded synthetic FIT header and a SquashFS superblock for parser tests. */
 be32(im+2*PEB+DOFF,0xd00dfeed);be32(im+2*PEB+DOFF+4,bytes);
 vid(im+2*PEB+VOFF,0,0,UBI_VID_STATIC,bytes,im+2*PEB+DOFF);
 memset(im+3*PEB+DOFF,0x11,bytes);vid(im+3*PEB+VOFF,1,0,UBI_VID_STATIC,bytes,im+3*PEB+DOFF);
 memset(im+4*PEB+DOFF,0x22,bytes);vid(im+4*PEB+VOFF,2,0,UBI_VID_STATIC,bytes,im+4*PEB+DOFF);
 memset(im+5*PEB+DOFF,0,bytes);memcpy(im+5*PEB+DOFF,"hsqs",4);le16(im+5*PEB+DOFF+28,4);le64(im+5*PEB+DOFF+40,bytes);
 vid(im+5*PEB+VOFF,3,0,UBI_VID_DYNAMIC,0,NULL);
 /* Recompute EC CRC after per-PEB initialization; table LEB CRCs are VID-ignored. */
 for(i=0;i<7;i++) set_crc(im+i*PEB);
 *size_out=7U*PEB;return im;
}
int main(void)
{
 struct nx30_d_ubi_map map;u32 sz;unsigned char *im=make_image(&sz);int ret;
 ret=nx30_d_ubi_validate(im,sz,crc32_no_comp,&map);
 if (ret) { fprintf(stderr,"ubi parser ret=%d\n",ret); }
 assert(ret==0);assert(map.peb_count==7);assert(map.volume_count==5);
 assert(map.kernel_id==0 && map.rootfs_id==3 && map.kernel_bytes==128 && map.rootfs_bytes==128);
 assert(map.kernel_peb[0]==2 && map.rootfs_peb[0]==5);
 /* High legal IDs must still map to the two compact output arrays. */
 { unsigned char *high=malloc(sz);assert(high);memcpy(high,im,sz);
   move_kernel_rootfs_to_high_ids(high);
   ret=nx30_d_ubi_validate(high,sz,crc32_no_comp,&map);
   if(ret)fprintf(stderr,"high volume ID UBI validation ret=%d\n",ret);
   assert(ret==0);assert(map.volume_count==5);
   assert(map.kernel_id==126 && map.rootfs_id==127);
   assert(map.kernel_peb[0]==2 && map.rootfs_peb[0]==5);free(high); }
 /* Reject changed static volume payload because VID data CRC no longer matches. */
 im[3*PEB+DOFF+5]^=1;assert(nx30_d_ubi_validate(im,sz,crc32_no_comp,&map)==-EBADMSG);
 im[3*PEB+DOFF+5]^=1;
 /* Reject a bad EC header and an image beyond the fixed NX30 slot. */
 im[0*PEB+16]^=1;assert(nx30_d_ubi_validate(im,sz,crc32_no_comp,&map)==-EBADMSG);
 assert(nx30_d_ubi_validate(im,NX30_D_UBI_IMAGE_SIZE+PEB,crc32_no_comp,&map)==-EINVAL);
 free(im);return 0;
}
