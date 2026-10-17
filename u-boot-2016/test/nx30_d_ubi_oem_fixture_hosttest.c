#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nx30_d_ubi_core.h>
#include <drivers/mtd/ubi/ubi-media.h>
#include <u-boot/crc.h>

static u32 be32(const unsigned char *p)
{ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }

int main(int argc,char **argv)
{
 FILE *f; long n; unsigned char *image; struct nx30_d_ubi_map map; int ret; u32 peb_count,volume_count,kernel_peb,table_peb[2]={0xffffffffU,0xffffffffU},i;
 assert(argc==2);f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));n=ftell(f);assert(n>0);rewind(f);
 image=malloc((size_t)n);assert(image);assert(fread(image,1,(size_t)n,f)==(size_t)n);fclose(f);
 ret=nx30_d_ubi_validate(image,(u32)n,crc32_no_comp,&map);if(ret)fprintf(stderr,"OEM UBI validation failed: %d size=%ld\n",ret,n);assert(ret==0);
 assert(map.volume_count==5&&map.peb_count==(u32)n/NX30_D_UBI_PEB_SIZE);
 peb_count=map.peb_count; volume_count=map.volume_count; kernel_peb=map.kernel_peb[0];
 /* Locate both layout-volume LEB0/1 PEBs for redundant-table negatives. */
 for(i=0;i<peb_count;i++) {
  unsigned char *vid=image+(size_t)i*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_VID_OFFSET;
  if(be32(vid+8)==(0x7fffffffU-4096U) && be32(vid+12)<2)
   table_peb[be32(vid+12)]=i;
 }
 assert(table_peb[0]!=0xffffffffU && table_peb[1]!=0xffffffffU);
 /* Redundant table mismatch is rejected before records are trusted. */
 image[(size_t)table_peb[1]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_DATA_OFFSET+4]^=1;
 assert(nx30_d_ubi_validate(image,(u32)n,crc32_no_comp,&map)==-EBADMSG);
 image[(size_t)table_peb[1]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_DATA_OFFSET+4]^=1;
 /* Equal tables with a bad per-record CRC are also rejected. */
 image[(size_t)table_peb[0]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_DATA_OFFSET+4]^=1;
 image[(size_t)table_peb[1]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_DATA_OFFSET+4]^=1;
 assert(nx30_d_ubi_validate(image,(u32)n,crc32_no_comp,&map)==-EBADMSG);
 image[(size_t)table_peb[0]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_DATA_OFFSET+4]^=1;
 image[(size_t)table_peb[1]*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_DATA_OFFSET+4]^=1;
 /* Corrupt one header from each class and one protected static payload. */
 image[16]^=1;assert(nx30_d_ubi_validate(image,(u32)n,crc32_no_comp,&map)==-EBADMSG);image[16]^=1;
 image[NX30_D_UBI_VID_OFFSET+60]^=1;assert(nx30_d_ubi_validate(image,(u32)n,crc32_no_comp,&map)==-EBADMSG);image[NX30_D_UBI_VID_OFFSET+60]^=1;
 image[(size_t)kernel_peb*NX30_D_UBI_PEB_SIZE+NX30_D_UBI_DATA_OFFSET+128]^=1;
 assert(nx30_d_ubi_validate(image,(u32)n,crc32_no_comp,&map)==-EBADMSG);
 free(image);printf("OEM UBI fixture PASS: bytes=%ld PEBs=%u volumes=%u\n",n,peb_count,volume_count);return 0;
}
