#!/usr/bin/env python3
import json
import subprocess
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
api=(root/'lib/ipq_api.c').read_text()
network=api[api.index('void do_network_check(void)'):api.index('// =============================================================================\n// 闪存设备检测')]
start=network.index('#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B')
end=network.index('\n#endif',start)+len('\n#endif')
block=network[start:end]
legacy='setenv mtdids nand0=nand0 && setenv mtdparts mtdparts=nand0:0x02800000@0x03100000(c-native) && ubi part c-native && ubi read 0x44000000 kernel && bootm 0x44000000#config@mp03.1-c2'
near_miss=legacy.replace('mp03.1-c2','mp03.1-c3')
prefix=r'''#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CONFIG_H3C_HW_C_PRODUCTION_PORT 1
static char command[512], delay[32], ethact[32];
static int have_command, have_delay, have_ethact;
static const char *mock_getenv(const char *key) {
 if (!strcmp(key,"bootcmd")) return have_command ? command : NULL;
 if (!strcmp(key,"bootdelay")) return have_delay ? delay : NULL;
 if (!strcmp(key,"ethact")) return have_ethact ? ethact : NULL;
 return NULL;
}
static int mock_setenv(const char *key,const char *value) {
 char *dst=NULL; size_t cap=0; int *present=NULL;
 if (!strcmp(key,"bootcmd")) { dst=command; cap=sizeof command; present=&have_command; }
 else if (!strcmp(key,"bootdelay")) { dst=delay; cap=sizeof delay; present=&have_delay; }
 else if (!strcmp(key,"ethact")) { dst=ethact; cap=sizeof ethact; present=&have_ethact; }
 else return -1;
 if (!value) { dst[0]=0; *present=0; return 0; }
 snprintf(dst,cap,"%s",value); *present=1; return 0;
}
#define getenv mock_getenv
#define setenv mock_setenv
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x); return 1; } } while (0)
static void migrate(void) {
''' 
suffix=r'''
}
static void reset(void) { command[0]=delay[0]=ethact[0]=0; have_command=have_delay=have_ethact=0; }
int main(void) {
 reset(); setenv("bootcmd",LEGACY); setenv("bootdelay","3"); migrate();
 CHECK(!strcmp(command,"bootipq") && !strcmp(delay,"3"));
 reset(); setenv("bootcmd","httpd"); setenv("bootdelay","0"); migrate();
 CHECK(!strcmp(command,"bootipq") && !strcmp(delay,"3"));
 reset(); setenv("bootcmd","httpd"); setenv("bootdelay","9"); migrate();
 CHECK(!strcmp(command,"bootipq") && !strcmp(delay,"9"));
 reset(); setenv("bootcmd",NEAR_MISS); setenv("bootdelay","3"); migrate();
 CHECK(!strcmp(command,NEAR_MISS) && !strcmp(delay,"3"));
 reset(); setenv("bootcmd","run operator_recovery"); setenv("bootdelay","0"); migrate();
 CHECK(!strcmp(command,"run operator_recovery") && !strcmp(delay,"0"));
 reset(); setenv("bootdelay","3"); migrate();
 CHECK(!have_command && !strcmp(delay,"3"));
 puts("C production exact legacy bootcmd RAM migration: PASS"); return 0;
}
'''
source=prefix+block+'\n'+suffix
source=source.replace('LEGACY',json.dumps(legacy)).replace('NEAR_MISS',json.dumps(near_miss))
with tempfile.TemporaryDirectory(prefix='hw-c-bootcmd-migration-') as td:
 c=Path(td)/'test.c'; exe=Path(td)/'test'; c.write_text(source)
 subprocess.run(['gcc','-std=gnu99','-Wall','-Wextra','-Werror','-Wno-unused-function',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
