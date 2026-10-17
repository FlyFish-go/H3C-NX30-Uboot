#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <nx30_d_bootargs.h>

int main(void)
{
 char out[1024], input[1200];
 struct { char text[32]; unsigned char guard[16]; } tiny;
 assert(!nx30_d_ecc_bootargs("console=ttyMSM0 cnss2.skip_radio_bmap=6", out, sizeof(out), 128, 4));
 assert(!strcmp(out,"console=ttyMSM0 cnss2.skip_radio_bmap=6 nandspare=128 nandeccbits=4"));
 assert(!nx30_d_ecc_bootargs("nandspare=128 nandeccbits=8 rootwait",out,sizeof(out),128,8));
 assert(!strcmp(out,"rootwait nandspare=128 nandeccbits=8"));
 assert(!nx30_d_ecc_bootargs("nandspare=128 nandspare=128\tnandeccbits=4 nandeccbits=4",out,sizeof(out),128,4));
 assert(!strcmp(out,"nandspare=128 nandeccbits=4"));
 assert(nx30_d_ecc_bootargs("nandspare=128 nandeccbits=8",out,sizeof(out),128,4)==-EINVAL);
 assert(nx30_d_ecc_bootargs("nandspare=64 nandeccbits=4",out,sizeof(out),128,4)==-EINVAL);
 assert(nx30_d_ecc_bootargs("nandeccbits=8garbage",out,sizeof(out),128,8)==-EINVAL);
 assert(nx30_d_ecc_bootargs("nandeccbits=",out,sizeof(out),128,4)==-EINVAL);
 assert(nx30_d_ecc_bootargs("",out,sizeof(out),128,0)==-EINVAL);
 memset(input,'x',sizeof(input)-1);input[sizeof(input)-1]=0;
 assert(nx30_d_ecc_bootargs(input,out,sizeof(out),128,4)==-ENOSPC);
 memset(&tiny,0xa5,sizeof(tiny));
 assert(nx30_d_ecc_bootargs("console=ttyMSM0",tiny.text,sizeof(tiny.text),128,4)==-ENOSPC);
 for (unsigned int i=0;i<sizeof(tiny.guard);i++) assert(tiny.guard[i]==0xa5);
 puts("NX30 D ECC4/ECC8, duplicates, conflicts and bounded output: PASS");
 return 0;
}
