// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#include <asm/errno.h>
#include <nx30_d_fw_txn_core.h>
#include <nx30_d_service.h>

int nx30_d_fw_txn_run(const struct nx30_d_fw_txn_io *io,u64 target,
                      u32 extent_size,const u8 *image,u32 image_size,
                      u32 peb_size,u8 *writebuf,u8 *verifybuf,
                      u32 *bytes_verified,u32 *stage)
{
 u32 i,blocks; int ret,opened=0,close_ret;
 if (bytes_verified) *bytes_verified=0;
 if (stage) *stage=NX30_D_STAGE_PREFLIGHT;
 if (!io || !io->is_bad || !io->window_open || !io->erase || !io->write ||
     !io->read || !io->window_close || !io->close_status || !image ||
     !writebuf || !verifybuf || !peb_size || !extent_size ||
     extent_size%peb_size || !image_size || image_size%peb_size ||
     image_size>extent_size) return -EINVAL;
 blocks=extent_size/peb_size;
 /* A bad block changes every later UBI PEB mapping; reject before opening. */
 for (i=0;i<blocks;i++) {
  ret=io->is_bad(io->ctx,target+(u64)i*peb_size);
  if (ret) return ret>0?-EIO:ret;
 }
 ret=io->window_open(io->ctx,target,extent_size); if (ret) return ret;
 opened=1;
 if (stage) *stage=NX30_D_STAGE_ERASE;
 ret=io->erase(io->ctx,target,extent_size); if (ret) goto out;
 if (stage) *stage=NX30_D_STAGE_WRITE;
 for (i=0;i<image_size/peb_size;i++) {
  u64 off=target+(u64)i*peb_size;
  memcpy(writebuf,image+(size_t)i*peb_size,peb_size);
  ret=io->write(io->ctx,off,writebuf,peb_size); if (ret) goto out;
  ret=io->read(io->ctx,off,verifybuf,peb_size); if (ret) goto out;
  if (memcmp(verifybuf,writebuf,peb_size)) { ret=-EIO; goto out; }
  if (bytes_verified) *bytes_verified+=peb_size;
 }
 if (stage) *stage=NX30_D_STAGE_READBACK;
 for (i=image_size/peb_size;i<blocks;i++) {
  u32 j; ret=io->read(io->ctx,target+(u64)i*peb_size,verifybuf,peb_size); if (ret) goto out;
  for (j=0;j<peb_size;j++) if (verifybuf[j]!=0xff) { ret=-EIO; goto out; }
  if (bytes_verified) *bytes_verified+=peb_size;
 }
 /* Full second pass ties readback evidence to the exact upload bytes. */
 for (i=0;i<image_size/peb_size;i++) {
  ret=io->read(io->ctx,target+(u64)i*peb_size,verifybuf,peb_size); if (ret) goto out;
  if (memcmp(verifybuf,image+(size_t)i*peb_size,peb_size)) { ret=-EIO; goto out; }
 }
 ret=0;
out:
 if (opened) {
  io->window_close(io->ctx);
  close_ret=io->close_status(io->ctx);
  if (close_ret) ret=close_ret;
 }
 if (!ret && stage) *stage=NX30_D_STAGE_COMPLETE;
 return ret;
}
