#define NX30_D_BC_HOST_TEST
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <nx30_d_bc_core.h>
#define SIZE 256
struct flash { unsigned char copy[2][SIZE]; int fail_call; int calls; int writes[8]; };
static int program(void *cookie,unsigned int copy,const void *bytes)
{
 struct flash *f=cookie;
 int n=++f->calls; f->writes[n-1]=copy;
 memcpy(f->copy[copy],bytes,SIZE); /* failure can follow a partial mutation */
 return n==f->fail_call?-5:0;
}
static unsigned int crc(const unsigned char *p,size_t n)
{
 unsigned int c=~0U;size_t i;int b;for(i=0;i<n;i++){c^=p[i];for(b=0;b<8;b++)c=(c>>1)^((0U-(c&1U))&0xedb88320U);}return ~c;
}
int main(void)
{
 struct flash f; unsigned char old0[SIZE],old1[SIZE],candidate[SIZE]; int ret;
 memset(old0,0x37,SIZE);memcpy(old1,old0,SIZE);memset(candidate,0x51,SIZE);memset(&f,0,sizeof(f));
 memcpy(f.copy[0],old0,SIZE);memcpy(f.copy[1],old1,SIZE);
 ret=nx30_d_bc_core_commit_pair(&f,program,candidate);assert(!ret);
 assert(!memcmp(f.copy[0],candidate,SIZE)&&!memcmp(f.copy[1],candidate,SIZE));
 assert(f.writes[0]==1&&f.writes[1]==0);
 /* BC1 failure stops before BC0. Full peer backups remain separate. */
 memset(&f,0,sizeof(f));memcpy(f.copy[0],old0,SIZE);memcpy(f.copy[1],old1,SIZE);f.fail_call=1;
 ret=nx30_d_bc_core_commit_pair(&f,program,candidate);assert(ret==-5);
 assert(f.calls==1&&f.writes[0]==1);
 assert(!memcmp(f.copy[0],old0,SIZE));
 /* BC0 failure stops immediately; no automatic rollback or retry occurs. */
 memset(&f,0,sizeof(f));memcpy(f.copy[0],old0,SIZE);memcpy(f.copy[1],old1,SIZE);f.fail_call=2;
 ret=nx30_d_bc_core_commit_pair(&f,program,candidate);assert(ret==-5);
 assert(f.calls==2&&f.writes[0]==1&&f.writes[1]==0);
 assert(crc(f.copy[0],SIZE)!=crc(old0,SIZE));
 assert(crc(f.copy[1],SIZE)==crc(candidate,SIZE));
 return 0;
}
