#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt3000_b_metadata_core.h"
static unsigned char *read_file(const char *path, unsigned int *size) {
 FILE *f=fopen(path,"rb"); long n; unsigned char *p;
 if(!f || fseek(f,0,SEEK_END) || (n=ftell(f))<=0 || n>0x2000000L || fseek(f,0,SEEK_SET)) return NULL;
 p=malloc((size_t)n); if(!p || fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
 fclose(f); *size=(unsigned int)n; return p;
}
#define C(x) do { if(!(x)){fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);return 1;} } while(0)
int main(int argc,char **argv) {
 unsigned int n,bn,outn=0,i; unsigned char *a,*b,*out,*cdt;
 static const unsigned char A[5]={2,8,4,0,1}, B[5]={2,8,4,0,0};
 C(argc==3); a=read_file(argv[1],&n); b=read_file(argv[2],&bn); C(a&&b);
 out=malloc(RT3_MD_APPSBL_SIZE); cdt=malloc(RT3_MD_CDT_SIZE); C(out&&cdt);
 C(rt3_md_prepare_elf(a,n,out,RT3_MD_APPSBL_SIZE,&outn)==0);
 C(rt3_md_validate_image(RT3_MD_UBOOT,out,outn)==0);
 C(rt3_md_prepare_elf(b,bn,out,RT3_MD_APPSBL_SIZE,&outn)!=0);
 memset(cdt,0,RT3_MD_CDT_SIZE); memcpy(cdt,"CDT",3); cdt[3]=0; cdt[4]=2; cdt[5]=0;
 cdt[14]=26;cdt[16]=5;cdt[18]=31;cdt[21]=2;cdt[22]=0x1f;cdt[23]=2;cdt[24]=4;
 for(i=0;i<5;i++) cdt[26+i]=A[i];
 C(rt3_md_validate_image(RT3_MD_CDT,cdt,RT3_MD_CDT_SIZE)==0);
 memcpy(cdt+26,B,5); C(rt3_md_validate_image(RT3_MD_CDT,cdt,RT3_MD_CDT_SIZE)!=0);
 puts("HW-A U-Boot ELF identity and CDT tuple host test: PASS"); free(a);free(b);free(out);free(cdt);return 0;
}
