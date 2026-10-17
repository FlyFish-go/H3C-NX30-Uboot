/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_D_FW_TXN_CORE_H__
#define __NX30_D_FW_TXN_CORE_H__
#include <linux/types.h>
struct nx30_d_fw_txn_io {
 void *ctx;
 int (*is_bad)(void *ctx,u64 offset);
 int (*window_open)(void *ctx,u64 offset,u64 size);
 int (*erase)(void *ctx,u64 offset,u32 size);
 int (*write)(void *ctx,u64 offset,const u8 *data,u32 size);
 int (*read)(void *ctx,u64 offset,u8 *data,u32 size);
 void (*window_close)(void *ctx);
 int (*close_status)(void *ctx);
};
int nx30_d_fw_txn_run(const struct nx30_d_fw_txn_io *io,u64 target,
                      u32 extent_size,const u8 *image,u32 image_size,
                      u32 peb_size,u8 *writebuf,u8 *verifybuf,
                      u32 *bytes_verified,u32 *stage);
#endif
