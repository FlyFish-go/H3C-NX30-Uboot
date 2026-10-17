#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <squashfs_release.h>
#ifndef ENOTSUP
#define ENOTSUP 95
#endif
static int check(int cond,const char *what){if(!cond)fprintf(stderr,"FAIL: %s\n",what);return !cond;}
static unsigned long long rd64(const unsigned char *p){unsigned i;unsigned long long v=0;for(i=0;i<8;i++)v|=(unsigned long long)p[i]<<(8*i);return v;}
static void wr64(unsigned char *p,unsigned long long v){unsigned i;for(i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));}
int main(int argc,char **argv){FILE *f;long flen;unsigned char *img,*mut;char out[128];int rc,bad=0;size_t n;
 if(argc!=3){fprintf(stderr,"usage: %s image expected-or--ENOENT\n",argv[0]);return 2;}
 f=fopen(argv[1],"rb");if(!f){perror("open image");return 2;}if(fseek(f,0,SEEK_END)||(flen=ftell(f))<0||fseek(f,0,SEEK_SET))return 2;
 img=malloc((size_t)flen);if(!img||fread(img,1,(size_t)flen,f)!=(size_t)flen)return 2;fclose(f);
 memset(out,0xa5,sizeof(out));rc=rt3_squashfs_read_release(img,(size_t)flen,out,sizeof(out));
 if(!strcmp(argv[2],"-ENOENT")){bad+=check(rc==-ENOENT,"absent release / symlink is not followed");}
 else {bad+=check(rc==0,"reader succeeds");if(!rc){if(strcmp(out,argv[2]))fprintf(stderr,"got=[%s] expected=[%s]\n",out,argv[2]);bad+=check(!strcmp(out,argv[2]),"release output matches exact expected value");}
  memset(out,0xa5,sizeof(out));rc=rt3_squashfs_read_release(img,(size_t)flen,out,8);bad+=check(rc==-ENOSPC,"undersized output is rejected");bad+=check((unsigned char)out[0]==0xa5,"undersized output is not partially modified");}
 rc=rt3_squashfs_read_release(img,31,out,sizeof(out));bad+=check(rc<0,"truncated superblock is rejected");
 mut=malloc((size_t)flen);if(!mut)return 2;memcpy(mut,img,(size_t)flen);if(flen>=96){mut[20]=99;mut[21]=0;rc=rt3_squashfs_read_release(mut,(size_t)flen,out,sizeof(out));bad+=check(rc==-ENOTSUP,"unsupported compressor is rejected");memcpy(mut,img,(size_t)flen);wr64(mut+40,rd64(mut+40)+(1ULL<<20));rc=rt3_squashfs_read_release(mut,(size_t)flen,out,sizeof(out));bad+=check(rc<0,"bytes_used beyond buffer is rejected");}
 free(mut);free(img);if(bad)return 1;printf("PASS %s: %s\n",argv[1],argv[2]);return 0;}
