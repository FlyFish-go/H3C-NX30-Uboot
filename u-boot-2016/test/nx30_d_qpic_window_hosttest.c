#define NX30_D_WINDOW_HOST_TEST
#include <assert.h>
#include <nx30_d_qpic_window_core.h>
int main(void)
{
 struct nx30_d_qpic_window_core w={0}; int mtd_a, mtd_b;
 assert(nx30_d_qpic_extent_allowed(0x00900000,0x02800000));
 assert(nx30_d_qpic_extent_allowed(0x03100000,0x02800000));
 assert(nx30_d_qpic_extent_allowed(0x00900000,0x05000000));
 assert(!nx30_d_qpic_extent_allowed(0x03100000,0x05000000));
 assert(!nx30_d_qpic_extent_allowed(0x00900000,0x05020000));
 assert(nx30_d_qpic_extent_allowed(0x00480000,0x00040000));
 assert(nx30_d_qpic_extent_allowed(0x00100000,0x00040000));
 assert(nx30_d_qpic_extent_allowed(0x00140000,0x00040000));
 assert(nx30_d_qpic_extent_allowed(0x00640000,0x00140000));
 assert(!nx30_d_qpic_extent_allowed(0x00500000,0x00140000));
 assert(!nx30_d_qpic_extent_allowed(0x00780000,0x00100000));
 assert(nx30_d_qpic_window_core_open(&w,&mtd_a,0,4,0x00900000,0x02800000)!=0);
 assert(!w.open);
 assert(nx30_d_qpic_window_core_open(&w,&mtd_a,1,6,0x00900000,0x02800000)!=0);
 assert(!w.open);
 /* ECC8 units (C8:41 after R005): the window binds to the ECC it was opened with. */
 assert(nx30_d_qpic_window_core_open(&w,&mtd_a,1,8,0x00900000,0x02800000)==0);
 assert(nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00900000,0x800,8));
 assert(!nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00900000,0x800,4));
 nx30_d_qpic_window_core_close(&w);
 assert(!nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00900000,0x800,8));
 assert(nx30_d_qpic_window_core_open(&w,&mtd_a,1,4,0x00900000,0x02800000)==0);
 /* A data page write and a block erase inside the opened A-slot aperture pass. */
 assert(nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00900000,0x800,4));
 assert(nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00920000,0x20000,4));
 assert(!nx30_d_qpic_window_core_allows(&w,&mtd_b,0x00900000,0x800,4));
 assert(!nx30_d_qpic_window_core_allows(&w,&mtd_a,0x008ff800,0x800,4));
 assert(!nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00900000,0x800,8));
 assert(nx30_d_qpic_window_core_open(&w,&mtd_a,1,4,0x00640000,0x00140000)!=0);
 nx30_d_qpic_window_core_close(&w);
 assert(!nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00900000,0x800,4));
 assert(nx30_d_qpic_window_core_open(&w,&mtd_a,1,4,0x00640000,0x00140000)==0);
 assert(nx30_d_qpic_window_core_allows(&w,&mtd_a,0x00640000,0x20000,4));
 nx30_d_qpic_window_core_close(&w);
 return 0;
}
