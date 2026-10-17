#!/usr/bin/env python3
"""Compile the exact production MIBIB alias-preservation helper and exercise swapped views."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
src=(root/"failsafe/modules/mibib.c").read_text()
start=src.index("struct h3c_mibib_smem_part")
end=src.index("/* AUTO reload",start)
helper=src[start:end]
c=r'''
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t u32;
''' + helper + r'''
#define C(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static void add(struct h3c_mibib_smem_table*t,int i,const char*n,u32 start,u32 size,u32 attr){strncpy(t->parts[i].name,n,15);t->parts[i].start=start;t->parts[i].size=size;t->parts[i].attr=attr;}
int main(void){
 struct h3c_mibib_smem_table live={0},candidate={0},before;
 live.len=candidate.len=6;
 add(&live,0,"0:APPSBL",50,10,0xaaaa);
 add(&live,1,"0:APPSBL_1",40,10,0xbbbb);
 add(&live,2,"0:CDT",34,2,0xcccc);
 add(&live,3,"0:CDT_1",32,2,0xdddd);
 add(&live,4,"rootfs",392,320,0xeeee);
 add(&live,5,"rootfs_1",72,320,0xffff);
 add(&candidate,0,"0:APPSBL",40,10,0x1111);
 add(&candidate,1,"0:APPSBL_1",50,10,0x2222);
 add(&candidate,2,"0:CDT",32,2,0x3333);
 add(&candidate,3,"0:CDT_1",34,2,0x4444);
 add(&candidate,4,"rootfs",72,320,0x5555);
 add(&candidate,5,"rootfs_1",392,320,0x6666);
 C(h3c_mibib_aliases_equal(&candidate,&live,sizeof(live))==0);
 C(h3c_mibib_preserve_live_aliases(&candidate,&live,sizeof(live))==0);
 C(h3c_mibib_aliases_equal(&candidate,&live,sizeof(live))==1);
 for(int i=0;i<6;i++){C(!strcmp(candidate.parts[i].name,live.parts[i].name));C(candidate.parts[i].start==live.parts[i].start);C(candidate.parts[i].size==live.parts[i].size);}
 C(candidate.parts[0].attr==0x1111 && candidate.parts[5].attr==0x6666);
 before=candidate;candidate.parts[5].size++;C(h3c_mibib_preserve_live_aliases(&candidate,&live,sizeof(live))==-EINVAL);
 C(candidate.parts[5].start==before.parts[5].start);
 puts("HW-A/B MIBIB alias-preservation C harness: PASS (swap source, copy alignment, post-import logical map equality)");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="hwa-mibib-c-") as td:
 p=Path(td)/"t.c";exe=Path(td)/"t";p.write_text(c)
 subprocess.run(["gcc","-std=c11","-Wall","-Wextra","-Werror",str(p),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
