/* SPDX-License-Identifier: GPL-2.0 */
/* cc -std=c99 -Wall -Wextra -Werror -DNX30_NOR_AUTOECC_HOST_TEST -I failsafe/modules -o /tmp/nor-autoecc-test test/nor_nand_autoecc_core_hosttest.c */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "../failsafe/modules/nor_nand_autoecc_core.c"

#ifndef EUCLEAN
#define EUCLEAN 117
#endif

struct fake_nand {
	unsigned int mode;
	unsigned int set_calls;
	unsigned int read_calls;
	unsigned int init_calls;
	unsigned int restore_calls;
	unsigned int flash_write_calls;
	unsigned int flash_erase_calls;
	unsigned int step_ms;
	unsigned int now_ms;
	unsigned int fail_index;
	unsigned int short_index;
	unsigned int fail_mode;
	unsigned int set_fail_mode;
	unsigned int euclean_mode;
	int init_error;
	int restore_error;
	int fail_read;
	int short_read;
	int wrong_mode_ebadmsg;
	int ebadmsg_zero_retlen;
	int short_status;
	int corrupt_ec_crc;
	int corrupt_vid_crc;
	uint8_t valid4[NX30_NOR_AUTOECC_SAMPLE_COUNT];
	uint8_t valid8[NX30_NOR_AUTOECC_SAMPLE_COUNT];
};

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void put_be64(uint8_t *p, uint64_t v)
{
	put_be32(p, (uint32_t)(v >> 32));
	put_be32(p + 4, (uint32_t)v);
}

static uint32_t raw_ubi_crc(const uint8_t *p, uint32_t n)
{
	uint32_t crc = 0xffffffffU, i, bit;
	for (i = 0; i < n; i++) {
		crc ^= p[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
	}
	return crc;
}

static void make_ec(uint8_t *p, int bad_crc)
{
	memset(p, 0xff, NX30_NOR_AUTOECC_PAGE_SIZE);
	put_be32(p, 0x55424923U); p[4] = 1;
	put_be64(p + 8, 7); put_be32(p + 16, 2048); put_be32(p + 20, 4096);
	put_be32(p + 60, raw_ubi_crc(p, 60) ^ (bad_crc ? 1U : 0U));
}

static void make_vid(uint8_t *p, int bad_crc)
{
	memset(p, 0xff, NX30_NOR_AUTOECC_PAGE_SIZE);
	put_be32(p, 0x55424921U); p[4] = 1; p[5] = 1; p[6] = 0; p[7] = 0;
	put_be32(p + 8, 1); put_be32(p + 12, 3); put_be32(p + 24, 0);
	put_be32(p + 28, 1); put_be32(p + 32, 0); put_be64(p + 40, 9);
	put_be32(p + 60, raw_ubi_crc(p, 60) ^ (bad_crc ? 1U : 0U));
}

static int fake_init(void *ctx)
{
	struct fake_nand *f = ctx;
	f->init_calls++;
	return f->init_error;
}

static int fake_set_ecc(void *ctx, unsigned int bits)
{
	struct fake_nand *f = ctx;
	f->set_calls++;
	f->mode = bits;
	if (f->set_fail_mode == bits)
		return -EIO;
	return 0;
}

static int fake_read(void *ctx, u64 offset, void *data, u32 cap,
		     struct nx30_nor_autoecc_page_result *result)
{
	struct fake_nand *f = ctx;
	u32 idx = (u32)(offset / NX30_NOR_AUTOECC_STRIDE);
	u32 within = (u32)(offset % NX30_NOR_AUTOECC_STRIDE);
	int valid;
	f->read_calls++;
	if (cap < NX30_NOR_AUTOECC_PAGE_SIZE)
		return -EINVAL;
	memset(data, 0xff, NX30_NOR_AUTOECC_PAGE_SIZE);
	result->status = 0;
	result->data_len = NX30_NOR_AUTOECC_PAGE_SIZE;
	result->failed = 0;
	result->corrected = 0;
	valid = idx < NX30_NOR_AUTOECC_SAMPLE_COUNT &&
		(f->mode == NX30_NOR_AUTOECC_ECC4 ? f->valid4[idx] : f->valid8[idx]);
	if (f->fail_read && idx == f->fail_index && f->mode == f->fail_mode) {
		result->status = -EIO;
		result->data_len = 0;
		return 0;
	}
	if (f->short_read && idx == f->short_index && f->mode == f->fail_mode) {
		result->data_len--;
		result->status = f->short_status;
		return 0;
	}
	if (!valid && f->wrong_mode_ebadmsg) {
		result->status = -EBADMSG;
		result->data_len = f->ebadmsg_zero_retlen ? 0 :
			NX30_NOR_AUTOECC_PAGE_SIZE;
		result->failed = 1;
		return 0;
	}
	if (!valid)
		return 0;
	if (within == 0)
		make_ec(data, f->corrupt_ec_crc);
	else if (within == 2048)
		make_vid(data, f->corrupt_vid_crc);
	else
		return -EINVAL;
	if (f->euclean_mode == f->mode) {
		result->status = -EUCLEAN;
		return 0;
	}
	return 0;
}

static int fake_restore4(void *ctx)
{
	struct fake_nand *f = ctx;
	f->restore_calls++;
	f->mode = NX30_NOR_AUTOECC_ECC4;
	return f->restore_error;
}

static u32 fake_timer(void *ctx, u32 base)
{
	struct fake_nand *f = ctx;
	u32 now = f->now_ms;
	f->now_ms += f->step_ms;
	return now - base;
}

static const struct nx30_nor_autoecc_ops fake_ops = {
	.init_once = fake_init,
	.set_read_ecc = fake_set_ecc,
	.read_page = fake_read,
	.restore4 = fake_restore4,
	.get_timer_ms = fake_timer,
};

static void check(int ok, const char *name)
{
	if (!ok) { fprintf(stderr, "FAIL: %s\
", name); exit(1); }
}

static void prepare(struct fake_nand *f, struct nx30_nor_autoecc_result *r)
{
	memset(f, 0, sizeof(*f));
	memset(r, 0, sizeof(*r));
	f->step_ms = 1;
}

int main(void)
{
	struct fake_nand f;
	struct nx30_nor_autoecc_result r;
	int ret;

	prepare(&f, &r);
	nx30_nor_autoecc_fixed_result(&r, NX30_NOR_AUTOECC_ECC4);
	check(r.state == NX30_NOR_AUTOECC_STATE_ECC4 &&
	      r.selected_read_ecc == 4 && r.fixed_profile_mode &&
	      !r.samples_ecc4 && !r.samples_ecc8 && !r.valid_ecc4 &&
	      !r.valid_ecc8, "fixed-profile mode has no dual-sample evidence");

	prepare(&f, &r); f.valid4[1] = f.valid4[9] = 1; f.wrong_mode_ebadmsg = 1;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_ECC4 && r.selected_read_ecc == 4, "ECC4-only selects ECC4");
	check(r.valid_ecc4 == 2 && !r.valid_ecc8 && f.mode == 4 && !f.restore_calls, "ECC4 evidence and retained mode");
	check(f.flash_write_calls == 0 && f.flash_erase_calls == 0, "detector has no flash-write operation");

	prepare(&f, &r); f.valid8[3] = f.valid8[17] = 1; f.wrong_mode_ebadmsg = 1; f.ebadmsg_zero_retlen = 1;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_ECC8 && r.selected_read_ecc == 8, "ECC8-only selects ECC8");
	check(f.mode == 8 && !f.restore_calls, "ECC8 mode retained");

	prepare(&f, &r);
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && !r.selected_read_ecc && f.mode == 4, "all-FF remains unknown and restores ECC4");

	prepare(&f, &r); f.valid4[2] = f.valid8[2] = 1;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && r.ambiguous_blocks == 1, "both modes valid at same block is ambiguous");

	prepare(&f, &r); f.valid4[0] = f.valid4[1] = 1; f.corrupt_ec_crc = 1;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && !r.valid_ecc4 && r.invalid_headers_ecc4 > 0, "bad EC CRC rejected");

	prepare(&f, &r); f.valid4[0] = f.valid4[1] = 1; f.corrupt_vid_crc = 1;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && !r.valid_ecc4 && r.invalid_headers_ecc4 > 0, "bad VID CRC rejected");

	prepare(&f, &r); f.valid4[0] = f.valid4[1] = 1; f.euclean_mode = 4;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_ECC4, "full corrected page with valid CRC accepted");

	prepare(&f, &r); f.valid4[0] = f.valid4[1] = 1; f.valid8[4] = f.valid8[5] = 1;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_MIXED && !r.selected_read_ecc && f.mode == 4, "different blocks with exclusive valid modes are mixed");

	prepare(&f, &r); f.fail_read = 1; f.fail_index = 0; f.fail_mode = 4;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && r.reason == NX30_NOR_AUTOECC_REASON_READ_FAILED && r.samples_ecc4 == 1, "controller read error stops incomplete scan");
	check(f.restore_calls == 1 && f.mode == 4, "read error restores ECC4");

	prepare(&f, &r); f.short_read = 1; f.short_index = 0; f.fail_mode = 4;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && r.reason == NX30_NOR_AUTOECC_REASON_SHORT_READ, "successful short page read stops as unknown");

	prepare(&f, &r); f.short_read = 1; f.short_index = 0; f.fail_mode = 4; f.short_status = -EUCLEAN;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && r.reason == NX30_NOR_AUTOECC_REASON_SHORT_READ, "EUCLEAN short page remains fatal");

	prepare(&f, &r); f.step_ms = 1000;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && r.reason == NX30_NOR_AUTOECC_REASON_TIMEOUT, "10 second timeout stops scan");
	check(f.restore_calls == 1 && f.mode == 4 && r.samples_ecc4 < NX30_NOR_AUTOECC_SAMPLE_COUNT, "timeout restores ECC4 with incomplete count");

	prepare(&f, &r); f.valid4[0] = f.valid4[1] = 1; f.set_fail_mode = 8;
	ret = nx30_nor_autoecc_probe(&fake_ops, &f, &r);
	check(!ret && r.state == NX30_NOR_AUTOECC_STATE_UNKNOWN && r.reason == NX30_NOR_AUTOECC_REASON_ECC8_SWITCH_FAILED, "ECC switch failure remains unknown");
	check(f.restore_calls == 1 && f.mode == 4, "switch error restores ECC4");

	puts("NOR_NAND_AUTOECC_CORE_HOSTTEST_PASS");
	return 0;
}
