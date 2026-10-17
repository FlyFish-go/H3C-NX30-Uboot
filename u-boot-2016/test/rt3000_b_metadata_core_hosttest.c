/* SPDX-License-Identifier: GPL-2.0+ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt3000_b_metadata_core.h"
#include <u-boot/sha256.h>
static unsigned int le32(const unsigned char*p){return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);}
static void put16(unsigned char*p,unsigned int x){p[0]=x;p[1]=x>>8;}
static void put32(unsigned char*p,unsigned int x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c);return 1;}}while(0)
static unsigned char*read_file(const char*p,unsigned int*n){FILE*f=fopen(p,"rb");long z;unsigned char*d;if(!f)return NULL;if(fseek(f,0,SEEK_END)||(z=ftell(f))<=0||z>0xffffffffL||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}d=malloc((size_t)z);if(!d||fread(d,1,(size_t)z,f)!=(size_t)z){free(d);fclose(f);return NULL;}fclose(f);*n=(unsigned int)z;return d;}
static void rehash_pack(unsigned char*d,unsigned int n){unsigned char h[SHA256_SUM_LEN];unsigned int load=le32(d+52+2*32+16);sha256_csum_wd(d,0xb4,h,CHUNKSZ_SHA256);memcpy(d+0x1000+40,h,32);sha256_csum_wd(d+0x3000,load,h,CHUNKSZ_SHA256);memcpy(d+0x1000+40+2*32,h,32);(void)n;}
static void strip_rt3000_markers(unsigned char*d,unsigned int n){unsigned int i;const char*marks[]={"H3C RT3000-B","h3c,rt3000-b","RT3000-B"};const unsigned int lens[]={12,13,8};for(i=0;i<3;i++){unsigned int j;for(j=0x3000;j+lens[i]<=n;j++){unsigned int k;for(k=0;k<lens[i]&&d[j+k]==(unsigned char)marks[i][k];k++);if(k==lens[i])memset(d+j,'X',lens[i]);}}}
int main(int argc,char**argv){
 unsigned int n,load_size,raw_size,out_size,i,stage_size,known_n,normalized_size;unsigned char*oem,*raw,*out,*bad,*art,*cdt,*stage,*stage_out,*known,*known_out,*unknown,*normalized;
 unsigned char source[64],owned[64],expected[64],actual[32];
 CHECK(argc>=4);oem=read_file(argv[1],&n);CHECK(oem);
 if(rt3_md_validate_image(RT3_MD_UBOOT,oem,n)!=0){
  normalized=malloc(RT3_MD_APPSBL_SIZE);CHECK(normalized);
  CHECK(rt3_md_prepare_elf(oem,n,normalized,RT3_MD_APPSBL_SIZE,&normalized_size)==0);
  free(oem);oem=normalized;n=normalized_size;
 }
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,oem,n)==0);
 CHECK(!strcmp(rt3_md_uboot_image_name(oem,n),"H3C HW-B U-Boot ELF"));
 known=read_file(argv[3],&known_n);CHECK(known);CHECK(known_n==473088U);
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,known,known_n)==0);
 CHECK(!strcmp(rt3_md_uboot_image_name(known,known_n),"NX30 R012 OEM U-Boot (user-provided test image)"));
 known_out=malloc(RT3_MD_APPSBL_SIZE);CHECK(known_out);
 CHECK(rt3_md_prepare_elf(known,known_n,known_out,RT3_MD_APPSBL_SIZE,&out_size)==0);
 CHECK(out_size==known_n&&!memcmp(known_out,known,known_n));
 CHECK(rt3_md_prepare_elf(known,known_n,known_out,known_n-1,&out_size)!=0);

 load_size=le32(oem+52+2*32+16);CHECK(n==0x3000+load_size);
 raw_size=0x80+load_size;raw=calloc(1,raw_size);out=malloc(RT3_MD_APPSBL_SIZE);CHECK(raw&&out);
 memcpy(raw,oem,52);put32(raw+28,52);put32(raw+32,0);put16(raw+40,52);put16(raw+42,32);put16(raw+44,1);put16(raw+46,0);put16(raw+48,0);put16(raw+50,0);
 put32(raw+52,1);put32(raw+56,0x80);put32(raw+60,0x4a920000);put32(raw+64,0x4a920000);put32(raw+68,load_size);put32(raw+72,load_size);put32(raw+76,7);put32(raw+80,0x20);
 memcpy(raw+0x80,oem+0x3000,load_size);
 CHECK(rt3_md_prepare_elf(raw,raw_size,out,RT3_MD_APPSBL_SIZE,&out_size)==0);
 CHECK(rt3_md_uboot_image_name(raw,raw_size)==NULL);
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,out,out_size)==0);
 unknown=malloc(out_size);CHECK(unknown);memcpy(unknown,out,out_size);
 strip_rt3000_markers(unknown,out_size);rehash_pack(unknown,out_size);
 /* Legacy board strings are not identity; the validated embedded DTB is. */
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,unknown,out_size)==0);
 CHECK(!strcmp(rt3_md_uboot_image_name(unknown,out_size),"H3C HW-B U-Boot ELF"));
 free(unknown);
 if(out_size!=n||memcmp(out,oem,n)){for(i=0;i<(out_size<n?out_size:n);i++)if(out[i]!=oem[i]){fprintf(stderr,"first diff %x got %02x expected %02x\n",i,out[i],oem[i]);break;}fprintf(stderr,"sizes got %x expected %x\n",out_size,n);return 1;} /* exact reviewed-package reproduction */
 if(argc>=3){stage=read_file(argv[2],&stage_size);stage_out=malloc(RT3_MD_APPSBL_SIZE);CHECK(stage&&stage_out);CHECK(rt3_md_prepare_elf(stage,stage_size,stage_out,RT3_MD_APPSBL_SIZE,&out_size)==0);CHECK(rt3_md_validate_image(RT3_MD_UBOOT,stage_out,out_size)==0);free(stage);free(stage_out);}
 bad=malloc(known_n+1);CHECK(bad);
 memcpy(bad,known,known_n);bad[0x1000]^=1;
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,bad,known_n)!=0);
 CHECK(rt3_md_prepare_elf(bad,known_n,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 memcpy(bad,known,known_n);bad[0x3000]^=1;
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,bad,known_n)!=0);
 CHECK(rt3_md_prepare_elf(bad,known_n,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 memcpy(bad,known,known_n);bad[known_n-1]^=1;
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,bad,known_n)!=0);
 CHECK(rt3_md_prepare_elf(bad,known_n,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,known,known_n-1)!=0);
 memcpy(bad,known,known_n);bad[known_n]=0;
 CHECK(rt3_md_validate_image(RT3_MD_UBOOT,bad,known_n+1)!=0);
 CHECK(rt3_md_prepare_elf(known,known_n-1,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 CHECK(rt3_md_prepare_elf(bad,known_n+1,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 free(bad);bad=malloc(n);CHECK(bad);memcpy(bad,oem,n);bad[0x1068]^=1;CHECK(rt3_md_prepare_elf(bad,n,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);memcpy(bad,oem,n);bad[0x3100]^=1;CHECK(rt3_md_prepare_elf(bad,n,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 CHECK(rt3_md_prepare_elf(raw,raw_size-1,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 put32(raw+52+8,0x4a921000);CHECK(rt3_md_prepare_elf(raw,raw_size,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);put32(raw+52+8,0x4a920000);
 put16(raw+44,2);put32(raw+84,1);put32(raw+88,0x80);put32(raw+92,0x4a920000);put32(raw+96,0x4a920000);put32(raw+100,load_size);put32(raw+104,load_size);put32(raw+108,7);put32(raw+112,0x20);CHECK(rt3_md_prepare_elf(raw,raw_size,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);put16(raw+44,1);
 raw[18]=0;CHECK(rt3_md_prepare_elf(raw,raw_size,out,RT3_MD_APPSBL_SIZE,&out_size)!=0);
 art=malloc(RT3_MD_ART_SIZE);cdt=malloc(RT3_MD_CDT_SIZE);CHECK(art&&cdt);
 memset(art,0xa5,RT3_MD_ART_SIZE);CHECK(rt3_md_validate_image(RT3_MD_ART,art,RT3_MD_ART_SIZE)==0);CHECK(rt3_md_validate_image(RT3_MD_ART,art,RT3_MD_ART_SIZE-1)!=0);
 memset(cdt,0xff,RT3_MD_CDT_SIZE);memcpy(cdt,"CDT",3);cdt[3]=0;cdt[4]=2;cdt[5]=0;cdt[14]=26;cdt[15]=0;cdt[16]=5;cdt[17]=0;cdt[18]=31;cdt[19]=0;cdt[20]=0;cdt[21]=2;cdt[22]=0x1f;cdt[23]=2;cdt[24]=4;cdt[25]=0;memcpy(cdt+26,"\002\010\004\000\000",5);
 CHECK(rt3_md_validate_image(RT3_MD_CDT,cdt,RT3_MD_CDT_SIZE)==0);cdt[26]^=1;CHECK(rt3_md_validate_image(RT3_MD_CDT,cdt,RT3_MD_CDT_SIZE)!=0);cdt[26]^=1;cdt[547]=0x7e;CHECK(rt3_md_validate_image(RT3_MD_CDT,cdt,RT3_MD_CDT_SIZE)!=0);
 for(i=0;i<sizeof(source);i++){source[i]=i;owned[i]=0;expected[i]=i;}for(i=0;i<sizeof(actual);i++)actual[i]=i;
 CHECK(rt3_md_snapshot_copy(owned,source,sizeof(source))==0);memset(source,0xee,sizeof(source));CHECK(!memcmp(owned,expected,sizeof(owned)));CHECK(rt3_md_hash_matches(expected,actual,sizeof(actual)));owned[7]^=1;memcpy(actual,owned,sizeof(actual));CHECK(!rt3_md_hash_matches(expected,actual,sizeof(actual)));CHECK(rt3_md_snapshot_copy(NULL,source,sizeof(source))!=0);
 free(oem);free(known);free(known_out);free(raw);free(out);free(bad);free(art);free(cdt);puts("RT3000-B metadata normalizer/core host tests: PASS");return 0;
}
