// SPDX-License-Identifier: GPL-2.0
/* Host-testable safety core for the RT3000-B firmware transaction. */
#ifdef RT3_FW_HOST_TEST
#include <errno.h>
#include <string.h>
#include <rt3000_b_fw_core.h>
#else
#include <common.h>
#include <asm/errno.h>
#include <rt3000_b_fw_core.h>
#endif

int rt3_fw_core_range_allows(int open, const void *window_mtd,
			     const void *request_mtd, rt3_core_u64 start,
			     rt3_core_u64 end, rt3_core_u64 offset, rt3_core_u64 size,
			     int raw, int explicit_oob)
{
	if (!open || !window_mtd || window_mtd != request_mtd || raw ||
	    explicit_oob || !size || start > end || offset < start ||
	    offset > end || size > end - offset)
		return 0;
	return 1;
}

int rt3_fw_core_request_matches(rt3_core_u32 pending_token,
				 rt3_core_u32 request_token,
				 const char *pending_target,
				 const char *request_target,
				 const char *pending_mode,
				 const char *request_mode,
				 rt3_core_u32 pending_fingerprint,
				 rt3_core_u32 current_fingerprint,
				 const rt3_core_u8 *expected_digest,
				 const rt3_core_u8 *current_digest,
				 unsigned int digest_size)
{
	if (!pending_token || pending_token != request_token ||
	    !pending_target || !request_target || strcmp(pending_target, request_target) ||
	    !pending_mode || !request_mode || strcmp(pending_mode, request_mode) ||
	    pending_fingerprint != current_fingerprint || !expected_digest ||
	    !current_digest || !digest_size || memcmp(expected_digest, current_digest, digest_size))
		return 0;
	return 1;
}

int rt3_fw_core_input_size_ok(rt3_core_u64 image_size,
			      rt3_core_u64 max_size, rt3_core_u64 target_size)
{
	return image_size && image_size <= max_size && image_size <= target_size;
}

int rt3_fw_core_capacity_ok(rt3_core_u32 good_pebs, rt3_core_u32 image_pebs,
			    rt3_core_u32 reserved_pebs, rt3_core_u32 margin)
{
	if (!image_pebs || reserved_pebs > 0xffffffffU - margin)
		return 0;
	return image_pebs <= good_pebs &&
	       good_pebs >= reserved_pebs + margin;
}

int rt3_fw_core_transaction(const struct rt3_fw_io *io,
			    rt3_core_u64 target_offset, rt3_core_u64 target_size,
			    const rt3_core_u8 *image, rt3_core_u32 image_bytes,
			    rt3_core_u32 peb_size, rt3_core_u32 image_pebs,
			    rt3_core_u32 reserved_pebs, rt3_core_u32 margin,
			    rt3_core_u32 *good_offsets, rt3_core_u32 max_good,
			    rt3_core_u8 *verify_buf)
{
	rt3_core_u64 end, offset;
	rt3_core_u32 good_count = 0, i, done;
	int ret, opened = 0;
	if (!io || !io->is_bad || !io->erase || !io->write || !io->read ||
	    !io->window_open || !io->window_close || !image || !verify_buf ||
	    !good_offsets || !target_size || !peb_size || !image_bytes ||
	    image_bytes % peb_size || image_pebs != image_bytes / peb_size ||
	    target_offset > ~(rt3_core_u64)0 - target_size || target_offset + target_size > 0xffffffffULL ||
	    target_offset % peb_size || image_bytes > target_size ||
	    target_size / peb_size > max_good)
		return -EINVAL;
	end = target_offset + target_size;

	/* Complete bad-block and capacity scan before opening the write window. */
	for (offset = target_offset; offset < end; offset += peb_size) {
		ret = io->is_bad(io->ctx, offset);
		if (ret < 0) return ret;
		if (ret) continue;
		good_offsets[good_count++] = (rt3_core_u32)offset;
	}
	if (!rt3_fw_core_capacity_ok(good_count, image_pebs,
				     reserved_pebs, margin))
		return -ENOSPC;
	ret = io->window_open(io->ctx, target_offset, target_size);
	if (ret) return ret;
	opened = 1;

	for (i = 0; i < good_count; i++) {
		ret = io->erase(io->ctx, good_offsets[i], peb_size);
		if (ret) goto out;
	}
	for (i = 0; i < image_pebs; i++) {
		done = 0;
		ret = io->write(io->ctx, good_offsets[i], image + (size_t)i * peb_size,
				peb_size, &done);
		if (ret || done != peb_size) {
			if (!ret) ret = -EIO;
			goto out;
		}
		done = 0;
		ret = io->read(io->ctx, good_offsets[i], verify_buf, peb_size, &done);
		if (ret || done != peb_size) {
			if (!ret) ret = -EIO;
			goto out;
		}
		if (memcmp(verify_buf, image + (size_t)i * peb_size, peb_size)) {
			ret = -EIO;
			goto out;
		}
	}
	for (i = image_pebs; i < good_count; i++) {
		done = 0;
		ret = io->read(io->ctx, good_offsets[i], verify_buf, peb_size, &done);
		if (ret || done != peb_size) {
			if (!ret) ret = -EIO;
			goto out;
		}
		for (done = 0; done < peb_size; done++)
			if (verify_buf[done] != 0xff) {
				ret = -EIO;
				goto out;
			}
	}
	ret = 0;
out:
	if (opened) io->window_close(io->ctx);
	return ret;
}
