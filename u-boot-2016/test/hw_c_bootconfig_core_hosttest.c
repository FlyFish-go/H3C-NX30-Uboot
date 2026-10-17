#include "boot_default_rt3000_b_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wr(uint8_t *p, uint32_t v)
{ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void row(uint8_t *b, unsigned n, const char *name, uint32_t selector)
{
 uint8_t *e=b+12+n*20; memset(e,0,20); strncpy((char *)e,name,15); wr(e+16,selector);
}
static void fixture(uint8_t *b, uint32_t age, uint32_t selector)
{
 memset(b,0xff,RT3_BOOTCFG_SIZE); wr(b,RT3_BOOTCFG_MAGIC); wr(b+4,age); wr(b+8,6);
 row(b,0,"APPSBL",0); row(b,1,"QSEE",1); row(b,2,"CDT",1);
 row(b,3,"rootfs_1",0); row(b,4,"ART",0); row(b,5,"rootfs",selector);
 wr(b+332,RT3_BOOTCFG_END);
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc, char **argv)
{
 uint8_t *a=malloc(RT3_BOOTCFG_SIZE), *b=malloc(RT3_BOOTCFG_SIZE), *out=malloc(RT3_BOOTCFG_SIZE);
 struct rt3_bootcfg_view v; uint8_t before[RT3_BOOTCFG_SIZE]; unsigned i, changed=0;
 CHECK(a&&b&&out);
 if (argc == 2) {
  FILE *f=fopen(argv[1],"rb"); CHECK(f);
  CHECK(fread(a,1,RT3_BOOTCFG_SIZE,f)==RT3_BOOTCFG_SIZE);
  CHECK(fread(b,1,RT3_BOOTCFG_SIZE,f)==RT3_BOOTCFG_SIZE);
  CHECK(fgetc(f)==EOF); fclose(f);
 } else {
  fixture(a,1,1); fixture(b,1,1);
 }
 CHECK(!rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE,&v));
 CHECK(v.rootfs_selector==1&&v.age==1);
 memcpy(before,a,RT3_BOOTCFG_SIZE);
 CHECK(!rt3_bootcfg_stage(a,RT3_BOOTCFG_SIZE,0,out));
 for(i=0;i<RT3_BOOTCFG_SIZE;i++) if(a[i]!=out[i]) { changed++; CHECK(i==12+v.rootfs_row*20+16); }
 CHECK(changed==1); /* C's rootfs selector changes; AGE, APPSBL, and full tail are byte-identical. */
 CHECK(!memcmp(a,before,RT3_BOOTCFG_SIZE));
 CHECK(!rt3_bootcfg_stage(out,RT3_BOOTCFG_SIZE,1,b));
 CHECK(!memcmp(a,b,RT3_BOOTCFG_SIZE)); /* rootfs1 -> rootfs -> rootfs1 round-trip */
 CHECK(a[4]==1 && b[4]==1);
 CHECK(!rt3_bootcfg_stage(b,RT3_BOOTCFG_SIZE,0,out)); CHECK(out[4]==1);
 CHECK(rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE-1,&v));
 a[0]=0; CHECK(rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE,&v));
 fixture(a,1,0); fixture(b,2,0); row(b,4,"rootfs",0);
 CHECK(rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE,&v)); /* duplicate rootfs */
 fixture(a,1,0); fixture(b,2,0); memset(a+12, 'A', 16);
 CHECK(rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE,&v)); /* unterminated */
 fixture(a,1,3); fixture(b,2,3);
 CHECK(rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE,&v)); /* unsupported selector */
 fixture(a,1,0); fixture(b,2,1);
 CHECK(rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE,&v)); /* divergent semantics */
 fixture(a,1,0); fixture(b,2,0); wr(b+332,0);
 CHECK(rt3_bootcfg_validate_pair(a,b,RT3_BOOTCFG_SIZE,&v));
 free(out);free(b);free(a);puts("HW-C BOOTCONFIG core validation/staging PASS");return 0;
}
