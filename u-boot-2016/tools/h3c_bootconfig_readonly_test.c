#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "h3c_bootconfig_readonly.h"
struct fixture {
 unsigned char data[2][2][336];
 int status[2][2];
 unsigned int actual[2][2], calls[2], reads, writes, erases;
};
static void put32(unsigned char *p, unsigned int n)
{ unsigned int i; for(i=0;i<4;++i) p[i]=(unsigned char)(n>>(8*i)); }
static void record(unsigned char *p)
{
 memset(p,0,336); put32(p,0xa3a2a1a0U); put32(p+4,7);put32(p+8,2);
 memcpy(p+12,"rootfs",7);put32(p+28,1);
 memcpy(p+32,"APPSBL",7);put32(p+48,0); put32(p+332,0xb3b2b1b0U);
}
static void setup(struct fixture *f)
{
 unsigned int c,p; memset(f,0,sizeof(*f));
 for(c=0;c<2;++c) for(p=0;p<2;++p) {record(f->data[c][p]);f->actual[c][p]=336;}
}
static int read_copy(void *v,unsigned int c,unsigned char *p,unsigned int n,unsigned int *actual)
{
 struct fixture *f=v;unsigned int pass=f->calls[c]++;
 assert(c<2&&pass<2&&n==336);++f->reads;
 memcpy(p,f->data[c][pass],336);*actual=f->actual[c][pass];return f->status[c][pass];
}
static int run(struct fixture *f, int expected)
{
 struct h3c_bc_ro_result r; memset(&r,0xa5,sizeof(r));
 int rc=h3c_bc_ro_select(read_copy,f,"rootfs",&r);
 assert(rc==expected);assert(f->reads==4);assert(f->writes==0&&f->erases==0);
 if(rc) {const unsigned char *p=(const unsigned char *)&r;unsigned int i;for(i=0;i<sizeof(r);++i)assert(p[i]==0xa5);}
 else {assert(r.selector==1);assert(r.age==7);}
 return rc;
}
static void mutate_word(struct fixture *f,unsigned int c,unsigned int off,unsigned int value)
{put32(f->data[c][0]+off,value);put32(f->data[c][1]+off,value);}
int main(void)
{
 struct fixture f;struct h3c_bc_ro_result r;unsigned int p,i;
 setup(&f);assert(h3c_bc_ro_select(read_copy,&f,"rootfs",&r)==0);
 assert(r.valid_mask==3&&r.selected_copy==0&&r.selector==1&&r.age==7);
 assert(r.crc32[0]==r.crc32[1]);assert(f.reads==4);
 setup(&f);mutate_word(&f,0,0,0);assert(h3c_bc_ro_select(read_copy,&f,"rootfs",&r)==0);assert(r.selected_copy==1&&r.valid_mask==2);
 setup(&f);mutate_word(&f,1,332,0);run(&f,0);
 setup(&f);mutate_word(&f,0,0,0);mutate_word(&f,1,0,0);run(&f,H3C_BC_RO_IO);
 setup(&f);mutate_word(&f,1,4,8);run(&f,H3C_BC_RO_CONFLICT);
 setup(&f);mutate_word(&f,1,28,0);run(&f,H3C_BC_RO_CONFLICT);
 setup(&f);mutate_word(&f,0,4,0xffffffffU);run(&f,H3C_BC_RO_CONFLICT);
 setup(&f);mutate_word(&f,0,8,17);run(&f,H3C_BC_RO_INVALID);
 setup(&f);mutate_word(&f,0,8,0);run(&f,H3C_BC_RO_INVALID);
 setup(&f);mutate_word(&f,0,28,2);run(&f,H3C_BC_RO_INVALID);
 setup(&f);mutate_word(&f,0,48,2);run(&f,H3C_BC_RO_INVALID);
 setup(&f);for(p=0;p<2;++p)memcpy(f.data[0][p]+32,"rootfs",7);run(&f,H3C_BC_RO_INVALID);
 setup(&f);for(p=0;p<2;++p)memset(f.data[0][p]+12,'x',16);run(&f,H3C_BC_RO_INVALID);
 setup(&f);for(p=0;p<2;++p)f.data[0][p][12]=0;run(&f,H3C_BC_RO_INVALID);
 setup(&f);for(p=0;p<2;++p)f.data[0][p][12]=0x80;run(&f,H3C_BC_RO_INVALID);
 setup(&f);for(p=0;p<2;++p)memcpy(f.data[0][p]+12,"other!",7);run(&f,H3C_BC_RO_INVALID);
 setup(&f);mutate_word(&f,0,0,0xa3a2a1a1U);run(&f,H3C_BC_RO_INVALID);
 setup(&f);f.data[0][1][4]^=1;run(&f,H3C_BC_RO_CHANGED);
 setup(&f);f.status[0][1]=-5;run(&f,H3C_BC_RO_CHANGED);
 setup(&f);f.actual[0][1]=335;run(&f,H3C_BC_RO_CHANGED);
 setup(&f);f.status[0][0]=f.status[0][1]=-5;run(&f,0);
 setup(&f);f.actual[0][0]=f.actual[0][1]=335;run(&f,0);
 setup(&f);for(i=0;i<2;++i)for(p=0;p<2;++p)f.status[i][p]=-74;run(&f,H3C_BC_RO_IO);
 /* Equality includes inactive entries/padding, avoiding undocumented semantics. */
 setup(&f);f.data[1][0][331]=f.data[1][1][331]=1;run(&f,H3C_BC_RO_CONFLICT);
 setup(&f);for(i=0;i<2;++i)for(p=0;p<2;++p){unsigned char *b=f.data[i][p];put32(b+8,16);for(unsigned int j=2;j<16;++j){snprintf((char *)b+12+20*j,16,"part%u",j);put32(b+28+20*j,j&1);}}run(&f,0);
 setup(&f);assert(h3c_bc_ro_select(read_copy,&f,"",&r)==H3C_BC_RO_INVALID&&f.reads==0);
 setup(&f);assert(h3c_bc_ro_select(read_copy,&f,"1234567890123456",&r)==H3C_BC_RO_INVALID&&f.reads==0);
 assert(h3c_bc_ro_select(NULL,&f,"rootfs",&r)==H3C_BC_RO_INVALID);
 puts("bootconfig readonly core PASS (synthetic host only)");return 0;
}
