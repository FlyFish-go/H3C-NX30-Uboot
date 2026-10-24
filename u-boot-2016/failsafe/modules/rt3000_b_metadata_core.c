/* SPDX-License-Identifier: GPL-2.0+ */
#include "rt3000_b_metadata_core.h"
#ifdef RT3_METADATA_HOST_TEST
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>
#else
#include <common.h>
#include <malloc.h>
#endif
#include <u-boot/sha256.h>
#define EH 52U
#define PH 32U
#define PT_LOAD 1U
#define ET_EXEC 2U
#define ET_DYN 3U
#define EM_ARM 40U
#define PT_GNU_STACK 0x6474e551U
#if defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(RT3_METADATA_HOST_TEST_A)
#define RT3_METADATA_HW_A 1
#endif
#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(RT3_METADATA_HOST_TEST_C)
#define RT3_METADATA_HW_C 1
#endif
#if defined(CONFIG_H3C_HW_D_PRODUCTION_PORT)
#define RT3_METADATA_HW_D 1
#endif
#define SDRAM_LOAD 0x4a920000U
#define MBN_HASH_OFFSET 0x1000U
#define MBN_HASH_SIZE 0xa8U
#define MBN_LOAD_OFFSET 0x3000U
#define MBN_VERSION 3U
static rt3_md_u16 le16(const rt3_md_u8*p){return (rt3_md_u16)p[0]|((rt3_md_u16)p[1]<<8);}
static rt3_md_u32 le32(const rt3_md_u8*p){return (rt3_md_u32)p[0]|((rt3_md_u32)p[1]<<8)|((rt3_md_u32)p[2]<<16)|((rt3_md_u32)p[3]<<24);}
static void put16(rt3_md_u8*p,rt3_md_u16 v){p[0]=v;p[1]=v>>8;}
static void put32(rt3_md_u8*p,rt3_md_u32 v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static rt3_md_u32 be32(const rt3_md_u8 *p)
{
 return ((rt3_md_u32)p[0]<<24)|((rt3_md_u32)p[1]<<16)|
        ((rt3_md_u32)p[2]<<8)|p[3];
}
#ifdef RT3_METADATA_HOST_TEST
static int metadata_gunzip(void *dst,int dstlen,rt3_md_u8 *src,
                           unsigned long *lenp)
{
 z_stream stream;int ret;
 if(!dst||!src||!lenp||*lenp>0xffffffffUL)return -1;
 memset(&stream,0,sizeof(stream));stream.next_in=src;stream.avail_in=(uInt)*lenp;
 stream.next_out=dst;stream.avail_out=(uInt)dstlen;
 if(inflateInit2(&stream,MAX_WBITS+16)!=Z_OK)return -1;
 ret=inflate(&stream,Z_FINISH);*lenp=stream.total_out;inflateEnd(&stream);
 return ret==Z_STREAM_END?0:-1;
}
#else
static int metadata_gunzip(void *dst,int dstlen,rt3_md_u8 *src,
                           unsigned long *lenp)
{return gunzip(dst,dstlen,src,lenp);}
#endif
static int fdt_overlap(rt3_md_u32 a,rt3_md_u32 an,rt3_md_u32 b,rt3_md_u32 bn)
{ return an && bn && a < b + bn && b < a + an; }
static int fdt_board(const rt3_md_u8*f,rt3_md_u32 av)
{
 rt3_md_u32 t,os,ot,orv,ss,st,v,lv,p,e,d=0,roots=0,rend;
 int ended=0,board=0,cb=0,model_seen=0,compat_seen=0,machid_seen=0;
 rt3_md_u32 machid=0;
 if(!f||av<40||be32(f)!=0xd00dfeedU)return 0;
 t=be32(f+4);
 if(t<40||t>av)return -1;
 os=be32(f+8);ot=be32(f+12);orv=be32(f+16);
 v=be32(f+20);lv=be32(f+24);ss=be32(f+32);st=be32(f+36);
 if(v<16||v>17||lv<16||lv>v||(os&3)||(orv&7)||(st&3)||
    os<40||ot<40||orv<40||!st||!ss||os>t||st>t-os||
    ot>t||ss>t-ot||orv>t-16)return -1;
 /* The three FDT payload regions are independently bounded and disjoint. */
 if(fdt_overlap(os,st,ot,ss))return -1;
 /* Reserve entries are 64-bit address/size pairs and need an in-bounds terminator. */
 rend=orv;
 for(;;){
  rt3_md_u32 ahi,alo,shi,slo;
  if(rend>t-16)return -1;
  ahi=be32(f+rend);alo=be32(f+rend+4);
  shi=be32(f+rend+8);slo=be32(f+rend+12);
  rend+=16;
  if(!ahi&&!alo&&!shi&&!slo)break;
 }
 if(fdt_overlap(orv,rend-orv,os,st)||fdt_overlap(orv,rend-orv,ot,ss))return -1;
 p=os;e=os+st;
 while(p<e){
  rt3_md_u32 x,nend;
  if(e-p<4)return -1;
  x=be32(f+p);p+=4;
  if(x==1){
   if(ended)return -1;
   for(nend=p;nend<e&&f[nend];nend++);
   if(nend==e||nend>0xfffffff8U)return -1;
   if(!d){if(roots||nend!=p)return -1;roots++;}
   p=(nend+4)&~3U;if(p>e)return -1;d++;
  }else if(x==2){
   if(!d)return -1;
   if(!--d)ended=1;
  }else if(x==3){
   rt3_md_u32 len,no,ne,k,padded;const rt3_md_u8*q;
   if(!d||e-p<8)return -1;
   len=be32(f+p);no=be32(f+p+4);p+=8;
   if(no>=ss||len>e-p||len>0xfffffffcU)return -1;
   for(ne=ot+no;ne<ot+ss&&f[ne];ne++);
   if(ne==ot+ss)return -1;
   q=f+p;padded=(len+3)&~3U;if(padded>e-p)return -1;
   if(d==1){const char*pn=(const char*)(f+ot+no);
    if(!strcmp(pn,"model")){
     if(model_seen++||!len||q[len-1])return -1;
     if(len==sizeof("H3C HW-A")&&!memcmp(q,"H3C HW-A",len))board=1;
     else if(len==sizeof("H3C HW-B")&&!memcmp(q,"H3C HW-B",len))board=2;
     else if(len==sizeof("H3C HW-C")&&!memcmp(q,"H3C HW-C",len))board=4;
#if !defined(RT3_METADATA_HW_A) && !defined(RT3_METADATA_HW_C)
     else if(len==sizeof("H3C RT3000-B stage0")&&!memcmp(q,"H3C RT3000-B stage0",len))board=2;
#endif
     else if(len>=8&&(!memcmp(q,"H3C HW-A",8)||!memcmp(q,"H3C HW-B",8)||!memcmp(q,"H3C HW-C",8)))return -1;
    }else if(!strcmp(pn,"compatible")){
     if(compat_seen++||!len||q[len-1])return -1;
     k=0;
     while(k<len){rt3_md_u32 z;int y=0;
      for(z=k;z<len&&q[z];z++);
      if(z==len||z==k)return -1;
      if(z-k==8&&!memcmp(q+k,"h3c,hw-a",8))y=1;
      else if(z-k==12&&!memcmp(q+k,"h3c,rt3000-b",12))y=2;
      else if(z-k==8&&!memcmp(q+k,"h3c,hw-c",8))y=4;
      if(y&&cb&&y!=cb)return -1;
      if(y)cb=y;
      k=z+1;
     }
    }else if(!strcmp(pn,"machid")){if(machid_seen++||len!=4)return -1;machid=be32(q);}
   }
   p+=padded;
  }else if(x==4){if(ended)return -1;}
  else if(x==9){
   if(!ended||d||roots!=1||p!=e)return -1;
   if(!board||!cb||board!=cb||!machid_seen)return -1;
   if((board==1&&machid!=0x08040001U)||(board==2&&machid!=0x08040000U)||
      (board==4&&machid!=0x08040101U))return -1;
   return board;
  }else return -1;
 }
 return -1;
}
static int fdt_header_candidate(const rt3_md_u8*f,rt3_md_u32 n)
{
 rt3_md_u32 t,v,lv;
 /* A bare magic word in ELF bytes is not enough to identify an FDT. */
 if(!f||n<40||be32(f)!=0xd00dfeedU)return 0;
 t=be32(f+4);v=be32(f+20);lv=be32(f+24);
 return t>=40&&v>=16&&v<=17&&lv>=16&&lv<=v;
}
static void fdt_scan(const rt3_md_u8*b,rt3_md_u32 n,int*m,int*bad)
{
 rt3_md_u32 i;
 for(i=0;i+4<=n;i++)if(be32(b+i)==0xd00dfeedU&&fdt_header_candidate(b+i,n-i)){
  rt3_md_u32 t=be32(b+i+4);int x=fdt_board(b+i,n-i);
  if(x<0)*bad=1;else *m|=x;
  /* Do not reinterpret property/data bytes within a recognized FDT as peers. */
  if(t>=40&&t<=n-i)i+=t-1;
 }
}
static int gzip_header_ok(const rt3_md_u8*s,rt3_md_u32 n){rt3_md_u32 i=10;rt3_md_u8 f;if(n<18||s[0]!=0x1f||s[1]!=0x8b||s[2]!=8)return 0;f=s[3];if(f&0xe0)return 0;if(f&4){rt3_md_u32 z;if(i+2>n)return 0;z=(rt3_md_u32)s[i]|((rt3_md_u32)s[i+1]<<8);i+=2;if(z>n-i)return 0;i+=z;}if(f&8){while(i<n&&s[i])i++;if(i==n)return 0;i++;}if(f&16){while(i<n&&s[i])i++;if(i==n)return 0;i++;}if(f&2){if(i+2>n)return 0;i+=2;}return i<n-8;}
static int __attribute__((unused)) embedded_fdt_identity(const rt3_md_u8*load,rt3_md_u32 size,const char*model,const char*compatible){rt3_md_u8*out;rt3_md_u32 i;int m=0,bad=0,target;(void)compatible;target=!strcmp(model,"H3C HW-A")?1:(!strcmp(model,"H3C HW-B")?2:4);fdt_scan(load,size,&m,&bad);out=malloc(RT3_MD_APPSBL_SIZE);if(!out)return 0;for(i=0;i+3<=size;i++)if(load[i]==0x1f&&load[i+1]==0x8b&&load[i+2]==8&&gzip_header_ok(load+i,size-i)){unsigned long z=size-i;if(!metadata_gunzip(out,RT3_MD_APPSBL_SIZE,(rt3_md_u8*)(load+i),&z)&&z<=RT3_MD_APPSBL_SIZE){fdt_scan(out,(rt3_md_u32)z,&m,&bad);memset(out,0,(rt3_md_u32)z);}}memset(out,0,RT3_MD_APPSBL_SIZE);free(out);return !bad&&m==target;}
static int target_identity(const rt3_md_u8 *load, rt3_md_u32 size)
{
#ifdef RT3_METADATA_HW_A
 return embedded_fdt_identity(load,size,"H3C HW-A","h3c,hw-a");
#elif defined(RT3_METADATA_HW_C)
 return embedded_fdt_identity(load,size,"H3C HW-C","h3c,hw-c");
#elif defined(RT3_METADATA_HW_D)
 /* D's DTB model is not an H3C HW-x name; the build identity binds D. */
 (void)load;(void)size;
 return 1;
#else
 return embedded_fdt_identity(load,size,"H3C HW-B","h3c,rt3000-b");
#endif
}
#ifdef RT3_METADATA_HOST_TEST
int rt3_md_test_target_identity(const rt3_md_u8 *load, rt3_md_u32 size) { return target_identity(load,size); }
int rt3_md_test_fdt_board(const rt3_md_u8 *fdt, rt3_md_u32 size) { return fdt_board(fdt,size); }
#endif
static int phdr_is(const rt3_md_u8*p,rt3_md_u32 type,rt3_md_u32 off,
                   rt3_md_u32 vaddr,rt3_md_u32 paddr,rt3_md_u32 filesz,
                   rt3_md_u32 memsz,rt3_md_u32 flags,rt3_md_u32 align)
{
 return le32(p)==type&&le32(p+4)==off&&le32(p+8)==vaddr&&le32(p+12)==paddr&&
        le32(p+16)==filesz&&le32(p+20)==memsz&&le32(p+24)==flags&&le32(p+28)==align;
}
static int valid_load_align(rt3_md_u32 align)
{
 return align==1||(align>=2&&align<=0x10000U&&!(align&(align-1)));
}
static int known_stack_phdr(const rt3_md_u8*p,rt3_md_u32 n)
{
 rt3_md_u32 flags=le32(p+24),align=le32(p+28);
 return le32(p)==PT_GNU_STACK&&le32(p+4)<=n&&le32(p+8)==0&&
        le32(p+12)==0&&le32(p+16)==0&&le32(p+20)==0&&
        (flags==6||flags==7)&&(align==0||align==1||align==0x10);
}
static int known_header_phdr(const rt3_md_u8*p)
{
 return phdr_is(p,0,0,0,0,0xb4,0,0x07000000,0);
}
static int known_hash_phdr(const rt3_md_u8*p,rt3_md_u32 hash_addr)
{
 return phdr_is(p,0,MBN_HASH_OFFSET,hash_addr,hash_addr,MBN_HASH_SIZE,
                0x1000,0x02200000,0x1000);
}
static int verify_hash_v3(const rt3_md_u8*d,rt3_md_u32 n,
                          const rt3_md_u8*load,rt3_md_u32 load_size,
                          rt3_md_u32 load_index)
{
 rt3_md_u8 digest[SHA256_SUM_LEN];
 if(n<MBN_HASH_OFFSET+MBN_HASH_SIZE||le32(d+MBN_HASH_OFFSET+4)!=MBN_VERSION||
    load_index>=4)return -1;
 sha256_csum_wd(load,load_size,digest,CHUNKSZ_SHA256);
 return memcmp(digest,d+MBN_HASH_OFFSET+40+load_index*SHA256_SUM_LEN,
               SHA256_SUM_LEN)?-1:0;
}
#define OEM_NX30_R012_SIZE 473088U
#define OEM_NX30_R012_TAIL 1754U
static const rt3_md_u8 oem_nx30_r012_sha256[SHA256_SUM_LEN]={
 0x4b,0x2d,0x13,0xb4,0x63,0x97,0xc3,0xd6,
 0x1f,0x82,0x90,0xb8,0xea,0x81,0x1f,0xae,
 0xb5,0xd6,0xa3,0x16,0xe4,0xc9,0x69,0x3d,
 0x5d,0x23,0xf6,0xc8,0xd6,0x09,0xb0,0x50
};
static int canonical_elf(const rt3_md_u8*d,rt3_md_u32 n,int require_marker,
                        rt3_md_u32 tail_size)
{
 const rt3_md_u8*p0,*p1,*pl,*ps;rt3_md_u32 size,hash_addr;rt3_md_u8 digest[32];
 if(!d||n<MBN_LOAD_OFFSET+tail_size||n>RT3_MD_APPSBL_SIZE||d[0]!=0x7f||d[1]!='E'||d[2]!='L'||d[3]!='F'||
    d[4]!=1||d[5]!=1||d[6]!=1||
    (le16(d+16)!=ET_EXEC&&le16(d+16)!=ET_DYN)||le16(d+18)!=EM_ARM||
    le32(d+20)!=1||le32(d+24)!=SDRAM_LOAD||le32(d+28)!=EH||
    le16(d+40)!=EH||le16(d+42)!=PH||le16(d+44)!=4||
    le32(d+32)!=0||le16(d+48)!=0||le16(d+50)!=0)return -1;
 p0=d+EH;p1=p0+PH;pl=p1+PH;ps=pl+PH;
 if(!phdr_is(p0,0,0,0,0,0xb4,0,0x07000000,0)||
    le32(pl)!=PT_LOAD||le32(pl+4)!=MBN_LOAD_OFFSET||
    le32(pl+8)!=SDRAM_LOAD||le32(pl+12)!=SDRAM_LOAD||
    (size=le32(pl+16))==0||le32(pl+20)!=size||
    le32(pl+24)!=7||le32(pl+28)!=0x1000||
    size!=n-MBN_LOAD_OFFSET-tail_size)return -1;
 hash_addr=(SDRAM_LOAD+size+0xfff)&~0xfffU;
 if(!phdr_is(p1,0,MBN_HASH_OFFSET,hash_addr,hash_addr,MBN_HASH_SIZE,
             0x1000,0x02200000,0x1000)||
    !phdr_is(ps,PT_GNU_STACK,0x2000,0,0,0,0,7,0x10)||
    le32(d+MBN_HASH_OFFSET)!=21||le32(d+MBN_HASH_OFFSET+4)!=MBN_VERSION||
    le32(d+MBN_HASH_OFFSET+8)!=0||
    le32(d+MBN_HASH_OFFSET+12)!=hash_addr+MBN_HASH_OFFSET+40||
    le32(d+MBN_HASH_OFFSET+16)!=128||le32(d+MBN_HASH_OFFSET+20)!=128||
    le32(d+MBN_HASH_OFFSET+24)!=hash_addr+MBN_HASH_OFFSET+MBN_HASH_SIZE||
    le32(d+MBN_HASH_OFFSET+28)!=0||
    le32(d+MBN_HASH_OFFSET+32)!=hash_addr+MBN_HASH_OFFSET+MBN_HASH_SIZE||
    le32(d+MBN_HASH_OFFSET+36)!=0)return -1;
 if(require_marker&&!target_identity(d+MBN_LOAD_OFFSET,size))return -1;
 sha256_csum_wd(d,0xb4,digest,CHUNKSZ_SHA256);
 if(memcmp(digest,d+MBN_HASH_OFFSET+40,SHA256_SUM_LEN))return -1;
 return verify_hash_v3(d,n,d+MBN_LOAD_OFFSET,size,2);
}
static int __attribute__((unused)) is_known_oem_nx30_r012(const rt3_md_u8*d,rt3_md_u32 n)
{
 rt3_md_u8 digest[SHA256_SUM_LEN];rt3_md_u32 i;
 if(!d||n!=OEM_NX30_R012_SIZE)return 0;
 sha256_csum_wd(d,n,digest,CHUNKSZ_SHA256);
 if(memcmp(digest,oem_nx30_r012_sha256,sizeof(digest)))return 0;
 for(i=n-OEM_NX30_R012_TAIL;i<n;i++)if(d[i]!=0xff)return 0;
 if(canonical_elf(d,n,0,OEM_NX30_R012_TAIL))return 0;
 return 1;
}
int rt3_md_validate_elf(const rt3_md_u8*d,rt3_md_u32 n)
{
#if defined(RT3_METADATA_HW_A) || defined(RT3_METADATA_HW_C) || defined(RT3_METADATA_HW_D)
 return canonical_elf(d,n,1,0);
#else
 if(!canonical_elf(d,n,1,0))return 0;
 return is_known_oem_nx30_r012(d,n)?0:-1;
#endif
}
const char *rt3_md_uboot_board_name(void)
{
#ifdef RT3_METADATA_HW_A
 return "H3C HW-A U-Boot ELF";
#elif defined(RT3_METADATA_HW_C)
 return "H3C HW-C U-Boot ELF";
#elif defined(RT3_METADATA_HW_D)
 return "H3C HW-D U-Boot ELF";
#else
 return "H3C HW-B U-Boot ELF";
#endif
}
const char *rt3_md_uboot_image_name(const rt3_md_u8*d,rt3_md_u32 n)
{
#if !defined(RT3_METADATA_HW_A) && !defined(RT3_METADATA_HW_C) && !defined(RT3_METADATA_HW_D)
 if(is_known_oem_nx30_r012(d,n))return "NX30 R012 OEM U-Boot (user-provided test image)";
#endif
 if(!canonical_elf(d,n,1,0))return rt3_md_uboot_board_name();
 return NULL;
}
int rt3_md_prepare_elf(const rt3_md_u8*src,rt3_md_u32 n,
                       rt3_md_u8*dst,rt3_md_u32 cap,rt3_md_u32*out_size)
{
 rt3_md_u32 phoff,phentsize,phnum,i,loads=0,stacks=0,load_off=0,load_size=0;
 const rt3_md_u8*load_ph=NULL,*stack_ph=NULL;int packaged=0;rt3_md_u32 new_size,hash_addr,table_end;
 rt3_md_u8 digest[32];
 if(out_size)*out_size=0;
 if(!src||!dst||!out_size||n<EH||n>RT3_MD_APPSBL_SIZE)return -1;
#if !defined(RT3_METADATA_HW_A) && !defined(RT3_METADATA_HW_C) && !defined(RT3_METADATA_HW_D)
 if(is_known_oem_nx30_r012(src,n)){
  if(cap<n)return -1;
  memcpy(dst,src,n);*out_size=n;return 0;
 }
#endif
 if(n<EH||n>RT3_MD_APPSBL_SIZE||
    src[0]!=0x7f||src[1]!='E'||src[2]!='L'||src[3]!='F'||
    src[4]!=1||src[5]!=1||src[6]!=1||
    (le16(src+16)!=ET_EXEC&&le16(src+16)!=ET_DYN)||le16(src+18)!=EM_ARM||
    le32(src+20)!=1||le32(src+24)!=SDRAM_LOAD||le16(src+40)!=EH||
    le32(src+28)!=EH||(phentsize=le16(src+42))!=PH||
    !(phnum=le16(src+44))||phnum>32)return -1;
 phoff=le32(src+28);if(phoff>n||phnum>(n-phoff)/phentsize)return -1;
 table_end=phoff+phnum*phentsize;
 for(i=0;i<phnum;i++){
  const rt3_md_u8*p=src+phoff+i*phentsize;
  if(le32(p)==PT_LOAD){loads++;load_ph=p;}
  else if(le32(p)==PT_GNU_STACK){stacks++;stack_ph=p;}
 }
 if(loads!=1||stacks!=1||!load_ph||!stack_ph||
    !known_stack_phdr(stack_ph,n)||le32(load_ph+8)!=SDRAM_LOAD||
    le32(load_ph+12)!=SDRAM_LOAD||(load_size=le32(load_ph+16))==0||
    le32(load_ph+20)!=load_size||le32(load_ph+24)!=7)return -1;
 load_off=le32(load_ph+4);
 {
  rt3_md_u32 align=le32(load_ph+28);
  if(!valid_load_align(align)||
     ((SDRAM_LOAD-load_off)&(align-1)))return -1;
 }
 if(phnum!=2&&phnum!=4)return -1;
 if(load_off>n||load_size>n-load_off||
    load_size>RT3_MD_APPSBL_SIZE-MBN_LOAD_OFFSET)return -1;
 if(phnum==2){
  /* Raw project ELFs contain only LOAD and the known empty GNU_STACK row. */
  if(load_off<table_end)return -1;
 }else{
  const rt3_md_u8 *p0=src+phoff,*p1=src+phoff+PH;
  rt3_md_u32 hash_end=MBN_HASH_OFFSET+MBN_HASH_SIZE;
  if(!known_header_phdr(p0)||!known_stack_phdr(src+phoff+3*PH,n)||
     load_ph!=src+phoff+2*PH||p1!=src+phoff+PH||
     le32(p1)!=0||le32(p1+4)!=MBN_HASH_OFFSET||
     le32(p1+8)!=le32(p1+12)||le32(p1+16)!=MBN_HASH_SIZE||
     le32(p1+20)!=0x1000||le32(p1+24)!=0x02200000||
     le32(p1+28)!=0x1000||load_off<table_end||
     n<hash_end||(load_off<hash_end&&MBN_HASH_OFFSET<load_off+load_size))
   return -1;
 }
 if(!target_identity(src+load_off,load_size))return -1;
 if(phnum==4&&n>=MBN_HASH_OFFSET+MBN_HASH_SIZE&&
    le32(src+MBN_HASH_OFFSET)==21&&le32(src+MBN_HASH_OFFSET+4)==MBN_VERSION){
  const rt3_md_u8*hp=src+phoff+PH;rt3_md_u32 ha=le32(hp+12);
  packaged=1;
  if(!known_header_phdr(src+phoff)||
     !known_hash_phdr(hp,ha)||!known_stack_phdr(src+phoff+3*PH,n)||
     load_ph!=src+phoff+2*PH||
     ha!=((SDRAM_LOAD+load_size+0xfff)&~0xfffU)||
     le32(src+MBN_HASH_OFFSET+8)!=0||le32(src+MBN_HASH_OFFSET+12)!=ha+MBN_HASH_OFFSET+40||
     le32(src+MBN_HASH_OFFSET+16)!=128||le32(src+MBN_HASH_OFFSET+20)!=128||
     le32(src+MBN_HASH_OFFSET+24)!=ha+MBN_HASH_OFFSET+MBN_HASH_SIZE||
     le32(src+MBN_HASH_OFFSET+28)!=0||le32(src+MBN_HASH_OFFSET+32)!=ha+MBN_HASH_OFFSET+MBN_HASH_SIZE||
     le32(src+MBN_HASH_OFFSET+36)!=0||
     verify_hash_v3(src,n,src+load_off,load_size,2))return -1;
 }else if(phnum==4)return -1;

 new_size=MBN_LOAD_OFFSET+load_size;
 if(new_size>cap||new_size>RT3_MD_APPSBL_SIZE)return -1;
 (void)packaged;
 memset(dst,0,new_size);memcpy(dst,src,EH);
 put32(dst+28,EH);put32(dst+32,0);
 put16(dst+40,EH);put16(dst+42,PH);put16(dst+44,4);
 put16(dst+46,40);put16(dst+48,0);put16(dst+50,0);
 {
  rt3_md_u8*p=dst+EH;
  put32(p,0);put32(p+4,0);put32(p+8,0);put32(p+12,0);
  put32(p+16,0xb4);put32(p+20,0);put32(p+24,0x07000000);put32(p+28,0);
  p+=PH;hash_addr=(SDRAM_LOAD+load_size+0xfff)&~0xfffU;
  put32(p,0);put32(p+4,MBN_HASH_OFFSET);put32(p+8,hash_addr);put32(p+12,hash_addr);
  put32(p+16,MBN_HASH_SIZE);put32(p+20,0x1000);put32(p+24,0x02200000);put32(p+28,0x1000);
  p+=PH;put32(p,PT_LOAD);put32(p+4,MBN_LOAD_OFFSET);put32(p+8,SDRAM_LOAD);put32(p+12,SDRAM_LOAD);
  put32(p+16,load_size);put32(p+20,load_size);put32(p+24,7);put32(p+28,0x1000);
  p+=PH;put32(p,PT_GNU_STACK);put32(p+4,0x2000);put32(p+8,0);put32(p+12,0);
  put32(p+16,0);put32(p+20,0);put32(p+24,7);put32(p+28,0x10);
 }
 {
  rt3_md_u8*h=dst+MBN_HASH_OFFSET;
  put32(h,21);put32(h+4,MBN_VERSION);put32(h+8,0);put32(h+12,hash_addr+MBN_HASH_OFFSET+40);
  put32(h+16,128);put32(h+20,128);put32(h+24,hash_addr+MBN_HASH_OFFSET+MBN_HASH_SIZE);
  put32(h+28,0);put32(h+32,hash_addr+MBN_HASH_OFFSET+MBN_HASH_SIZE);put32(h+36,0);
 }
 memcpy(dst+MBN_LOAD_OFFSET,src+load_off,load_size);
 sha256_csum_wd(dst,0xb4,digest,CHUNKSZ_SHA256);memcpy(dst+MBN_HASH_OFFSET+40,digest,32);
 sha256_csum_wd(dst+MBN_LOAD_OFFSET,load_size,digest,CHUNKSZ_SHA256);
 memcpy(dst+MBN_HASH_OFFSET+40+2*32,digest,32);
 *out_size=new_size;
 return rt3_md_validate_elf(dst,new_size);
}
static int validate_cdt(const rt3_md_u8*d,rt3_md_u32 n)
{
 static const rt3_md_u16 offsets[3]={26,31,543};
 static const rt3_md_u16 lengths[3]={5,512,4};
#ifdef RT3_METADATA_HW_A
 static const rt3_md_u8 b_identity[5]={2,8,4,0,1};
#elif defined(RT3_METADATA_HW_C)
 static const rt3_md_u8 b_identity[5]={2,8,4,1,1};
#else
 static const rt3_md_u8 b_identity[5]={2,8,4,0,0};
#endif
 rt3_md_u32 i;
 if(n!=RT3_MD_CDT_SIZE||d[0]!='C'||d[1]!='D'||d[2]!='T'||d[3]!=0||le16(d+4)!=2)return -1;
 for(i=0;i<3;i++){rt3_md_u32 off=le16(d+14+i*4),len=le16(d+16+i*4);if(off!=offsets[i]||len!=lengths[i]||off>n||len>n-off)return -1;}
 if(memcmp(d+26,b_identity,sizeof(b_identity)))return -1;
 for(i=547;i<n;i++)if(d[i]!=0&&d[i]!=0xff)return -1;
 return 0;
}
int rt3_md_validate_image(enum rt3_md_kind k,const rt3_md_u8*d,rt3_md_u32 n)
{
 if(!d||!n)return -1;
 switch(k){case RT3_MD_UBOOT:return rt3_md_validate_elf(d,n);case RT3_MD_ART:return n==RT3_MD_ART_SIZE?0:-1;case RT3_MD_CDT:return validate_cdt(d,n);default:return -1;}
}
int rt3_md_snapshot_copy(rt3_md_u8*dst,const rt3_md_u8*src,rt3_md_u32 n){if(!dst||!src||!n)return -1;memcpy(dst,src,n);return 0;}
int rt3_md_hash_matches(const rt3_md_u8*e,const rt3_md_u8*a,rt3_md_u32 n){rt3_md_u32 i;if(!e||!a||!n)return 0;for(i=0;i<n;i++)if(e[i]!=a[i])return 0;return 1;}
