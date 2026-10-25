// SPDX-License-Identifier: GPL-2.0-or-later
/* Host tests for the rule pack and the install transaction. Needs BB_PACK (a
 * pack built by pack/make_pack.py from the U-Boot release) and BB_FIXTURES
 * (see test_oem_return.c and test_facts_rules.c). */
#include "../runtime/install.h"
#include "../runtime/pack.h"
#include "flash_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BC_SIZE 0x40000u
#define SLOT_SIZE 0x2800000u
#define APPSBL_SIZE 0x140000u

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

static uint8_t *load(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	rewind(f);
	buf = malloc(n + 1);
	if (buf && fread(buf, 1, n, f) != (size_t)n) {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	if (buf) {
		buf[n] = 0;
		*len = n;
	}
	return buf;
}

static uint8_t *fixture(const char *name, size_t *len)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", fixtures, name);
	return load(path, len);
}

/* A HW-B field unit: OEM U-Boot in both copies, bridge running from the
 * upper slot, OEM NX30 system image standing in for the OEM slot. */
struct rig {
	struct mem_nand *appsbl[2], *bc[2], *slot[2];
	struct bb_inst_inputs in;
};

static int rig_init(struct rig *r)
{
	size_t len;
	uint8_t *bc0 = fixture("field-bc0.bin", &len), *bc1 = fixture("field-bc1.bin", &len);
	uint8_t *oem = fixture("hwd-any-slot-20261024/mtd16.bin", &len);
	uint8_t *old = malloc(APPSBL_SIZE);

	memset(r, 0, sizeof(*r));
	if (!bc0 || !bc1 || !oem || !old)
		return -1;
	memset(old, 0xff, APPSBL_SIZE);
	for (uint32_t i = 0; i < 600000; i++)
		old[i] = (uint8_t)(i * 7 + 3);	/* stands for the OEM APPSBL */
	r->appsbl[0] = mem_nand_new("0:APPSBL", 0x500000, APPSBL_SIZE, old);
	r->appsbl[1] = mem_nand_new("0:APPSBL_1", 0x640000, APPSBL_SIZE, old);
	r->bc[0] = mem_nand_new("0:BOOTCONFIG", 0x100000, BC_SIZE, bc0);
	r->bc[1] = mem_nand_new("0:BOOTCONFIG1", 0x140000, BC_SIZE, bc1);
	r->slot[0] = mem_nand_new("rootfs_1", 0x900000, SLOT_SIZE, oem);
	r->slot[1] = mem_nand_new("rootfs", 0x3100000, SLOT_SIZE, NULL);
	for (int i = 0; i < 2; i++) {
		r->in.appsbl[i] = &r->appsbl[i]->f;
		r->in.ret.bc[i] = &r->bc[i]->f;
		r->in.ret.slot[i] = &r->slot[i]->f;
	}
	r->in.ret.running_offset = 0x3100000;
	free(bc0);
	free(bc1);
	free(oem);
	free(old);
	return 0;
}

static void rig_free(struct rig *r)
{
	for (int i = 0; i < 2; i++) {
		mem_nand_free(r->appsbl[i]);
		mem_nand_free(r->bc[i]);
		mem_nand_free(r->slot[i]);
	}
}

static int bc_selector(struct mem_nand *m)
{
	struct bb_bc bc;

	return bb_bc_parse(m->data, BC_SIZE, &bc) == BB_BC_OK ? (int)bc.rootfs_selector : -1;
}

int main(void)
{
	const char *pack_path = getenv("BB_PACK");
	const struct bb_candidate *cand = NULL;
	struct bb_inst_plan plan;
	struct bb_pack pack;
	struct bb_rules rules;
	struct rig r;
	uint8_t *buf, *mbn = NULL, *orig_active;
	size_t len, mbn_len = 0;
	char *text;

	fixtures = getenv("BB_FIXTURES");
	if (!fixtures || !pack_path) {
		printf("BB_FIXTURES and BB_PACK must be set\n");
		return 77;
	}
	buf = load(pack_path, &len);
	CHECK(buf && bb_pack_open(buf, len, &pack) == BB_PACK_OK, "pack opens");
	if (!buf)
		return 1;

	/* Rules come out of the pack; pick the HW-B GD5F ECC8 candidate. */
	text = strndup(pack.rules, pack.rules_len);
	CHECK(!bb_rules_load(&rules, text, NULL), "pack rules load");
	for (unsigned i = 0; i < rules.ncand; i++)
		if (!strcmp(rules.cand[i].id, "B-h3c-oem-c841-128m-2k-sp128-ecc8"))
			cand = &rules.cand[i];
	CHECK(cand, "candidate present");
	if (!cand)
		return 1;
	CHECK(bb_pack_extract(&pack, cand->file, &mbn, &mbn_len) == BB_PACK_OK && mbn_len == cand->bytes,
	      "extract %s (%zu bytes)", cand->file, mbn_len);
	{
		uint8_t *x;
		size_t xl;
		const struct bb_candidate *last = &rules.cand[rules.ncand - 1];

		CHECK(bb_pack_extract(&pack, last->file, &x, &xl) == BB_PACK_OK && xl == last->bytes,
		      "extract last member %s", last->file);
		free(x);
		CHECK(bb_pack_extract(&pack, "candidates/nope.mbn", &x, &xl) == BB_PACK_NOT_FOUND,
		      "missing member");
	}

	/* Happy path. */
	CHECK(!rig_init(&r), "rig");
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.code == BB_INST_READY, "plan: %s", bb_inst_code_str(plan.code));
	/* Field BOOTCONFIG: APPSBL selector 0, so 0:APPSBL (lower) is active. */
	CHECK(plan.first == 1 && plan.second == 0, "order first=%d second=%d", plan.first, plan.second);
	CHECK(bb_inst_commit(&r.in, &plan) == BB_INST_DONE, "commit: %s", bb_inst_code_str(plan.code));
	for (int i = 0; i < 2; i++)
		CHECK(!memcmp(r.appsbl[i]->data, plan.image, APPSBL_SIZE), "copy %d holds image", i);
	CHECK(bc_selector(r.bc[0]) == 0 && bc_selector(r.bc[1]) == 0, "BOOTCONFIG on OEM slot");
	bb_inst_plan_free(&plan);

	/* Running it again: nothing left to write. */
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.code == BB_INST_READY && plan.already[0] && plan.already[1], "already installed");
	CHECK(bb_inst_commit(&r.in, &plan) == BB_INST_DONE && r.appsbl[0]->erases == 10,
	      "second commit writes nothing (%d erases)", r.appsbl[0]->erases);
	bb_inst_plan_free(&plan);
	rig_free(&r);

	/* First (inactive) copy fails: the active copy must be untouched. */
	rig_init(&r);
	orig_active = malloc(APPSBL_SIZE);
	memcpy(orig_active, r.appsbl[0]->data, APPSBL_SIZE);
	r.appsbl[1]->fail_write_at = 5;
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(bb_inst_commit(&r.in, &plan) == BB_INST_WRITE_FIRST, "first fail: %s",
	      bb_inst_code_str(plan.code));
	CHECK(!memcmp(r.appsbl[0]->data, orig_active, APPSBL_SIZE), "active copy untouched");
	CHECK(bc_selector(r.bc[0]) == 1, "BOOTCONFIG unchanged after failure");
	bb_inst_plan_free(&plan);
	rig_free(&r);

	/* Second copy fails: first is complete, BOOTCONFIG unchanged. */
	rig_init(&r);
	r.appsbl[0]->fail_erase_at = 3;
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(bb_inst_commit(&r.in, &plan) == BB_INST_WRITE_SECOND, "second fail: %s",
	      bb_inst_code_str(plan.code));
	CHECK(!memcmp(r.appsbl[1]->data, plan.image, APPSBL_SIZE), "first copy complete");
	CHECK(bc_selector(r.bc[0]) == 1, "BOOTCONFIG unchanged");
	bb_inst_plan_free(&plan);
	/* A retry finishes the job. */
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.already[1] && !plan.already[0], "retry sees first copy done");
	CHECK(bb_inst_commit(&r.in, &plan) == BB_INST_DONE, "retry: %s", bb_inst_code_str(plan.code));
	bb_inst_plan_free(&plan);
	rig_free(&r);
	free(orig_active);

	/* Conflicting APPSBL selectors: never guess an inactive copy. */
	rig_init(&r);
	*(uint32_t *)(r.bc[1]->data + 28 + 20 * 3) = 1;
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.code == BB_INST_BOOTCONFIG, "conflicting selectors: %s",
	      bb_inst_code_str(plan.code));
	CHECK(bb_inst_commit(&r.in, &plan) == BB_INST_BOOTCONFIG &&
	      !r.appsbl[0]->erases && !r.appsbl[1]->erases,
	      "conflict leaves both APPSBL copies untouched");
	bb_inst_plan_free(&plan);
	rig_free(&r);

	/* Selector 1: fail writing the lower inactive copy, preserve upper active. */
	rig_init(&r);
	for (int i = 0; i < 2; i++)
		*(uint32_t *)(r.bc[i]->data + 28 + 20 * 3) = 1;
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.code == BB_INST_READY && plan.first == 0 && plan.second == 1,
	      "selector 1 reverses write order");
	r.appsbl[0]->fail_erase_at = 1;
	CHECK(bb_inst_commit(&r.in, &plan) == BB_INST_WRITE_FIRST && !r.appsbl[1]->erases,
	      "upper active copy untouched after lower failure");
	bb_inst_plan_free(&plan);
	rig_free(&r);

	/* Invalid and missing APPSBL selectors must not default to zero. */
	for (int mode = 0; mode < 2; mode++) {
		rig_init(&r);
		for (int i = 0; i < 2; i++) {
			if (mode)
				r.bc[i]->data[12 + 20 * 3] = 'x';
			else
				*(uint32_t *)(r.bc[i]->data + 28 + 20 * 3) = 2;
		}
		bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
		CHECK(plan.code == BB_INST_BOOTCONFIG, "invalid/missing APPSBL selector mode %d", mode);
		bb_inst_plan_free(&plan);
		rig_free(&r);
	}

	/* Refusals. */
	rig_init(&r);
	mbn[1000] ^= 1;
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.code == BB_INST_PAYLOAD, "corrupt payload: %s", bb_inst_code_str(plan.code));
	mbn[1000] ^= 1;
	bb_inst_plan_free(&plan);

	r.appsbl[1]->bad[3] = 1;
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.code == BB_INST_BAD_BLOCK, "bad block: %s", bb_inst_code_str(plan.code));
	bb_inst_plan_free(&plan);
	r.appsbl[1]->bad[3] = 0;

	memset(r.slot[0]->data, 0xff, SLOT_SIZE);	/* OEM system gone */
	bb_inst_plan(&r.in, cand, mbn, mbn_len, &plan);
	CHECK(plan.code == BB_INST_RETURN, "no OEM return: %s", bb_inst_code_str(plan.code));
	bb_inst_plan_free(&plan);
	rig_free(&r);

	/* Pack integrity. */
	buf[200] ^= 1;
	CHECK(bb_pack_open(buf, len, &pack) == BB_PACK_HASH, "pack hash");
	buf[0] = 'X';
	CHECK(bb_pack_open(buf, len, &pack) == BB_PACK_NONE, "no pack");

	free(mbn);
	free(text);
	bb_rules_free(&rules);
	free(buf);
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
