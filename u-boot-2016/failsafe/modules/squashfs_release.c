// SPDX-License-Identifier: GPL-2.0+
/* Bounded, read-only SquashFS v4 release-file reader. */
#include <common.h>
#include <errno.h>
#include <malloc.h>
#include <squashfs_release.h>
#include "xz/xz.h"
#define SQFS_MAGIC 0x73717368U
#define SQFS_META 8192U
#define SQFS_MAX_BLOCK (1U << 20)
#define SQFS_MAX_IMAGE (32U << 20)
#define SQFS_MAX_FILE 8192U
#define SQFS_MAX_DIR_BYTES (4096U * 300U)
#define SQFS_INVALID_FRAG 0xffffffffU
#define SQFS_COMPRESSED_BIT 0x8000U /* metadata block header */
#define SQFS_DATA_UNCOMPRESSED 0x01000000U
#define SQFS_DATA_SIZE_MASK 0x00ffffffU
#define SQFS_XZ 4U
#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif
struct sqfs { const u8 *img; size_t len; u64 used, inode_start, dir_start, frag_start; u32 inodes, block_size, fragments; u16 flags, comp; };
struct meta { const struct sqfs *s; u64 pos; u16 off; u8 data[SQFS_META]; u16 size; };
struct dirent_ref { char name[256]; u64 ref; u16 type; };
static u16 rd16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u64 rd64(const u8 *p) { return (u64)rd32(p) | ((u64)rd32(p + 4) << 32); }
static int span(const struct sqfs *s, u64 off, size_t n) { return off <= s->used && n <= s->used - off; }
static int xz_block(const u8 *in, size_t inlen, u8 *out, size_t cap, size_t *olen)
{
 static int crc_ready;
 struct xz_dec *dec; struct xz_buf b; enum xz_ret r;
 if (!crc_ready) { xz_crc32_init(); crc_ready = 1; }
 dec = xz_dec_init(XZ_SINGLE, 0); if (!dec) return -ENOMEM;
 b.in=in; b.in_pos=0; b.in_size=inlen; b.out=out; b.out_pos=0; b.out_size=cap;
 r=xz_dec_run(dec,&b); xz_dec_end(dec);
 if(r!=XZ_STREAM_END || b.in_pos!=inlen || b.out_pos>cap) return -EBADMSG; *olen=b.out_pos; return 0;
}
static int meta_load(struct meta *m)
{
 const struct sqfs *s=m->s; u16 h; size_t n; u64 data;
 if(!span(s,m->pos,2))return -EBADMSG;
 h=rd16(s->img+m->pos); n=h&~SQFS_COMPRESSED_BIT; data=m->pos+2;
 if(!n||n>65535U||!span(s,data,n))return -EBADMSG;
 if(h&SQFS_COMPRESSED_BIT){if(n>SQFS_META)return -EBADMSG;memcpy(m->data,s->img+data,n);m->size=n;}
 else {size_t decoded=0;if(s->comp!=SQFS_XZ||xz_block(s->img+data,n,m->data,sizeof(m->data),&decoded))return -EBADMSG;if(!decoded||decoded>SQFS_META)return -EBADMSG;m->size=(u16)decoded;}
 m->pos=data+n;m->off=0;return 0;
}
static int meta_read(struct meta *m,void *dst,size_t n)
{
 u8 *d=dst;while(n){size_t take;if(m->off>=m->size&&meta_load(m))return -EBADMSG;take=m->size-m->off;if(take>n)take=n;memcpy(d,m->data+m->off,take);d+=take;n-=take;m->off+=take;}return 0;
}
static int meta_at(struct meta *m,const struct sqfs *s,u64 table,u32 block,u16 off)
{if(block>s->used||table>s->used-block)return -EBADMSG;memset(m,0,sizeof(*m));m->s=s;m->pos=table+block;if(meta_load(m)||off>=m->size)return -EBADMSG;m->off=off;return 0;}
static int inode_type(const struct sqfs *s,u64 ref,u16 *type,struct meta *m)
{u64 block64=ref>>16;u32 block;u16 off=(u16)ref;u8 b[16];if(block64>0xffffffffU)return -EBADMSG;block=(u32)block64;if(meta_at(m,s,s->inode_start,block,off)||meta_read(m,b,sizeof(b)))return -EBADMSG;*type=rd16(b);return 0;}
static int valid_block_log(u32 size,u16 log)
{u32 v=4096;u16 l=12;while(v<size&&l<32){v<<=1;l++;}return v==size&&l==log;}
static int find_entry(const struct sqfs *s,u64 dirref,const char *wanted,struct dirent_ref *found)
{
 struct meta im,dm;u16 type;u8 dhdr[12],tail[24];u32 start,dsize;u16 doff;
 if(inode_type(s,dirref,&type,&im))return -EBADMSG;
 if(type==1){if(meta_read(&im,tail,12))return -EBADMSG;start=rd32(tail);dsize=rd16(tail+8);doff=rd16(tail+10);}
 else if(type==8){if(meta_read(&im,tail,24))return -EBADMSG;dsize=rd32(tail+4);start=rd32(tail+8);doff=rd16(tail+18);}
 else return -ENOTDIR;
 if(dsize<3||dsize>SQFS_MAX_DIR_BYTES)return -EBADMSG;dsize-=3;
 if(meta_at(&dm,s,s->dir_start,start,doff))return -EBADMSG;
 while(dsize){u32 count,iblock,i;if(dsize<12||meta_read(&dm,dhdr,12))return -EBADMSG;dsize-=12;count=rd32(dhdr);iblock=rd32(dhdr+4);
if(count>=256)return -EBADMSG;count++;
  for(i=0;i<count;i++){u8 de[8],name[256];size_t nl;u32 off;if(dsize<8||meta_read(&dm,de,8))return -EBADMSG;dsize-=8;off=rd16(de);nl=(size_t)rd16(de+6)+1;if(!nl||nl>255||nl>dsize||meta_read(&dm,name,nl))return -EBADMSG;dsize-=nl;if(memchr(name,0,nl)||memchr(name,'/',nl))return -EBADMSG;
   if(strlen(wanted)==nl&&!memcmp(name,wanted,nl)){memcpy(found->name,name,nl);found->name[nl]=0;found->ref=((u64)iblock<<16)|off;found->type=rd16(de+4);return 0;}}
 }
 return -ENOENT;
}
static int data_block(const struct sqfs *s,u64 *pos,u32 enc,u8 *out,size_t cap,size_t *olen)
{u32 n;int r;if(enc&0xfe000000U)return -EBADMSG;n=enc&SQFS_DATA_SIZE_MASK;if(!n||n>cap+65536U||!span(s,*pos,n))return -EBADMSG;if(enc&SQFS_DATA_UNCOMPRESSED){if(n>cap)return -EFBIG;memcpy(out,s->img+*pos,n);*olen=n;r=0;}else if(s->comp==SQFS_XZ)r=xz_block(s->img+*pos,n,out,cap,olen);else return -ENOTSUP;*pos+=n;return r;}
static int fragment_read(const struct sqfs *s,u32 idx,u32 off,u8 *dst,size_t n)
{
 u64 ti,ptr,pos;struct meta m;u8 ent[16],*buf;size_t outn=0;u32 enc;
 if(idx>=s->fragments||s->frag_start>=s->used)return -EBADMSG;ti=(u64)(idx/(SQFS_META/16))*8;if(!span(s,s->frag_start+ti,8))return -EBADMSG;ptr=rd64(s->img+s->frag_start+ti);
 if(ptr>0xffffffffU||meta_at(&m,s,0,(u32)ptr,(u16)((idx%(SQFS_META/16))*16))||meta_read(&m,ent,16))return -EBADMSG;
 pos=rd64(ent);enc=rd32(ent+8);if(off>s->block_size||n>s->block_size-off)return -EBADMSG;
 buf=malloc(s->block_size);if(!buf)return -ENOMEM;{int r=data_block(s,&pos,enc,buf,s->block_size,&outn);if(r){free(buf);return r;}}
 if(off>outn||n>outn-off){free(buf);return -EBADMSG;}memcpy(dst,buf+off,n);free(buf);return 0;
}
static int read_regular(const struct sqfs *s,u64 ref,u8 *out,size_t *outlen)
{
 struct meta m;u16 type;u8 h[40];u64 pos,fsize;u32 start,frag,offset,nb,i;u32 sizes[128];size_t copied=0;
 if(inode_type(s,ref,&type,&m))return -EBADMSG;
 if(type==2){if(meta_read(&m,h,16))return -EBADMSG;start=rd32(h);frag=rd32(h+4);offset=rd32(h+8);fsize=rd32(h+12);}
 else if(type==9){u8 l[40];u64 long_start;if(meta_read(&m,l,40))return -EBADMSG;long_start=rd64(l);if(long_start>0xffffffffU||long_start>=s->used)return -EBADMSG;start=(u32)long_start;fsize=rd64(l+8);frag=rd32(l+28);offset=rd32(l+32);}
 else return -ELOOP;
 if(!fsize||fsize>SQFS_MAX_FILE||offset>s->block_size)return -EFBIG;nb=(u32)(fsize/s->block_size);if(frag==SQFS_INVALID_FRAG&&fsize%s->block_size)nb++;if(nb>128)return -EFBIG;
 if(meta_read(&m,sizes,nb*4))return -EBADMSG;pos=start;
 for(i=0;i<nb;i++){u32 want=s->block_size;size_t got=0;u8 *buf;if(want>fsize-copied)want=fsize-copied;buf=malloc(s->block_size);if(!buf)return -ENOMEM;{int r=data_block(s,&pos,rd32((u8*)sizes+i*4),buf,s->block_size,&got);if(r||got<want){free(buf);return r?r:-EBADMSG;}}if(copied+want>SQFS_MAX_FILE){free(buf);return -EFBIG;}memcpy(out+copied,buf,want);copied+=want;free(buf);}
 if(copied<fsize){size_t rem=fsize-copied;if(frag==SQFS_INVALID_FRAG||rem>s->block_size||fragment_read(s,frag,offset,out+copied,rem))return -EBADMSG;copied+=rem;}
 *outlen=copied;return 0;
}
static int safe_value(const char *v,size_t n)
{size_t i;if(!n||n>96)return 0;for(i=0;i<n;i++){unsigned char c=v[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c==' '||c=='.'||c=='_'||c=='-'||c=='+'||c=='('||c==')'||c==':'||c=='/'))return 0;}return 1;}
static size_t trim(char *v,size_t n)
{size_t i=0;while(n&&(v[n-1]=='\r'||v[n-1]==' '||v[n-1]=='\t'))n--;while(i<n&&(v[i]==' '||v[i]=='\t'))i++;if(i){memmove(v,v+i,n-i);n-=i;}if(n>=2&&((v[0]=='"'&&v[n-1]=='"')||(v[0]=='\''&&v[n-1]=='\''))){memmove(v,v+1,n-2);n-=2;}v[n]=0;return n;}
static int parse_release(const u8 *buf,size_t n,const char *kind,char out[97])
{
 char release[97]={0},rev[97]={0},desc[97]={0},pretty[97]={0},id[97]={0};size_t p=0;
 while(p<n){size_t e=p,eq,kn,vn;char val[128];int wanted=0;while(e<n&&buf[e]!='\n')e++;eq=p;while(eq<e&&buf[eq]!='=')eq++;if(eq<e){kn=eq-p;
  wanted=(kn==15&&!memcmp(buf+p,"DISTRIB_RELEASE",15))||(kn==16&&!memcmp(buf+p,"DISTRIB_REVISION",16))||(kn==19&&!memcmp(buf+p,"DISTRIB_DESCRIPTION",19))||(kn==11&&!memcmp(buf+p,"PRETTY_NAME",11))||(kn==10&&!memcmp(buf+p,"VERSION_ID",10))||(kn==10&&!memcmp(buf+p,"DISTRIB_ID",10))||(kn==2&&!memcmp(buf+p,"ID",2));
  if(wanted){vn=e-eq-1;if(vn>=sizeof(val))return -EBADMSG;memcpy(val,buf+eq+1,vn);vn=trim(val,vn);if(vn>96||!safe_value(val,vn))return -EBADMSG;
   if(kn==15&&!memcmp(buf+p,"DISTRIB_RELEASE",15))memcpy(release,val,vn+1);
   else if(kn==16&&!memcmp(buf+p,"DISTRIB_REVISION",16))memcpy(rev,val,vn+1);
   else if(kn==19&&!memcmp(buf+p,"DISTRIB_DESCRIPTION",19))memcpy(desc,val,vn+1);
   else if(kn==11&&!memcmp(buf+p,"PRETTY_NAME",11))memcpy(pretty,val,vn+1);
   else if(kn==10&&!memcmp(buf+p,"VERSION_ID",10)&&!release[0])memcpy(release,val,vn+1);
   else if((kn==10&&!memcmp(buf+p,"DISTRIB_ID",10))||(kn==2&&!memcmp(buf+p,"ID",2)))memcpy(id,val,vn+1);
  }
 }p=e+1;}
 if(!strcmp(kind,"openwrt_version")&&!release[0]){p=0;while(p<n&&(buf[p]=='\r'||buf[p]=='\n'||buf[p]==' '))p++;size_t e=p;while(e<n&&buf[e]!='\r'&&buf[e]!='\n')e++;size_t l=e-p;if(l>96)return -EBADMSG;memcpy(release,buf+p,l);release[l]=0;if(!safe_value(release,l))return -EBADMSG;}
 if(desc[0]){strcpy(out,desc);if(rev[0]&&!strstr(desc,rev)){size_t a=strlen(out),b=strlen(rev);if(a+b+2>96)return -EBADMSG;strcat(out," ");strcat(out,rev);}}
 else if(pretty[0])strcpy(out,pretty);
 else if(release[0]&&rev[0]){const char *prefix=id[0]?id:"OpenWrt";if(strlen(prefix)+strlen(release)+strlen(rev)+3>96)return -EBADMSG;strcpy(out,prefix);strcat(out," ");strcat(out,release);strcat(out," ");strcat(out,rev);}
 else if(release[0]){if(id[0]&&strlen(id)+strlen(release)+2<=96){strcpy(out,id);strcat(out," ");strcat(out,release);}else strcpy(out,release);}
 else return -ENOENT;
 return safe_value(out,strlen(out))?0:-EBADMSG;
}
int rt3_squashfs_read_release(const void *image,size_t image_len,char *out,size_t out_len)
{
 const u8 *im=image;struct sqfs s;struct dirent_ref etc,f;u64 root;u8 file[SQFS_MAX_FILE];u8 sb[96];size_t flen;const char *names[]={"openwrt_release","openwrt_version","os-release"};unsigned i;int r=-ENOENT;char parsed[97];
 if(!image||!out||out_len<2)return -EINVAL;if(image_len<96||image_len>SQFS_MAX_IMAGE)return -EFBIG;memset(&s,0,sizeof(s));s.img=im;s.len=image_len;if(rd32(im)!=SQFS_MAGIC)return -EINVAL;if(image_len<sizeof(sb))return -EBADMSG;memcpy(sb,im,sizeof(sb));
 s.inodes=rd32(sb+4);s.block_size=rd32(sb+12);s.fragments=rd32(sb+16);s.comp=rd16(sb+20);s.flags=rd16(sb+24);root=rd64(sb+32);s.used=rd64(sb+40);s.inode_start=rd64(sb+64);s.dir_start=rd64(sb+72);s.frag_start=rd64(sb+80);
 if(rd16(sb+28)!=4||rd16(sb+30)!=0||s.comp!=SQFS_XZ)return -ENOTSUP;
 if(!s.used||s.used>image_len||s.used>SQFS_MAX_IMAGE||!s.inodes||s.inodes>100000||s.block_size<4096||s.block_size>SQFS_MAX_BLOCK||!valid_block_log(s.block_size,rd16(sb+22))||s.inode_start>=s.used||s.dir_start>=s.used)return -EBADMSG;
 r=find_entry(&s,root,"etc",&etc);if(r||(etc.type!=1&&etc.type!=8))return -ENOENT;
 for(i=0;i<3;i++){r=find_entry(&s,etc.ref,names[i],&f);if(r)continue;if(f.type!=2&&f.type!=9)continue;flen=0;r=read_regular(&s,f.ref,file,&flen);if(r)continue;r=parse_release(file,flen,names[i],parsed);if(!r){size_t n=strlen(parsed);if(n+1>out_len)return -ENOSPC;memcpy(out,parsed,n+1);return 0;}}
 out[0]=0;return r<0?r:-ENOENT;
}
