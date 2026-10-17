#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
src=(Path(__file__).resolve().parents[1]/"board/qca/arm/common/failsafe.c").read_text()
def ex(name):
 a=src.index(name); b=src.index("{",a); d=0
 for i in range(b,len(src)):
  d+= (src[i]=="{")-(src[i]=="}")
  if d==0:return src[a:i+1]
prod=ex("struct recovery_env_snapshot")+";\n"+"\n".join(ex(n) for n in ("static int recovery_env_save","static int recovery_env_restore","static int recovery_run_diagnostic_boot"))
h=r"""
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
struct E{const char*n;char v[1024];int p;};static struct E e[]={{"bootargs"},{"initrd_high"},{"fdt_high"}};
static int calls,fail,seen,rc;static struct E*F(const char*n){for(int i=0;i<3;i++)if(!strcmp(e[i].n,n))return e+i;return 0;}
static const char*G(const char*n){struct E*x=F(n);return x&&x->p?x->v:0;}
static int S(const char*n,const char*v){struct E*x=F(n);if(!x||++calls==fail)return -1;if(!v){x->p=0;x->v[0]=0;}else{strcpy(x->v,v);x->p=1;}return 0;}
static int run_command(const char*c,int f){(void)f;if(strcmp(c,"bootm 0x4e000000#config@mp03.1")||strcmp(G("bootargs"),"console=ttyMSM0,115200,n8 rdinit=/init")||strcmp(G("initrd_high"),"0x48000000")||strcmp(G("fdt_high"),"0x48000000"))return -2;seen++;return rc;}
static size_t strlcpy(char*d,const char*s,size_t n){size_t z=strlen(s),k=z<n-1?z:n-1;if(n){memcpy(d,s,k);d[k]=0;}return z;}
#define getenv G
#define setenv S
"""
h+=prod+r"""
#undef getenv
#undef setenv
#define C(x) do{if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static void put(int i,const char*v){e[i].p=!!v;if(v)strcpy(e[i].v,v);else e[i].v[0]=0;}
int main(void){int r;put(0,"oldargs");put(1,"oldinit");put(2,"oldfdt");rc=7;seen=calls=0;fail=0;
r=recovery_run_diagnostic_boot("bootm 0x4e000000#config@mp03.1");C(r==7&&seen==1&&!strcmp(G("bootargs"),"oldargs")&&!strcmp(G("initrd_high"),"oldinit")&&!strcmp(G("fdt_high"),"oldfdt"));
put(0,0);put(1,0);put(2,0);rc=0;seen=calls=0;r=recovery_run_diagnostic_boot("bootm 0x4e000000#config@mp03.1");C(r==0&&seen==1&&!G("bootargs")&&!G("initrd_high")&&!G("fdt_high"));
put(0,"a");put(1,"i");put(2,"f");seen=calls=0;fail=2;r=recovery_run_diagnostic_boot("bootm 0x4e000000#config@mp03.1");C(r==-ENOMEM&&!seen&&!strcmp(G("bootargs"),"a")&&!strcmp(G("initrd_high"),"i")&&!strcmp(G("fdt_high"),"f"));C(recovery_run_diagnostic_boot(0)==-EINVAL);
puts("volatile recovery boot environment: PASS (present/absent restore, exact temporary values, setter rollback)");}
"""
with tempfile.TemporaryDirectory() as d:
 c=Path(d)/"t.c";x=Path(d)/"t";c.write_text(h)
 subprocess.run(["gcc","-std=gnu11","-O2","-Wall","-Werror",str(c),"-o",str(x)],check=True);subprocess.run([str(x)],check=True)
