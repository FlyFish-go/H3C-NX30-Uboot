#define NX30_D_READ_HOST_TEST
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <nx30_d_read_core.h>
#define BASE 0x480000ULL
#define TOTAL (8*NX30_D_READ_WINDOW_BYTES)
struct fixture {
 const nx30_d_read_u8 *source;
 size_t source_len,calls,fail_call,short_call,euclean_call; nx30_d_read_u64 base;
 int hard_error;
};
static int read_window(void *ctx,nx30_d_read_u64 off,size_t len,size_t *got,nx30_d_read_u8 *dst)
{
 struct fixture *f=ctx; size_t call=++f->calls;
 if (call==f->fail_call) return f->hard_error;
 if (off<f->base || off-f->base>f->source_len || len>f->source_len-(size_t)(off-f->base)) return -EINVAL;
 memcpy(dst,f->source+(size_t)(off-f->base),len);
 *got=len;
 if (call==f->short_call) { (*got)--; return call==f->euclean_call?-EUCLEAN:0; }
 if (call==f->euclean_call) return -EUCLEAN;
 return 0;
}
int main(void)
{
 nx30_d_read_u8 src[TOTAL],dst[TOTAL]; struct fixture f; size_t i;
 for(i=0;i<TOTAL;i++) src[i]=(nx30_d_read_u8)(i*31U);
 memset(&f,0,sizeof(f)); f.source=src; f.source_len=TOTAL; f.base=BASE;
 assert(nx30_d_read_extent(read_window,&f,BASE,TOTAL,dst)==0);
 assert(f.calls==8 && !memcmp(src,dst,TOTAL));
 memset(&f,0,sizeof(f)); f.source=src; f.source_len=TOTAL; f.base=BASE; f.euclean_call=2;
 assert(nx30_d_read_extent(read_window,&f,BASE,TOTAL,dst)==0);
 assert(f.calls==8);
 memset(&f,0,sizeof(f)); f.source=src; f.source_len=TOTAL; f.base=BASE; f.short_call=2;
 assert(nx30_d_read_extent(read_window,&f,BASE,TOTAL,dst)==-EIO);
 assert(f.calls==2);
 memset(&f,0,sizeof(f)); f.source=src; f.source_len=TOTAL; f.base=BASE; f.euclean_call=2; f.short_call=2;
 assert(nx30_d_read_extent(read_window,&f,BASE,TOTAL,dst)==-EUCLEAN);
 assert(f.calls==2);
 memset(&f,0,sizeof(f)); f.source=src; f.source_len=TOTAL; f.base=BASE; f.fail_call=2; f.hard_error=-EBADMSG;
 assert(nx30_d_read_extent(read_window,&f,BASE,TOTAL,dst)==-EBADMSG);
 assert(f.calls==2);
 memset(&f,0,sizeof(f)); f.source=src; f.source_len=TOTAL; f.base=BASE;
 assert(nx30_d_read_extent(read_window,&f,0xfffffffffffffff0ULL,32,dst)==-EINVAL);
 assert(f.calls==0);
 return 0;
}
