// SPDX-License-Identifier: GPL-2.0-or-later
/* Host tests for BOOTCONFIG parsing, the raw UBI check and the OEM return
 * transaction. Real dumps are private and not in the repository: point
 * BB_FIXTURES at a directory holding
 *   hw{a,b,c,d}-any-slot-20261024/mtd{2,3}.bin   BOOTCONFIG copies
 *   field-bc{0,1}.bin                            field RT3000 BOOTCONFIG
 *   hwd-any-slot-20261024/mtd16.bin              an OEM NX30 rootfs slot
 * Without it, only the synthetic cases run. */
#include "../runtime/oem_return.h"
#include "flash_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BC_SIZE 0x40000u
#define SLOT_SIZE 0x2800000u
#define SLOT_LO 0x900000u
#define SLOT_HI 0x3100000u

static int failures, checks;
static const char *fixtures;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} \
} while (0)

static uint8_t *load(const char *rel, uint32_t want)
{
	char path[512];
	FILE *f;
	uint8_t *buf;
	size_t n;

	snprintf(path, sizeof(path), "%s/%s", fixtures, rel);
	f = fopen(path, "rb");
	if (!f)
		return NULL;
	buf = malloc(want);
	n = buf ? fread(buf, 1, want, f) : 0;
	fclose(f);
	if (n != want) {
		free(buf);
		return NULL;
	}
	return buf;
}

/* A minimal valid BOOTCONFIG partition, padded like the OEM writes it. */
static uint8_t *synthetic_bc(uint32_t age, uint32_t selector)
{
	static const char *const names[] = {
		"0:QSEE", "0:DEVCFG", "0:CDT", "0:APPSBL", "0:HLOS", "rootfs", "0:WIFIFW", "0:BTFW",
	};
	uint8_t *b = malloc(BC_SIZE);

	memset(b, 0xff, BC_SIZE);
	memset(b, 0, 2048);
	*(uint32_t *)b = BB_BC_MAGIC_START;
	*(uint32_t *)(b + 4) = age;
	*(uint32_t *)(b + 8) = 8;
	for (int i = 0; i < 8; i++) {
		strcpy((char *)b + 12 + 20 * i, names[i]);
		*(uint32_t *)(b + 28 + 20 * i) = i == 5 ? selector : 0;
	}
	*(uint32_t *)(b + 332) = BB_BC_MAGIC_END;
	return b;
}

struct rig {
	struct mem_nand *bc[2], *slot[2];
	struct bb_ret_inputs in;
	uint8_t *orig[2];
};

static void rig_init(struct rig *r, uint8_t *bc0, uint8_t *bc1, const uint8_t *oem,
		     uint32_t oem_off)
{
	memset(r, 0, sizeof(*r));
	r->orig[0] = bc0;
	r->orig[1] = bc1;
	r->bc[0] = mem_nand_new("0:BOOTCONFIG", 0x100000, BC_SIZE, bc0);
	r->bc[1] = mem_nand_new("0:BOOTCONFIG1", 0x140000, BC_SIZE, bc1);
	r->slot[0] = mem_nand_new("rootfs_1", SLOT_LO, SLOT_SIZE, oem_off == SLOT_LO ? oem : NULL);
	r->slot[1] = mem_nand_new("rootfs", SLOT_HI, SLOT_SIZE, oem_off == SLOT_HI ? oem : NULL);
	for (int i = 0; i < 2; i++) {
		r->in.bc[i] = &r->bc[i]->f;
		r->in.slot[i] = &r->slot[i]->f;
	}
	r->in.running_offset = oem_off == SLOT_LO ? SLOT_HI : SLOT_LO;
}

static void rig_free(struct rig *r)
{
	for (int i = 0; i < 2; i++) {
		mem_nand_free(r->bc[i]);
		mem_nand_free(r->slot[i]);
		free(r->orig[i]);
	}
}

/* After a successful return: both copies select the OEM slot, carry the new
 * age, and every other byte of each copy is unchanged. */
static void check_returned(struct rig *r, const struct bb_ret_plan *p, const char *what)
{
	for (int c = 0; c < 2; c++) {
		const uint8_t *now = r->bc[c]->data, *src = r->orig[c];
		struct bb_bc bc;
		size_t diff = 0;

		CHECK(bb_bc_parse(now, BC_SIZE, &bc) == BB_BC_OK, "%s copy %d parses", what, c);
		CHECK(bc.rootfs_selector == p->target_selector, "%s copy %d selector %u", what, c,
		      bc.rootfs_selector);
		CHECK(bc.age == p->new_age, "%s copy %d age %u want %u", what, c, bc.age, p->new_age);
		if (!p->valid[c])
			continue;	/* rebuilt from the other copy */
		for (uint32_t i = 0; i < BC_SIZE; i++) {
			if (i >= 4 && i < 8)
				continue;
			if (i >= 12 + 20 * bc.rootfs_index + 16 && i < 12 + 20 * bc.rootfs_index + 20)
				continue;
			diff += now[i] != src[i];
		}
		CHECK(diff == 0, "%s copy %d: %zu unrelated bytes changed", what, c, diff);
		/* Pages that are all 0xff must stay unprogrammed. */
		for (uint32_t pg = 0; pg < 0x20000 / 2048; pg++) {
			int ff = 1;

			for (uint32_t i = 0; i < 2048 && ff; i++)
				ff = now[pg * 2048 + i] == 0xff;
			CHECK(!(ff && r->bc[c]->programmed[pg]), "%s copy %d page %u programmed as 0xff",
			      what, c, pg);
		}
	}
}

static uint8_t *dup(const uint8_t *src, size_t n)
{
	uint8_t *d = malloc(n);

	memcpy(d, src, n);
	return d;
}

static void test_parse_fixtures(void)
{
	static const char *const files[] = {
		"hwa-any-slot-20261024/mtd2.bin", "hwa-any-slot-20261024/mtd3.bin",
		"hwb-any-slot-20261024/mtd2.bin", "hwb-any-slot-20261024/mtd3.bin",
		"hwc-any-slot-20261024/mtd2.bin", "hwc-any-slot-20261024/mtd3.bin",
		"hwd-any-slot-20261024/mtd2.bin", "hwd-any-slot-20261024/mtd3.bin",
		"field-bc0.bin", "field-bc1.bin",
	};

	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		uint8_t *b = load(files[i], BC_SIZE);
		struct bb_bc bc;

		CHECK(b, "load %s", files[i]);
		if (!b)
			continue;
		CHECK(bb_bc_parse(b, BC_SIZE, &bc) == BB_BC_OK, "parse %s", files[i]);
		free(b);
	}
}

static void test_ubi_fixture(const uint8_t *oem)
{
	struct mem_nand *m = mem_nand_new("rootfs_1", SLOT_LO, SLOT_SIZE, oem);
	struct bb_ubi_report r;
	enum bb_ubi_status s = bb_ubi_check(&m->f, &r);

	CHECK(s == BB_UBI_OK, "OEM slot fixture: %s", bb_ubi_status_str(s));
	CHECK(r.kernel_lebs > 0 && r.volumes >= 2, "OEM slot report: %u lebs %u volumes",
	      r.kernel_lebs, r.volumes);
	printf("  oem fixture: pebs=%u ec=%u volumes=%u kernel_lebs=%u kernel_bytes=%u static=%d\n",
	       r.pebs, r.ec_valid, r.volumes, r.kernel_lebs, r.kernel_bytes, r.kernel_static);

	/* An erased slot is not a system. */
	memset(m->data, 0xff, SLOT_SIZE);
	CHECK(bb_ubi_check(&m->f, &r) == BB_UBI_NOT_UBI, "erased slot rejected");
	mem_nand_free(m);
}

/* Find the first PEB of a static kernel volume and flip a data byte. */
static int corrupt_kernel(uint8_t *img)
{
	for (uint32_t peb = 0; peb < SLOT_SIZE / 0x20000; peb++) {
		uint8_t *p = img + peb * 0x20000;
		uint32_t vid = p[16] << 24 | p[17] << 16 | p[18] << 8 | p[19];
		uint32_t data = p[20] << 24 | p[21] << 16 | p[22] << 8 | p[23];

		if (memcmp(p, "UBI#", 4) || vid >= 0x20000 || memcmp(p + vid, "UBI!", 4))
			continue;
		if (p[vid + 5] == 2 && p[vid + 8] == 0 && p[vid + 9] == 0 && p[vid + 10] == 0 &&
		    p[vid + 11] < 0x80) {
			p[data + 100] ^= 0x40;
			return 1;
		}
	}
	return 0;
}

static void test_return_field(const uint8_t *oem)
{
	struct bb_ret_plan p;
	struct rig r;
	uint8_t *bc0 = load("field-bc0.bin", BC_SIZE), *bc1 = load("field-bc1.bin", BC_SIZE);

	if (!bc0 || !bc1) {
		CHECK(0, "field fixtures missing");
		return;
	}
	/* Field unit: bridge runs from 0x3100000 (selector 1), OEM in 0x900000. */
	rig_init(&r, bc0, bc1, oem, SLOT_LO);
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_READY, "field plan: %s", bb_ret_code_str(p.code));
	CHECK(p.target_selector == 0 && p.oem_offset == SLOT_LO, "field target sel %u off %#x",
	      p.target_selector, p.oem_offset);
	CHECK(bb_ret_commit(&r.in, &p) == BB_RET_DONE, "field commit: %s", bb_ret_code_str(p.code));
	check_returned(&r, &p, "field");
	bb_ret_plan_free(&p);

	/* Running it again is a no-op. */
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_ALREADY, "second plan: %s", bb_ret_code_str(p.code));
	bb_ret_plan_free(&p);
	rig_free(&r);
}

static void test_return_board_fixtures(const uint8_t *oem)
{
	static const char *const boards[] = { "hwa", "hwb", "hwc", "hwd" };

	for (int i = 0; i < 4; i++) {
		char a[64], b[64];
		uint8_t *bc0, *bc1;
		struct bb_ret_plan p;
		struct bb_bc bc;
		uint32_t oem_off;
		struct rig r;

		snprintf(a, sizeof(a), "%s-any-slot-20261024/mtd2.bin", boards[i]);
		snprintf(b, sizeof(b), "%s-any-slot-20261024/mtd3.bin", boards[i]);
		bc0 = load(a, BC_SIZE);
		bc1 = load(b, BC_SIZE);
		if (!bc0 || !bc1 || bb_bc_parse(bc0, BC_SIZE, &bc)) {
			CHECK(0, "%s fixtures", boards[i]);
			free(bc0);
			free(bc1);
			continue;
		}
		/* Pretend the bridge runs from the slot the primary copy boots. */
		oem_off = bc.rootfs_selector ? SLOT_LO : SLOT_HI;
		rig_init(&r, bc0, bc1, oem, oem_off);
		bb_ret_plan(&r.in, &p);
		CHECK(p.code == BB_RET_READY, "%s plan: %s", boards[i], bb_ret_code_str(p.code));
		bb_ret_commit(&r.in, &p);
		CHECK(p.code == BB_RET_DONE, "%s commit: %s", boards[i], bb_ret_code_str(p.code));
		check_returned(&r, &p, boards[i]);
		bb_ret_plan_free(&p);
		rig_free(&r);
	}
}

static void test_refusals_and_faults(const uint8_t *oem)
{
	struct bb_ret_plan p;
	struct rig r;
	uint8_t *bad_oem;

	/* Already pointing at the OEM slot. */
	rig_init(&r, synthetic_bc(5, 0), synthetic_bc(5, 0), oem, SLOT_LO);
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_ALREADY, "already: %s", bb_ret_code_str(p.code));
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* Copies disagree (seen on HW-A): both end up on the OEM slot. */
	rig_init(&r, synthetic_bc(3, 0), synthetic_bc(2, 1), oem, SLOT_LO);
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_READY && p.new_age == 4, "disagree plan: %s age %u",
	      bb_ret_code_str(p.code), p.new_age);
	bb_ret_commit(&r.in, &p);
	check_returned(&r, &p, "disagree");
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* Primary unparseable: rebuilt from the backup. */
	{
		uint8_t *bc0 = synthetic_bc(7, 1);

		bc0[0] ^= 1;
		rig_init(&r, bc0, synthetic_bc(7, 1), oem, SLOT_LO);
		bb_ret_plan(&r.in, &p);
		CHECK(p.code == BB_RET_READY && !p.valid[0] && p.valid[1], "primary invalid: %s",
		      bb_ret_code_str(p.code));
		bb_ret_commit(&r.in, &p);
		CHECK(p.code == BB_RET_DONE, "primary invalid commit: %s", bb_ret_code_str(p.code));
		check_returned(&r, &p, "primary-invalid");
		bb_ret_plan_free(&p);
		rig_free(&r);
	}

	/* Rebuilding an invalid copy must reject mismatched erase sizes first. */
	rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), oem, SLOT_LO);
	r.bc[0]->data[0] ^= 1;
	r.bc[0]->f.erase_size *= 2;
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_BC_IO && !r.bc[0]->erases && !r.bc[1]->erases,
	      "unequal BOOTCONFIG geometry refuses before copying/writing");
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* Do not wrap the age counter back to zero. */
	rig_init(&r, synthetic_bc(UINT32_MAX, 1), synthetic_bc(UINT32_MAX, 1), oem, SLOT_LO);
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_BC_INVALID, "age overflow refused");
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* Both copies unparseable. */
	{
		uint8_t *bc0 = synthetic_bc(1, 1), *bc1 = synthetic_bc(1, 1);

		bc0[332] = bc1[332] = 0;
		rig_init(&r, bc0, bc1, oem, SLOT_LO);
		bb_ret_plan(&r.in, &p);
		CHECK(p.code == BB_RET_BC_INVALID, "both invalid: %s", bb_ret_code_str(p.code));
		bb_ret_plan_free(&p);
		rig_free(&r);
	}

	/* Bad block 0 in BOOTCONFIG1. */
	rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), oem, SLOT_LO);
	r.bc[1]->bad[0] = 1;
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_BC_IO, "bad block: %s", bb_ret_code_str(p.code));
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* OEM slot erased. */
	rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), NULL, SLOT_LO);
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_OEM_SLOT && p.ubi_status == BB_UBI_NOT_UBI, "erased oem: %s/%s",
	      bb_ret_code_str(p.code), bb_ubi_status_str(p.ubi_status));
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* OEM kernel corrupted. */
	bad_oem = dup(oem, SLOT_SIZE);
	if (corrupt_kernel(bad_oem)) {
		rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), bad_oem, SLOT_LO);
		bb_ret_plan(&r.in, &p);
		CHECK(p.code == BB_RET_OEM_SLOT && p.ubi_status == BB_UBI_KERNEL_CRC,
		      "corrupt kernel: %s/%s", bb_ret_code_str(p.code), bb_ubi_status_str(p.ubi_status));
		bb_ret_plan_free(&p);
		rig_free(&r);
	} else {
		printf("  note: fixture kernel volume is not static, CRC case skipped\n");
	}
	free(bad_oem);

	/* Running from neither slot. */
	rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), oem, SLOT_LO);
	r.in.running_offset = 0x123000;
	bb_ret_plan(&r.in, &p);
	CHECK(p.code == BB_RET_BAD_SLOTS, "unknown running slot: %s", bb_ret_code_str(p.code));
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* Flash changed between plan and commit. */
	rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), oem, SLOT_LO);
	bb_ret_plan(&r.in, &p);
	r.bc[0]->data[4] = 9;
	CHECK(bb_ret_commit(&r.in, &p) == BB_RET_CHANGED, "changed: %s", bb_ret_code_str(p.code));
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* BOOTCONFIG1 write fails: primary must be untouched. */
	rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), oem, SLOT_LO);
	r.bc[1]->fail_write_at = 1;
	bb_ret_plan(&r.in, &p);
	CHECK(bb_ret_commit(&r.in, &p) == BB_RET_WRITE_BC1, "bc1 fail: %s", bb_ret_code_str(p.code));
	CHECK(!memcmp(r.bc[0]->data, r.orig[0], BC_SIZE), "primary untouched after bc1 failure");
	bb_ret_plan_free(&p);
	rig_free(&r);

	/* BOOTCONFIG erase fails after BOOTCONFIG1 is done: backup must be right. */
	rig_init(&r, synthetic_bc(1, 1), synthetic_bc(1, 1), oem, SLOT_LO);
	r.bc[0]->fail_erase_at = 1;
	bb_ret_plan(&r.in, &p);
	CHECK(bb_ret_commit(&r.in, &p) == BB_RET_WRITE_BC0, "bc0 fail: %s", bb_ret_code_str(p.code));
	{
		struct bb_bc bc;

		CHECK(bb_bc_parse(r.bc[1]->data, BC_SIZE, &bc) == BB_BC_OK && bc.rootfs_selector == 0,
		      "backup selects OEM after primary failure");
	}
	bb_ret_plan_free(&p);
	rig_free(&r);
}

int main(void)
{
	uint8_t *oem;

	fixtures = getenv("BB_FIXTURES");
	if (!fixtures) {
		printf("BB_FIXTURES not set: fixture tests need private dumps\n");
		return 77;
	}
	oem = load("hwd-any-slot-20261024/mtd16.bin", SLOT_SIZE);
	if (!oem) {
		printf("OEM slot fixture missing\n");
		return 1;
	}
	test_parse_fixtures();
	test_ubi_fixture(oem);
	test_return_field(oem);
	test_return_board_fixtures(oem);
	test_refusals_and_faults(oem);
	free(oem);
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
