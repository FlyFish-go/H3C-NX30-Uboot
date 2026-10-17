/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_READ_CORE_H__
#define __NX30_D_READ_CORE_H__
#ifdef NX30_D_READ_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef uint64_t nx30_d_read_u64;
typedef uint8_t nx30_d_read_u8;
#else
#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/errno.h>
typedef u64 nx30_d_read_u64;
typedef u8 nx30_d_read_u8;
#endif
#define NX30_D_READ_WINDOW_BYTES (64U << 10)
typedef int (*nx30_d_read_window_fn)(void *ctx,nx30_d_read_u64 offset,
                                     size_t length,size_t *read_length,
                                     nx30_d_read_u8 *buffer);
static inline int nx30_d_read_extent(nx30_d_read_window_fn read_fn,void *ctx,
                                     nx30_d_read_u64 base,size_t length,
                                     nx30_d_read_u8 *buffer)
{
 size_t offset=0;
 if (!read_fn || !buffer || !length ||
     base>(nx30_d_read_u64)-1-(nx30_d_read_u64)length) return -EINVAL;
 while (offset<length) {
  size_t chunk=length-offset,got=0; int ret;
  if (chunk>NX30_D_READ_WINDOW_BYTES) chunk=NX30_D_READ_WINDOW_BYTES;
  ret=read_fn(ctx,base+(nx30_d_read_u64)offset,chunk,&got,buffer+offset);
  if (ret==-EUCLEAN && got==chunk) ret=0;
  if (ret) return ret;
  if (got!=chunk) return -EIO;
  offset+=chunk;
 }
 return 0;
}
#endif
