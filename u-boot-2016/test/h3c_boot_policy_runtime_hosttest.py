#!/usr/bin/env python3
"""Compile real runtime + exact do_bootipq body, with observable IO stubs."""
from pathlib import Path
import subprocess,tempfile,json
r=Path(__file__).resolve().parents[1]
common=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint32_t u32;
typedef int cmd_tbl_t;
#define CMD_RET_SUCCESS 0
#define CMD_RET_FAILURE 1
#define CONFIG_IPQ_FDT_HIGH 0x4a000000
#define STR_(x) #x
#define MK_STR(x) STR_(x)
int setenv(const char *,const char *);
int run_command(const char *,int);
void udelay(unsigned long);
u32 readl(const void *);
void writel(u32,void *);
int gpio_get_value(unsigned int);
int h3c_nand_ecc_ready(void);
int rt3000b_prepare_boot_slot(int,char *const []);
int do_boot_unsignedimg(cmd_tbl_t *,int,int,char *const []);
'''
driver=r'''
#include <common.h>
#include <h3c_boot_policy.h>
static int do_bootipq(cmd_tbl_t *,int,int,char *const []);
static int web,boot,prepare,load,ready=1,levels[20],reads,delays;
static int prep_error,load_error,fdt_error,lan_error,nest,bad_config;
static int dispatch_error;
static unsigned gpio_seen;
static u32 cfg;
int setenv(const char *k,const char *v) {
 assert(strcmp(k,"bootcmd")&&strcmp(k,"bootargs"));
 if(!strcmp(k,"ipaddr")) assert(!strcmp(v,"192.168.10.1"));
 if(!strcmp(k,"ethact")||!strcmp(k,"ethprime")) assert(!strcmp(v,"eth1"));
 return lan_error;
}
int run_command(const char *s,int flags) {
 (void)flags;
 if(!strcmp(s,"bootipq")) { char *arg[]={"bootipq"}; boot++; if(dispatch_error) return dispatch_error; return do_bootipq(0,0,1,arg); }
 if(!strcmp(s,"httpd")) { web++; if(nest) h3c_boot_recovery(); return 0; }
 assert(!strncmp(s,"setenv fdt_high ",16)); return fdt_error;
}
void udelay(unsigned long us) { assert(us==10000); delays++; }
u32 readl(const void *p) { (void)p; return bad_config ? 0xffffffffU : cfg; }
void writel(u32 v,void *p) { (void)p; cfg=v; }
int gpio_get_value(unsigned gpio) { assert(reads<20); gpio_seen=gpio; return levels[reads++]; }
int h3c_nand_ecc_ready(void) { return ready; }
int rt3000b_prepare_boot_slot(int argc,char *const argv[]) {
 assert(argc==1&&!strcmp(argv[0],"bootipq")); prepare++; return prep_error;
}
int do_boot_unsignedimg(cmd_tbl_t *c,int f,int argc,char *const argv[]) {
 (void)c;(void)f;(void)argc;(void)argv; load++; return load_error;
}
static void reset(void) {
 unsigned i; web=boot=prepare=load=reads=delays=0;
 dispatch_error=0; prep_error=load_error=fdt_error=lan_error=nest=bad_config=0; ready=1;
 for(i=0;i<20;i++) levels[i]=1;
}
int main(void) {
 int i; char *arg[]={"bootipq"};
#ifdef TEST_FORCED_WEB
 reset(); h3c_release_autoboot(); assert(web==1&&boot==0&&reads==0);
#else
 reset(); h3c_release_autoboot();
 assert(boot==1&&prepare==1&&load==1&&web==0&&reads==5&&delays==4);
#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)||defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
 assert(gpio_seen==27);
#else
 assert(gpio_seen==34);
#endif
 reset(); for(i=0;i<20;i++) levels[i]=0;
 h3c_release_autoboot(); assert(web==1&&boot==0);
 reset(); for(i=0;i<20;i++) levels[i]=i%2;
 h3c_release_autoboot(); assert(web==1&&boot==0&&reads==20);
 reset(); levels[2]=-5; h3c_release_autoboot(); assert(web==1&&boot==0);
 reset(); bad_config=1; h3c_release_autoboot(); assert(web==1&&boot==0&&reads==0);
 reset(); dispatch_error=-19; h3c_release_autoboot(); assert(web==1&&boot==1&&prepare==0);
 reset(); ready=0; h3c_release_autoboot(); assert(web==1&&boot==0);
 reset(); lan_error=1; levels[0]=levels[1]=levels[2]=levels[3]=levels[4]=0;
 h3c_release_autoboot(); assert(web==0&&boot==0);
 reset(); nest=1; for(i=0;i<20;i++) levels[i]=0;
 h3c_release_autoboot(); assert(web==1&&boot==0);
#endif
 for(i=-20;i<=20;i++) {
  reset(); prep_error=i; h3c_release_autoboot();
#ifndef TEST_FORCED_WEB
  assert(web==(i!=0)&&boot==1&&load==(i==0));
#else
  assert(web==1&&boot==0);
#endif
  reset(); load_error=i; do_bootipq(0,0,1,arg); assert(web==(i!=0)&&load==1);
 }
 reset(); fdt_error=-7; do_bootipq(0,0,1,arg); assert(web==1&&load==0);
 reset(); ready=0; do_bootipq(0,0,1,arg); assert(web==1&&prepare==0);
 reset(); nest=1; load_error=-5; do_bootipq(0,0,1,arg); assert(web==1&&load==1);
 reset(); load_error=-5; do_bootipq(0,0,1,arg); do_bootipq(0,0,1,arg); assert(web==2&&load==2);
 puts("PASS real runtime + actual do_bootipq: selected boot, input errors, early/nonzero failures, recursion");
 return 0;
}
'''
src=(r/'board/qca/arm/common/cmd_bootqca.c').read_text()
start=src.index('static int do_bootipq(')
end=src.index('\n\treturn ret;\n}',start)+len('\n\treturn ret;\n}')
body=src[start:end]
results=[]
with tempfile.TemporaryDirectory(prefix='h3c-boot-runtime-') as td:
 p=Path(td)
 headers={'common.h':common,'command.h':'#include <common.h>\n',
  'asm/io.h':'#include <common.h>\n',
  'asm/arch-qca-common/gpio.h':'#include <common.h>\n#define GPIO_CONFIG_ADDR(gpio) ((uintptr_t)(gpio)*0x1000U)\n',
  'dt-bindings/qcom/gpio-ipq5018.h':'#define GPIO_PULL_UP 3U\n#define GPIO_INPUT 0U\n#define GPIO_8MA 3U\n#define GPIO_OE_DISABLE 0U\n',
  'h3c_nand_ecc.h':'int h3c_nand_ecc_ready(void);\n'}
 for name,text in headers.items():
  q=p/name;q.parent.mkdir(parents=True,exist_ok=True);q.write_text(text)
 (p/'driver.c').write_text(driver+'\n'+body)
 profiles={
  'A':['CONFIG_TARGET_IPQ5018_H3C_HW_A','CONFIG_H3C_HW_A_PRODUCTION_PORT'],
  'B':['CONFIG_TARGET_IPQ5018_H3C_RT3000_B'],
  'C':['CONFIG_TARGET_IPQ5018_H3C_HW_C','CONFIG_H3C_HW_C_PRODUCTION_PORT'],
  'D':['CONFIG_TARGET_IPQ5018_H3C_HW_D','CONFIG_H3C_HW_D_RAM_WEB','CONFIG_H3C_HW_D_RAM_READONLY','CONFIG_H3C_HW_D_PRODUCTION_PORT'],
  'A-RAM':['CONFIG_TARGET_IPQ5018_H3C_HW_A','CONFIG_H3C_HW_A_RAM_READONLY','TEST_FORCED_WEB'],
  'C-RAM':['CONFIG_TARGET_IPQ5018_H3C_HW_C','CONFIG_H3C_HW_C_RAM_READONLY','TEST_FORCED_WEB'],
  'D-SERVICE-RAM':['CONFIG_TARGET_IPQ5018_H3C_HW_D','CONFIG_H3C_HW_D_RAM_WEB','CONFIG_H3C_HW_D_PRODUCTION_PORT','CONFIG_H3C_HW_D_SERVICE_RAM','TEST_FORCED_WEB'],
  'D-RAM':['CONFIG_TARGET_IPQ5018_H3C_HW_D','CONFIG_H3C_HW_D_RAM_WEB','CONFIG_H3C_HW_D_RAM_READONLY','TEST_FORCED_WEB'],
  'D-NOR':['CONFIG_TARGET_IPQ5018_H3C_HW_D','CONFIG_H3C_HW_D_RAM_WEB','CONFIG_H3C_HW_D_NOR_RESCUE','TEST_FORCED_WEB'],
 }
 for name,defs in profiles.items():
  cmd=['cc','-std=c99','-Wall','-Wextra','-Werror','-DCONFIG_H3C_RELEASE_SYSTEM_DEFAULT','-DCONFIG_H3C_NAND_ECC_COMPAT','-DCONFIG_DISABLE_SIGNED_BOOT']+['-D'+v for v in defs]+['-I'+str(p),'-I'+str(r/'include'),str(r/'lib/h3c_boot_policy.c'),str(r/'lib/h3c_boot_policy_core.c'),str(p/'driver.c'),'-o',str(p/'test')]
  subprocess.run(cmd,check=True)
  out=subprocess.run([str(p/'test')],check=True,capture_output=True,text=True)
  results.append({'profile':name,'status':'PASS','stdout':out.stdout})
print(json.dumps(results,indent=2))
