/* Differential-test harness: runs the desktop bb_export_plan_bin() on a plan
 * described in a text file (test-only; production recipes are compiled in).
 * usage: c_harness PLAN DATA INPUT OUTPUT   -> prints the bb_status number */
#include "bb_export.h"
#include "bb_export_plan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bb_result bb_export_plan_bin(const char*,const char*,const bb_plan_recipe*,size_t,const char*);
static void hex(const char *s, unsigned char *o){ for(int i=0;i<32;i++){ unsigned v; sscanf(s+2*i,"%2x",&v); o[i]=(unsigned char)v; } }
int main(int argc, char **argv){
  if(argc!=5) return 2;
  FILE *p=fopen(argv[1],"r"), *d=fopen(argv[2],"rb"); if(!p||!d) return 2;
  static unsigned char data[1<<26]; size_t dn=fread(data,1,sizeof data,d); fclose(d);
  bb_plan_recipe r; memset(&r,0,sizeof r); char a[80],b[80]; unsigned long long is,os; size_t n;
  if(fscanf(p,"%llu %64s %llu %64s %zu",&is,a,&os,b,&n)!=5) return 2;
  r.id="t"; r.input_size=is; r.output_size=os; hex(a,r.input_sha256); hex(b,r.output_sha256);
  bb_plan_op *ops=calloc(n?n:1,sizeof *ops); size_t used=0;
  for(size_t i=0;i<n;i++){ int k,hb; unsigned long long off,sz;
    if(fscanf(p,"%d %llu %llu %d",&k,&off,&sz,&hb)!=4) return 2;
    ops[i].kind=(bb_plan_kind)k; ops[i].input_offset=off; ops[i].size=sz;
    ops[i].bytes = hb ? data+used : NULL; if(hb) used+=(size_t)sz; if(used>dn) used=dn; }
  r.operations=ops; r.operation_count=n;
  bb_result res=bb_export_plan_bin(argv[3],argv[4],&r,1,"t");
  printf("%d\n",(int)res.status); return 0;
}
