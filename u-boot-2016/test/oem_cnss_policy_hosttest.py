#!/usr/bin/env python3
"""Exercise the actual OEM policy/composition functions with host stubs."""
import argparse, os, subprocess, tempfile
from pathlib import Path

PRE = r"""
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_SYS_CBSIZE 512
#define CMD_RET_SUCCESS 0
#define CMD_RET_FAILURE 1
#define FIT_BOOTARGS_PROP "bootargs"
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define getenv host_getenv
#define setenv host_setenv
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d failed: %s\n",__LINE__,#x); exit(1); } } while (0)
static char base[1024], root[1024];
static const char *fit;
static int failure, writes, missing_base;
static struct { struct { unsigned long long offset, size; } rootfs; } boot_flash, *sfi = &boot_flash;
static char *host_getenv(const char *k) {
 if (!strcmp(k,"bootargs")) return missing_base ? NULL : base;
 if (!strcmp(k,"fsbootargs")) return root[0] ? root : NULL;
 return NULL;
}
static int host_setenv(const char *k,const char *v) {
 CHECK(!strcmp(k,"bootargs")); writes++;
 if(failure) return -1;
 CHECK(strlen(v)<sizeof(base));strcpy(base,v); missing_base=0; return 0;
}
static const void *fdt_getprop(void *a,int n,const char *k,int *l) {
 (void)a;(void)n;(void)k;*l=fit ? strlen(fit)+1 : 0;return fit;
}
"""
MAIN = r"""
static int count(const char *s,const char *key) {
 int n=0;size_t z=strlen(key);
 while((s=strstr(s,key))) {n++;s+=z;} return n;
}
static void reset(const char *b,const char *r,const char *f) {
 strcpy(base,b);strcpy(root,r);fit=f;failure=writes=missing_base=0;
#ifdef CONFIG_H3C_HW_C_OEM_CNSS_SKIP4
 h3c_hw_c_bootargs_snapshot_valid=0;
#endif
}
int main(void) {
 enum h3c_hw_c_fit_profile p;
 CHECK(!h3c_hw_c_classify_fit_profile(0,1,1,"config@mp03.1-c2",&p) && p==H3C_HW_C_PROFILE_OEM);
 CHECK(!h3c_hw_c_classify_fit_profile(1,1,1,"config@mp03.1-c2",&p) && p==H3C_HW_C_PROFILE_NATIVE);
 CHECK(h3c_hw_c_classify_fit_profile(0,1,0,"config@mp03.1-c2",&p) && p==H3C_HW_C_PROFILE_INVALID);
 CHECK(h3c_hw_c_classify_fit_profile(0,0,1,"config@mp02.1",&p) && p==H3C_HW_C_PROFILE_INVALID);
 CHECK(h3c_hw_c_classify_fit_profile(1,0,1,"config@mp03.1-c2",&p));
 CHECK(h3c_hw_c_classify_fit_profile(0,1,1,NULL,&p));
 reset("console=ttyMSM0 root=/old panic=0","ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs",NULL);
 CHECK(!update_bootargs(NULL,0,1));CHECK(writes==1);
#ifdef CONFIG_H3C_HW_C_OEM_CNSS_SKIP4
 CHECK(count(base,"cnss2.skip_radio_bmap=4")==1);
 CHECK(!update_bootargs(NULL,0,1)); CHECK(count(base,"cnss2.skip_radio_bmap=4")==1 && count(base,"rootwait")==1);
 strcpy(root,"ubi.mtd=rootfs_1 ubi.block=0,3 root=/dev/ubiblock0_3 rootfstype=squashfs panic=0");
 CHECK(!update_bootargs(NULL,0,0));CHECK(!strstr(base,"cnss2.skip_radio_bmap"));
 CHECK(strstr(base,"ubi.mtd=rootfs_1") && !strstr(base,"root=mtd:"));
 strcpy(root,"ubi.mtd=rootfs root=mtd:ubi_rootfs rootfstype=squashfs");
 CHECK(!update_bootargs(NULL,0,1)); CHECK(count(base,"cnss2.skip_radio_bmap=4")==1);
#else
 CHECK(!strstr(base,"cnss2.skip_radio_bmap"));
#endif
 const char *values[]={"0","4","6","0x4","0x6"};
 for(unsigned int i=0;i<ARRAY_SIZE(values);i++) {
  char explicit[128];snprintf(explicit,sizeof(explicit),"console=x cnss2.skip_radio_bmap=%s",values[i]);
  reset(explicit,"root=/new",NULL);CHECK(!update_bootargs(NULL,0,1));
  CHECK(strstr(base,explicit) && count(base,"cnss2.skip_radio_bmap=")==1);
 }
 reset("console=x","root=/new","\tcnss2.skip_radio_bmap=0x4\r\n");
 CHECK(!update_bootargs(NULL,0,1));CHECK(count(base,"cnss2.skip_radio_bmap=")==1);
#ifdef CONFIG_H3C_HW_C_OEM_CNSS_SKIP4
 const char *bad[]={"cnss2.skip_radio_bmap", "cnss2.skip_radio_bmap=", "cnss2.skip_radio_bmap=4 cnss2.skip_radio_bmap=6"};
 for(unsigned int i=0;i<ARRAY_SIZE(bad);i++) {
  reset(bad[i],"root=/new",NULL);char before[1024];strcpy(before,base);
  CHECK(update_bootargs(NULL,0,1)==CMD_RET_FAILURE && !writes && !strcmp(base,before));
 }
 reset("cnss2.skip_radio_bmap=4","root=/new","cnss2.skip_radio_bmap=6");
 CHECK(update_bootargs(NULL,0,1)==CMD_RET_FAILURE && !writes);
 reset("console=x","root=/new",NULL);CHECK(!update_bootargs(NULL,0,1));
 strcpy(base,"console=x cnss2.skip_radio_bmap=6");CHECK(!update_bootargs(NULL,0,0));
 CHECK(strstr(base,"cnss2.skip_radio_bmap=6") && !strstr(base,"cnss2.skip_radio_bmap=4"));
 reset("console=x","root=/new",NULL);failure=1;
 CHECK(update_bootargs(NULL,0,1)==CMD_RET_FAILURE && !strcmp(base,"console=x"));
 failure=0;CHECK(!update_bootargs(NULL,0,0));CHECK(!strstr(base,"cnss2.skip_radio_bmap"));
 reset("console=x","root=/new",NULL);CHECK(!update_bootargs(NULL,0,1));
 char before[1024];strcpy(before,base);memset(root,'x',sizeof(root)-1);root[sizeof(root)-1]=0;writes=0;
 CHECK(update_bootargs(NULL,0,0)==CMD_RET_FAILURE && !writes && !strcmp(base,before));
 strcpy(root,"root=/native");CHECK(!update_bootargs(NULL,0,0));CHECK(!strstr(base,"cnss2.skip_radio_bmap"));
 reset("","root=/new",NULL);missing_base=1;CHECK(!update_bootargs(NULL,0,1));
 CHECK(count(base,"cnss2.skip_radio_bmap=4")==1);
#endif
 puts("OEM_CNSS_POLICY_HOSTTEST_PASS");return 0;
}
"""

def main():
 a=argparse.ArgumentParser();a.add_argument('--source',required=True,type=Path);a.add_argument('--output-dir',required=True,type=Path);a=a.parse_args()
 s=a.source.read_text()
 classifier=s[s.index('enum h3c_hw_c_fit_profile {'):s.index('\n#endif',s.index('enum h3c_hw_c_fit_profile {'))]
 filters=s[s.index('static int h3c_hw_c_root_token('):s.index('#ifdef CONFIG_H3C_NAND_ECC_COMPAT\nstatic int h3c_normalize_nand_bootargs')]
 # Include the production region's opening conditional to balance its endif.
 filters='#if defined(CONFIG_H3C_HW_C_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY) || defined(CONFIG_H3C_HW_D_RAM_WEB)\n'+filters
 update=s[s.index('/* Append the booted physical rootfs offset'):s.index('/*\n * Set the root device',s.index('static int update_bootargs('))]
 a.output_dir.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='cnss-policy-') as d:
  c=Path(d)/'test.c';c.write_text('\n'.join([PRE,classifier,filters,update,MAIN]))
  for mode,defines in [('enabled',['CONFIG_H3C_HW_C_PRODUCTION_PORT','CONFIG_H3C_HW_C_OEM_CNSS_SKIP4']),('disabled',['CONFIG_H3C_HW_C_PRODUCTION_PORT'])]:
   exe=Path(d)/mode
   subprocess.run(['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-D'+x for x in defines],str(c),'-o',str(exe)],check=True)
   env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
   r=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
   (a.output_dir/(mode+'.txt')).write_text(r.stdout+r.stderr)
   if r.returncode:raise SystemExit(r.stderr or r.stdout)
   print(mode,r.stdout.strip())
if __name__=='__main__': main()
