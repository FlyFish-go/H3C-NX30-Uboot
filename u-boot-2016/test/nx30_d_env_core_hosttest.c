#define NX30_D_ENV_HOST_TEST
#include <assert.h>

#include <string.h>
#include <nx30_d_env_core.h>
#define H 4
#define D 512
static unsigned int crc32_ref(const unsigned char *p, size_t n)
{
 unsigned int c=~0U; size_t i; int b;
 for(i=0;i<n;i++){ c^=p[i]; for(b=0;b<8;b++) c=(c>>1)^((0U-(c&1U))&0xedb88320U); }
 return ~c;
}
static int has_entry(const unsigned char *d,size_t n,const char *entry)
{
 size_t p=0,l=strlen(entry);
 while(p<n && d[p]) { size_t e=p; while(e<n&&d[e])e++; if(e-p==l&&!memcmp(d+p,entry,l))return 1; p=e+1; }
 return 0;
}
int main(void)
{
 const char *keys[]={"custom_network","ipaddr","netmask","serverip","ethact","ethprime"};
 const char *cn[]={"ipaddr","serverip","custom_network"};
 const char *cv[]={"192.168.1.1","192.168.1.2",""};
 const unsigned char unset[]={0,0,1};
 unsigned char in[H+D],out[H+D]; char values[6][128]; size_t p=0;
 memset(in,0xa5,sizeof(in)); memset(in+H,0,D);
 memcpy(in+H+p,"bootcmd=bootipq",15);p+=15;in[H+p++]=0;
 memcpy(in+H+p,"ipaddr=192.168.0.1",18);p+=18;in[H+p++]=0;
 memcpy(in+H+p,"untouched=keep-bytes",20);p+=20;in[H+p++]=0;
 memcpy(in+H+p,"custom_network=1",16);p+=16;in[H+p++]=0;in[H+p++]=0;
 memset(in+H+p,0x5a,D-p);
 assert(nx30_d_env_core_rewrite(in,out,H,D,keys,6,cn,cv,unset,3,128,(char *)values)==0);
 assert(!strcmp(values[1],"192.168.0.1")); assert(!strcmp(values[0],"1"));
 assert(has_entry(out+H,D,"bootcmd=bootipq"));
 assert(has_entry(out+H,D,"untouched=keep-bytes"));
 assert(has_entry(out+H,D,"ipaddr=192.168.1.1"));
 assert(has_entry(out+H,D,"serverip=192.168.1.2"));
 assert(!has_entry(out+H,D,"custom_network=1"));
 { unsigned int crc=crc32_ref(out+H,D), stored; memcpy(out,&crc,H); memcpy(&stored,out,H); assert(stored==crc32_ref(out+H,D)); }
 /* The non-target keys remain byte-identical; the original padding tail survives. */
 assert(out[H+100]==0x5a);
 /* Duplicate whitelisted entries and malformed termination fail closed. */
 { const char dup[]="ipaddr=1.1.1.1\0ipaddr=1.1.1.2\0\0"; memcpy(in+H,dup,sizeof(dup)); }
 assert(nx30_d_env_core_rewrite(in,out,H,D,keys,6,NULL,NULL,NULL,0,128,(char *)values)==-2);
 memset(in+H,'A',D);
 assert(nx30_d_env_core_rewrite(in,out,H,D,keys,6,NULL,NULL,NULL,0,128,(char *)values)==-1);
 return 0;
}
