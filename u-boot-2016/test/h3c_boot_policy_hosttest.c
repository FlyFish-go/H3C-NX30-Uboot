/* SPDX-License-Identifier: GPL-2.0+ */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <h3c_boot_policy.h>
struct fixture { int v[20]; unsigned n, calls, delays, gpio; };
static int read_sample(void *ctx, unsigned int gpio)
{
 struct fixture *f = ctx;
 assert(f->calls < f->n);
 f->gpio = gpio;
 return f->v[f->calls++];
}
static void delay_sample(void *ctx) { ((struct fixture *)ctx)->delays++; }
static void sample(enum h3c_boot_board b, int mode, enum h3c_reset_state expected)
{
 struct fixture f = {{0},20,0,0,0};
 struct h3c_reset_io io = { &f, read_sample, delay_sample };
 unsigned i;
 for(i=0;i<20;i++) f.v[i] = mode==0 ? 0 : mode==1 ? 1 : (int)(i%2);
 if(mode==3) f.v[2]=-5;
 if(mode==4) for(i=3;i<20;i++) f.v[i]=1;
 if(mode==5) for(i=3;i<20;i++) f.v[i]=0;
 if(mode==6) f.v[0]=2;
 assert(h3c_boot_sample_reset(b,&io)==expected);
 assert(f.gpio==(b==H3C_BOOT_A||b==H3C_BOOT_C?27U:34U));
 assert(f.calls<=20 && f.delays+1==f.calls);
}
int main(void)
{
 enum h3c_boot_board b;
 struct h3c_boot_inputs in = {1,0,1,H3C_RESET_RELEASED};
 int rc;
 for(b=H3C_BOOT_A;b<=H3C_BOOT_D;b++) {
  sample(b,0,H3C_RESET_HELD); sample(b,1,H3C_RESET_RELEASED);
  sample(b,2,H3C_RESET_UNKNOWN); sample(b,3,H3C_RESET_UNKNOWN);
  sample(b,4,H3C_RESET_RELEASED); sample(b,5,H3C_RESET_HELD);
  sample(b,6,H3C_RESET_UNKNOWN);
 }
 assert(h3c_boot_sample_reset((enum h3c_boot_board)99,0)==H3C_RESET_UNKNOWN);
 assert(h3c_boot_sample_reset(H3C_BOOT_A,0)==H3C_RESET_UNKNOWN);
 assert(h3c_boot_decide(&in)==H3C_BOOT_SELECTED);
 in.reset=H3C_RESET_HELD; assert(h3c_boot_decide(&in)==H3C_BOOT_WEB);
 in.reset=H3C_RESET_UNKNOWN; assert(h3c_boot_decide(&in)==H3C_BOOT_WEB);
 in.reset=H3C_RESET_RELEASED; in.format_ready=0;
 assert(h3c_boot_decide(&in)==H3C_BOOT_WEB);
 in.release_system=0; assert(h3c_boot_decide(&in)==H3C_BOOT_LEGACY);
 in.forced_web=1; assert(h3c_boot_decide(&in)==H3C_BOOT_WEB);
 in.release_system=1; assert(h3c_boot_decide(&in)==H3C_BOOT_WEB);
 assert(h3c_boot_decide(0)==H3C_BOOT_WEB);
 for(rc=-4096;rc<4096;rc++) assert(h3c_boot_load_failed(rc)==(rc!=0));
 assert(h3c_boot_load_failed(INT_MIN)); assert(h3c_boot_load_failed(INT_MAX));
 puts("PASS: four board reset/debounce/errors, forced web, format gate, all nonzero load codes");
 return 0;
}
