#include <assert.h>
#include <errno.h>
#include <string.h>
#include <nx30_d_fw_txn_core.h>
#include <nx30_d_service.h>
#define PEB 16U
#define EXT 48U
struct fake { unsigned char flash[EXT]; int bad_at, open_ret, erase_ret, write_fail_at, read_fail_at, mismatch_at, close_ret; unsigned int opens,closes,erases,writes,reads; };
static int bad(void *p,u64 o) { struct fake *f=p; return o==(u64)(100+f->bad_at*PEB)?1:0; }
static int openw(void *p,u64 o,u64 n) { struct fake *f=p; (void)o;(void)n;f->opens++;return f->open_ret; }
static int erasew(void *p,u64 o,u32 n) { struct fake *f=p;(void)o;f->erases++;if(f->erase_ret)return f->erase_ret;memset(f->flash,0xff,n);return 0; }
static int writew(void *p,u64 o,const u8 *d,u32 n) { struct fake *f=p;unsigned int i=(o-100)/PEB;f->writes++;if((int)i==f->write_fail_at)return -EIO;memcpy(f->flash+i*PEB,d,n);return 0; }
static int readw(void *p,u64 o,u8 *d,u32 n) { struct fake *f=p;unsigned int i=(o-100)/PEB;f->reads++;if((int)i==f->read_fail_at)return -EIO;memcpy(d,f->flash+i*PEB,n);if((int)i==f->mismatch_at)d[0]^=1;return 0; }
static void closew(void *p) { ((struct fake *)p)->closes++; }
static int closest(void *p) { return ((struct fake *)p)->close_ret; }
static void reset(struct fake *f) { memset(f,0,sizeof(*f));f->bad_at=-1;f->write_fail_at=-1;f->read_fail_at=-1;f->mismatch_at=-1; }
static int run(struct fake *f,const u8 *im,u32 *verified,u32 *stage)
{
 struct nx30_d_fw_txn_io io={f,bad,openw,erasew,writew,readw,closew,closest};u8 wb[PEB],vb[PEB];
 return nx30_d_fw_txn_run(&io,100,EXT,im,2*PEB,PEB,wb,vb,verified,stage);
}
int main(void)
{
 struct fake f;u8 image[2*PEB];u32 verified,stage;int ret;unsigned int i;
 for(i=0;i<sizeof(image);i++)image[i]=(u8)(i+1);
 reset(&f);memset(f.flash,0,EXT);ret=run(&f,image,&verified,&stage);assert(!ret&&f.opens==1&&f.closes==1&&f.erases==1&&f.writes==2&&verified==EXT&&stage==NX30_D_STAGE_COMPLETE);assert(!memcmp(f.flash,image,sizeof(image)));for(i=sizeof(image);i<EXT;i++)assert(f.flash[i]==0xff);
 reset(&f);f.bad_at=1;ret=run(&f,image,&verified,&stage);assert(ret==-EIO&&!f.opens&&!f.erases&&!f.writes&&!f.closes);
 reset(&f);f.open_ret=-EPERM;ret=run(&f,image,&verified,&stage);assert(ret==-EPERM&&f.opens==1&&!f.closes&&!f.erases);
 reset(&f);f.erase_ret=-EIO;ret=run(&f,image,&verified,&stage);assert(ret==-EIO&&f.closes==1&&!f.writes);
 reset(&f);f.write_fail_at=0;ret=run(&f,image,&verified,&stage);assert(ret==-EIO&&f.writes==1&&f.closes==1);
 reset(&f);f.mismatch_at=0;ret=run(&f,image,&verified,&stage);assert(ret==-EIO&&f.writes==1&&f.closes==1);
 reset(&f);f.close_ret=-EACCES;ret=run(&f,image,&verified,&stage);assert(ret==-EACCES&&f.closes==1&&stage!=NX30_D_STAGE_COMPLETE);
 return 0;
}
