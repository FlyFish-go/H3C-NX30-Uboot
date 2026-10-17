/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_BC_CORE_H__
#define __NX30_D_BC_CORE_H__
#ifdef NX30_D_BC_HOST_TEST
#include <stdint.h>
#else
#include <linux/types.h>
#endif
/* BC1 is programmed and completely verified before BC0 is attempted. Failures
 * stop immediately; the caller retains both full peer backups for evidence. */
static inline int nx30_d_bc_core_commit_pair(
 void *cookie,
 int (*program_verify)(void *cookie,unsigned int copy,const void *bytes),
 const void *candidate)
{
 int ret;
 if (!program_verify || !candidate) return -22;
 ret=program_verify(cookie,1,candidate);
 if (ret) return ret;
 return program_verify(cookie,0,candidate);
}
#endif
