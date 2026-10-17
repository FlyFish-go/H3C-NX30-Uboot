#include <stdio.h>
#include <string.h>
#include <rt3000_b_metadata_core.h>
#define C(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static void put32(unsigned char *p,unsigned int v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static unsigned int align4(unsigned int n){return (n+3U)&~3U;}
static unsigned int get32(const unsigned char*p){return ((unsigned int)p[0]<<24)|((unsigned int)p[1]<<16)|((unsigned int)p[2]<<8)|p[3];}
static unsigned int make_fdt(unsigned char *p,const char *model,const char *compat)
{
 static const char strings[]="model\0compatible\0machid\0";
 unsigned int q=56,st;
 memset(p,0,512);
 put32(p+q,1);q+=4; q+=4; /* root BEGIN_NODE and empty name */
 put32(p+q,3);put32(p+q+4,(unsigned int)strlen(model)+1);put32(p+q+8,0);q+=12;
 memcpy(p+q,model,strlen(model)+1);q+= (unsigned int)strlen(model)+1;q=56+align4(q-56);
 put32(p+q,3);put32(p+q+4,(unsigned int)strlen(compat)+1);put32(p+q+8,6);q+=12;
 memcpy(p+q,compat,strlen(compat)+1);q+= (unsigned int)strlen(compat)+1;q=56+align4(q-56);
 put32(p+q,3);put32(p+q+4,4);put32(p+q+8,17);q+=12;
 put32(p+q,!strcmp(model,"H3C HW-A")?0x08040001U:(!strcmp(model,"H3C HW-B")?0x08040000U:0x08040101U));q+=4;
 put32(p+q,2);q+=4;put32(p+q,9);q+=4;st=q-56;
 memcpy(p+q,strings,sizeof(strings));put32(p,0xd00dfeed);put32(p+4,q+sizeof(strings));
 put32(p+8,56);put32(p+12,q);put32(p+16,40);put32(p+20,17);put32(p+24,16);
 put32(p+28,0);put32(p+32,sizeof(strings));put32(p+36,st);
 return q+sizeof(strings);
}
int main(void)
{
 static const unsigned char ct[5]={2,8,4,1,1},at[5]={2,8,4,0,1},bt[5]={2,8,4,0,0};
 unsigned char c[512],a[512],b[512],mix[1024],bad[512],cdt[RT3_MD_CDT_SIZE];
 unsigned int cn,an,bn,i;
 cn=make_fdt(c,"H3C HW-C","h3c,hw-c");an=make_fdt(a,"H3C HW-A","h3c,hw-a");bn=make_fdt(b,"H3C HW-B","h3c,rt3000-b");
 C(rt3_md_test_target_identity(c,cn));
 C(!rt3_md_test_target_identity(a,an));C(!rt3_md_test_target_identity(b,bn));
 memcpy(mix,c,cn);memcpy(mix+cn,a,an);C(!rt3_md_test_target_identity(mix,cn+an));
 C(!rt3_md_test_target_identity(c,cn-1)); /* truncated totalsize */
 memcpy(bad,c,cn);put32(bad+56+get32(bad+36)-4,5);C(!rt3_md_test_target_identity(bad,cn)); /* invalid terminal token */
 memcpy(bad,c,cn);memcpy(bad+56+8+12,"H3C HW-A",8);C(!rt3_md_test_target_identity(bad,cn)); /* model/compatible mismatch */
 memset(cdt,0,sizeof(cdt));memcpy(cdt,"CDT",3);cdt[4]=2;cdt[14]=26;cdt[16]=5;cdt[18]=31;cdt[21]=2;cdt[22]=0x1f;cdt[23]=2;cdt[24]=4;
 memcpy(cdt+26,ct,5);C(rt3_md_validate_image(RT3_MD_CDT,cdt,sizeof(cdt))==0);
 memcpy(cdt+26,at,5);C(rt3_md_validate_image(RT3_MD_CDT,cdt,sizeof(cdt))!=0);
 memcpy(cdt+26,bt,5);C(rt3_md_validate_image(RT3_MD_CDT,cdt,sizeof(cdt))!=0);
 for(i=547;i<sizeof(cdt);i++)cdt[i]=0xff;
 memcpy(cdt+26,ct,5);C(rt3_md_validate_image(RT3_MD_CDT,cdt,sizeof(cdt))==0);
 puts("HW-C FDT structure/mixed identity and CDT tuple host test: PASS");return 0;
}
