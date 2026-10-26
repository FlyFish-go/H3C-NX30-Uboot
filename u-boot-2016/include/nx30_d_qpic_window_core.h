/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_QPIC_WINDOW_CORE_H__
#define __NX30_D_QPIC_WINDOW_CORE_H__
#ifdef NX30_D_WINDOW_HOST_TEST
#include <stdint.h>
typedef uint64_t nx30_d_core_u64;
#else
#include <linux/types.h>
typedef u64 nx30_d_core_u64;
#endif
struct nx30_d_qpic_window_core {
 const void *mtd;
 nx30_d_core_u64 start, end;
 int open;
};
static inline int nx30_d_qpic_extent_allowed(nx30_d_core_u64 offset,
                                              nx30_d_core_u64 size)
{
 return (offset==0x00900000ULL && size==0x02800000ULL) ||
        (offset==0x03100000ULL && size==0x02800000ULL) ||
        /* PTABLE single layout: one 80 MiB rootfs replaces both slots. */
        (offset==0x00900000ULL && size==0x05000000ULL) ||
        (offset==0x00480000ULL && size==0x00040000ULL) ||
        (offset==0x00100000ULL && size==0x00040000ULL) ||
        (offset==0x00140000ULL && size==0x00040000ULL) ||
        (offset==0x00640000ULL && size==0x00140000ULL)
#ifdef CONFIG_H3C_HW_D_PRODUCTION_PORT
        || (offset==0x00480000ULL && size==0x00080000ULL)
        || (offset==0x00780000ULL && size==0x00100000ULL)
        || (offset==0x00400000ULL && size==0x00040000ULL)
        || (offset==0x00440000ULL && size==0x00040000ULL)
        || (offset==0x00500000ULL && size==0x00140000ULL)
        || (offset==0x00080000ULL && size==0x00080000ULL)
#endif
        ;
}
static inline int nx30_d_qpic_window_core_open(
 struct nx30_d_qpic_window_core *w, const void *mtd, int profile_valid,
 unsigned int ecc_bits, nx30_d_core_u64 offset, nx30_d_core_u64 size)
{
 if (!w || w->open || !mtd || !profile_valid || ecc_bits!=4 ||
     !nx30_d_qpic_extent_allowed(offset,size) ||
     offset>(nx30_d_core_u64)-1-size) return -1;
 w->mtd=mtd; w->start=offset; w->end=offset+size; w->open=1;
 return 0;
}
static inline void nx30_d_qpic_window_core_close(
 struct nx30_d_qpic_window_core *w)
{
 if (w) { w->mtd=0; w->start=0; w->end=0; w->open=0; }
}
static inline int nx30_d_qpic_window_core_allows(
 const struct nx30_d_qpic_window_core *w, const void *mtd,
 nx30_d_core_u64 offset, nx30_d_core_u64 size, unsigned int ecc_bits)
{
 return w && w->open && w->mtd && w->mtd==mtd && size && ecc_bits==4 &&
        offset>=w->start && offset<=w->end && size<=w->end-offset;
}
#endif
