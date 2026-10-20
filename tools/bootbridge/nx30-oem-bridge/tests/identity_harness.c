/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge_install_identity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void crc_env(uint8_t *e){
    uint32_t c=0xffffffffu;for(size_t i=4;i<0x40000;i++){c^=e[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}c=~c;
    for(unsigned i=0;i<4;i++)e[i]=(uint8_t)(c>>(8*i));
}
static uint8_t *env_make(const char *text,size_t n){
    uint8_t *e=malloc(0x80000);assert(e);memset(e,0xff,0x80000);memset(e+4,0,2);
    assert(n+2<0x40000-4);memcpy(e+4,text,n);e[4+n]=0;e[5+n]=0;crc_env(e);return e;
}
#define ENV(text) env_make((text),sizeof(text)-1)
static const char *source_sha(const char *product){
    if(!strcmp(product,"RC3000"))return "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44";
    if(!strcmp(product,"RT3000"))return "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b";
    if(!strcmp(product,"RW3000"))return "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5";
    if(!strcmp(product,"NX30"))return "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922";
    return "0000000000000000000000000000000000000000000000000000000000000000";
}
static bii_code check_source(const char *board,const char *source_product,const char *source,
                             const char *machid,const char *sha,const uint8_t *env,const char *cmdline,bii_result *r){
    return bii_check_public_candidate(board,source_product,source,machid,sha,env,0x80000,cmdline,r);
}
static bii_code check(const char *board,const char *product,const char *machid,const char *sha,const uint8_t *env,const char *cmdline,bii_result *r){
    return check_source(board,product,source_sha(product),machid,sha,env,cmdline,r);
}
int main(void){
    const char *a="8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb";
    const char *b="912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589";
    const char *c="57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e";
    const char *d="91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362";
    static const char good[]="productname=RC3000\0machid=0x08040001\0bootcmd=run untrusted_old_command\0bootargs=console=ttyMSM0,115200n8 loglevel=3\0";
    uint8_t *env=ENV(good);bii_result r;unsigned n=0;
    bii_code first=check("A","RC3000","0x08040001",a,env,"console=ttyMSM0",&r);
    assert(first==BII_OK&&r.trial_only&&r.have_bootcmd);n++;
    assert(check("A","RC3000","0x08040001",a,env,"machid=0x08040001",&r)==BII_OK);n++;
    assert(check("A","RC3000","0x08040001",b,env,NULL,&r)==BII_POLICY);n++;
    /* B and D share machid: a digest from the other compiled board must fail. */
    assert(check("B","RT3000","0x08040000",d,env,NULL,&r)==BII_POLICY);n++;
    assert(check("D","NX30","0x08040000",b,env,NULL,&r)==BII_POLICY);n++;
    assert(check("A","RC3000","0x08040000",a,env,NULL,&r)==BII_POLICY);n++;
    assert(check("A","OTHER","0x08040001",a,env,NULL,&r)==BII_POLICY);n++;
    assert(check("A","RC3000","0x08040001",a,env,"machid=0x08040000",&r)==BII_CMDLINE_MACHID);n++;
    uint8_t *no_machid=ENV("productname=RC3000\0bootcmd=bootipq\0");
    assert(check("A","RC3000","0x08040001",a,no_machid,NULL,&r)==BII_ENV_MACHID);n++;free(no_machid);
    no_machid=ENV("productname=RT3000\0bootcmd=bootipq\0");
    assert(check("B","RT3000","0x08040000",b,no_machid,NULL,&r)==BII_ENV_MACHID);n++;
    no_machid=ENV("productname=RT3000\0bootcmd=bootipq\0");
    assert(check("C","RT3000","0x08040101",c,no_machid,NULL,&r)==BII_ENV_MACHID);n++;free(no_machid);
    static const char custom[]="productname=RC3000\0machid=8040001\0bootcmd=run oldscript; setenv x y\0bootargs=console=ttyMSM0\0";
    uint8_t *x=ENV(custom);assert(check("A","RC3000","0x8040001",a,x,NULL,&r)==BII_OK);n++;
    static const char root[]="productname=RC3000\0machid=8040001\0bootargs=console=tty root=/dev/old ubi.mtd=old\0";
    free(x);x=ENV(root);assert(check("A","RC3000","8040001",a,x,NULL,&r)==BII_BOOTARGS);n++;
    static const char c_root[]="productname=RT3000\0machid=8040101\0bootargs=console=tty root=/dev/old ubi.mtd=old\0";
    uint8_t *cenv=ENV(c_root);assert(check("C","RT3000","8040101",c,cenv,NULL,&r)==BII_OK);n++;free(cenv);
    static const char d_root[]="productname=NX30\0machid=8040000\0bootargs=console=tty root=/dev/old ubi.mtd=old\0";
    uint8_t *denv=ENV(d_root);assert(check("D","NX30","8040000",d,denv,"console=tty machid=0x08040000",&r)==BII_OK);n++;free(denv);
    static const char d_alias[]="productname=NX30_2i5x\0bootcmd=bootipq\0bootargs=console=ttyMSM0,115200n8 cnss2.bdf_integrated=0x10 cnss2.bdf_pci0=0x60 cnss2.bdf_pci1=0x60 cnss2.skip_radio_bmap=4 loglevel=3\0";
    denv=ENV(d_alias);assert(check("D","NX30","08040000",d,denv,"console=tty machid=8040000",&r)==BII_OK&&!strcmp(r.product,"NX30_2i5x"));n++;
    /* Verified OEM D omits machid in both ENV and cmdline. This exact
     * source-bound record permits absence; it never accepts a conflicting or
     * duplicated value when one is supplied. */
    assert(check("D","NX30","08040000",d,denv,NULL,&r)==BII_OK);n++;
    assert(check("D","NX30","08040000",d,denv,"console=tty machid=8040001",&r)==BII_CMDLINE_MACHID);n++;
    assert(check("D","NX30","08040000",d,denv,"machid=8040000 machid=8040000",&r)==BII_CMDLINE_MACHID);n++;
    free(denv);
    static const char d_alias_with_env_machid[]="productname=NX30_2i5x\0machid=8040000\0bootcmd=bootipq\0";
    denv=ENV(d_alias_with_env_machid);assert(check("D","NX30","08040000",d,denv,NULL,&r)==BII_OK);n++;
    assert(check("D","NX30","08040000",d,denv,"machid=8040001",&r)==BII_CMDLINE_MACHID);n++;
    free(denv);
    static const char d_bad_alias[]="productname=NX30_2i5x-extra\0bootcmd=bootipq\0";
    denv=ENV(d_bad_alias);assert(check("D","NX30","08040000",d,denv,"machid=8040000",&r)==BII_PRODUCT);n++;free(denv);
    static const char d_missing_both[]="productname=NX30_2i5x\0bootcmd=bootipq\0";
    denv=ENV(d_missing_both);assert(check("D","NX30","08040000",d,denv,NULL,&r)==BII_OK);n++;
    assert(check("D","NX30","08040000",d,denv,"machid=8040000",&r)==BII_OK);n++;free(denv);
    static const char d_duplicate_machid[]="productname=NX30_2i5x\0machid=8040000\0machid=8040000\0";
    denv=ENV(d_duplicate_machid);assert(check("D","NX30","08040000",d,denv,"machid=8040000",&r)==BII_ENV_MACHID);n++;free(denv);
    static const char d_wrong_env_machid[]="productname=NX30_2i5x\0machid=8040001\0";
    denv=ENV(d_wrong_env_machid);assert(check("D","NX30","08040000",d,denv,NULL,&r)==BII_ENV_MACHID);n++;free(denv);
    static const char missing[]="productname=RC3000\0machid=8040001\0";
    free(x);x=ENV(missing);assert(check("A","RC3000","8040001",a,x,NULL,&r)==BII_OK&&!r.have_bootcmd&&!r.have_bootargs);n++;
    static const char wrong[]="productname=RC3000\0productname=RC3000\0machid=8040001\0";
    free(x);x=ENV(wrong);assert(check("A","RC3000","8040001",a,x,NULL,&r)==BII_PRODUCT);n++;
    assert(bii_check_public_candidate("A","RC3000",source_sha("RC3000"),"8040001",a,x,0x40000,NULL,&r)==BII_SIZE);n++;
    x[0]^=1;assert(check("A","RC3000","8040001",a,x,NULL,&r)==BII_CRC);n++;
    /* One exact provider per class can be used with each reviewed product
     * source, while ENV product remains bound to that exact source context. */
    const char *boards[]={"A","B","C","D"};
    const char *hashes[]={a,b,c,d};
    const char *machids[]={"08040001","08040000","08040101","08040000"};
    const char *products[]={"RC3000","RT3000","RW3000"};
    for(size_t p=0;p<sizeof(products)/sizeof(products[0]);p++)for(size_t q=0;q<4;q++){
        char envtext[96];int envlen=snprintf(envtext,sizeof(envtext),"productname=%s;machid=%s",products[p],machids[q]);
        assert(envlen>0&&(size_t)envlen<sizeof(envtext));char *sep=strchr(envtext,';');assert(sep);*sep=0;
        uint8_t *pair_env=env_make(envtext,(size_t)envlen);
        assert(check(boards[q],products[p],machids[q],hashes[q],pair_env,NULL,&r)==BII_OK);
        assert(!strcmp(r.product,products[p]));n++;free(pair_env);
    }
    for(size_t q=0;q<4;q++){
        char envtext[96];int envlen=snprintf(envtext,sizeof(envtext),"productname=RX3000;machid=%s",machids[q]);
        assert(envlen>0&&(size_t)envlen<sizeof(envtext));char *sep=strchr(envtext,';');assert(sep);*sep=0;
        uint8_t *pair_env=env_make(envtext,(size_t)envlen);
        assert(check_source(boards[q],"NX30",source_sha("NX30"),machids[q],hashes[q],pair_env,NULL,&r)==BII_OK);
        assert(!strcmp(r.product,"RX3000"));n++;free(pair_env);
    }
    assert(check_source("A","RC3000",source_sha("RT3000"),"08040001",a,env,NULL,&r)==BII_POLICY);n++;
    assert(check_source("A","RX3000","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","08040001",a,env,NULL,&r)==BII_POLICY);n++;
    static const char nx_env_text[]="productname=NX30\0machid=08040000\0";
    uint8_t *nx_env=ENV(nx_env_text);
    assert(check("A","NX30","08040000",a,nx_env,NULL,&r)==BII_POLICY);n++;
    assert(check("D","NX30","08040000",d,nx_env,NULL,&r)==BII_OK);n++;
    assert(check("D","RT3000","08040000",d,nx_env,NULL,&r)==BII_PRODUCT);n++;
    free(nx_env);
    static const char nx_alias_a_text[]="productname=NX30_2i5x\0machid=08040001\0";
    nx_env=ENV(nx_alias_a_text);
    assert(check_source("A","NX30",source_sha("NX30"),"08040001",a,nx_env,NULL,&r)==BII_PRODUCT);n++;free(nx_env);
    static const char rx_wrong_source_text[]="productname=RX3000\0machid=08040001\0";
    uint8_t *rx_env=ENV(rx_wrong_source_text);
    assert(check_source("A","RC3000",source_sha("RC3000"),"08040001",a,rx_env,NULL,&r)==BII_PRODUCT);n++;free(rx_env);
    /* Independent OEM return uses this same source-to-product matrix. The
     * shared NX source is an RX identity on all reviewed routes, while the
     * NX30/2i5x identity remains restricted to the D route. */
    const char *nxsha=source_sha("NX30");
    assert(bii_return_env_product_allowed(nxsha,"NX30","config@mp02.1","NX30"));n++;
    assert(bii_return_env_product_allowed(nxsha,"NX30","config@mp02.1","NX30_2i5x"));n++;
    assert(bii_return_env_product_allowed(nxsha,"NX30","config@mp02.1","RX3000"));n++;
    assert(bii_return_env_product_allowed(nxsha,"NX30","config@mp03.1","RX3000"));n++;
    assert(bii_return_env_product_allowed(nxsha,"NX30","config@mp03.1-c2","RX3000"));n++;
    assert(!bii_return_env_product_allowed(nxsha,"NX30","config@mp03.1","NX30"));n++;
    assert(!bii_return_env_product_allowed(nxsha,"NX30","config@mp03.1-c2","NX30_2i5x"));n++;
    assert(!bii_return_env_product_allowed(nxsha,"RC3000","config@mp02.1","RX3000"));n++;
    assert(!bii_return_env_product_allowed(source_sha("RC3000"),"NX30","config@mp02.1","RX3000"));n++;
    assert(!bii_return_env_product_allowed(nxsha,"NX30","config@unknown","RX3000"));n++;
    printf("PASS %u source-bound identity cases (synthetic ENV; no hardware qualification)\n",n);
    free(env);free(x);return 0;
}
