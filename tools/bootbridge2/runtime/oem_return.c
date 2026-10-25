// SPDX-License-Identifier: GPL-2.0-or-later
#include "oem_return.h"

#include <stdlib.h>
#include <string.h>

/* Selector 0 boots the lower of the two rootfs slots, 1 the upper one. This
 * matches every unit seen so far (OEM 0x900000 / 0x3100000 layout). */
static int slot_for_offset(const struct bb_ret_inputs *in, uint32_t off)
{
	const struct bb_flash *lo = in->slot[0], *hi = in->slot[1];

	if (lo->offset > hi->offset) {
		const struct bb_flash *t = lo;

		lo = hi;
		hi = t;
	}
	if (off == lo->offset)
		return 0;
	if (off == hi->offset)
		return 1;
	return -1;
}

static const struct bb_flash *slot_by_selector(const struct bb_ret_inputs *in, uint32_t sel)
{
	const struct bb_flash *a = in->slot[0], *b = in->slot[1];
	const struct bb_flash *lo = a->offset < b->offset ? a : b;

	return sel == 0 ? lo : (lo == a ? b : a);
}

void bb_ret_plan(const struct bb_ret_inputs *in, struct bb_ret_plan *plan)
{
	int running, both_target = 1;
	uint32_t age = 0;

	memset(plan, 0, sizeof(*plan));
	plan->code = BB_RET_BAD_SLOTS;
	if (!in->slot[0] || !in->slot[1] || in->slot[0]->size != in->slot[1]->size ||
	    in->slot[0]->offset == in->slot[1]->offset)
		return;
	running = slot_for_offset(in, in->running_offset);
	if (running < 0)
		return;
	plan->target_selector = !running;
	plan->oem_offset = slot_by_selector(in, plan->target_selector)->offset;

	plan->ubi_status = bb_ubi_check(slot_by_selector(in, plan->target_selector), &plan->ubi);
	if (plan->ubi_status != BB_UBI_OK) {
		plan->code = plan->ubi_status == BB_UBI_NO_MEM ? BB_RET_NO_MEM : BB_RET_OEM_SLOT;
		return;
	}

	for (int c = 0; c < 2; c++) {
		const struct bb_flash *f = in->bc[c];

		plan->code = BB_RET_BC_IO;
		if (!f || f->is_bad(f->ctx, 0))
			return;
		plan->code = BB_RET_NO_MEM;
		plan->image[c] = malloc(f->erase_size);
		plan->updated[c] = malloc(f->erase_size);
		if (!plan->image[c] || !plan->updated[c])
			return;
		plan->code = BB_RET_BC_IO;
		if (bb_flash_read(f, 0, plan->image[c], f->erase_size))
			return;
		plan->bc_status[c] = bb_bc_parse(plan->image[c], f->erase_size, &plan->bc[c]);
		plan->valid[c] = plan->bc_status[c] == BB_BC_OK;
		if (plan->valid[c]) {
			if (plan->bc[c].age > age)
				age = plan->bc[c].age;
			if (plan->bc[c].rootfs_selector != plan->target_selector)
				both_target = 0;
		}
	}
	/* One unreadable copy is tolerated (the boot chain falls back to the
	 * other); it is rebuilt from the valid copy so both end up correct. */
	plan->code = BB_RET_BC_INVALID;
	if (!plan->valid[0] && !plan->valid[1])
		return;
	if (plan->valid[0] && plan->valid[1] && both_target) {
		plan->code = BB_RET_ALREADY;
		return;
	}
	plan->new_age = age + 1;
	for (int c = 0; c < 2; c++) {
		int src = plan->valid[c] ? c : !c;

		memcpy(plan->updated[c], plan->image[src], in->bc[c]->erase_size);
		bb_bc_set(plan->updated[c], &plan->bc[src], plan->target_selector, plan->new_age);
	}
	plan->code = BB_RET_READY;
}

static int unchanged(const struct bb_flash *f, const uint8_t *expect)
{
	uint8_t *now = malloc(f->erase_size);
	int same;

	if (!now)
		return 0;
	same = !bb_flash_read(f, 0, now, f->erase_size) && !memcmp(now, expect, f->erase_size);
	free(now);
	return same;
}

enum bb_ret_code bb_ret_commit(const struct bb_ret_inputs *in, struct bb_ret_plan *plan)
{
	if (plan->code != BB_RET_READY)
		return plan->code;
	if (in->bc[0]->erase_size != in->bc[1]->erase_size)
		return plan->code = BB_RET_BC_IO;
	if (!unchanged(in->bc[0], plan->image[0]) || !unchanged(in->bc[1], plan->image[1]))
		return plan->code = BB_RET_CHANGED;
	/* Backup copy first: if power fails here the primary still boots the
	 * bridge, which can retry. Once the primary is being rewritten, the boot
	 * chain falls back to the already updated backup. */
	if (bb_flash_rewrite_block(in->bc[1], 0, plan->updated[1]))
		return plan->code = BB_RET_WRITE_BC1;
	if (bb_flash_rewrite_block(in->bc[0], 0, plan->updated[0]))
		return plan->code = BB_RET_WRITE_BC0;
	return plan->code = BB_RET_DONE;
}

void bb_ret_plan_free(struct bb_ret_plan *plan)
{
	for (int c = 0; c < 2; c++) {
		free(plan->image[c]);
		free(plan->updated[c]);
		plan->image[c] = plan->updated[c] = NULL;
	}
}

const char *bb_ret_code_str(enum bb_ret_code c)
{
	switch (c) {
	case BB_RET_READY: return "ready";
	case BB_RET_ALREADY: return "already_oem";
	case BB_RET_DONE: return "done";
	case BB_RET_BAD_SLOTS: return "bad_slots";
	case BB_RET_OEM_SLOT: return "oem_slot_invalid";
	case BB_RET_BC_IO: return "bootconfig_io";
	case BB_RET_BC_INVALID: return "bootconfig_invalid";
	case BB_RET_CHANGED: return "changed";
	case BB_RET_WRITE_BC1: return "write_bootconfig1_failed";
	case BB_RET_WRITE_BC0: return "write_bootconfig_failed";
	case BB_RET_NO_MEM: return "no_memory";
	}
	return "unknown";
}

const char *bb_ret_code_text(enum bb_ret_code c)
{
	switch (c) {
	case BB_RET_READY: return "可以返回原厂系统";
	case BB_RET_ALREADY: return "启动配置已经指向原厂系统，重启即可";
	case BB_RET_DONE: return "已切换到原厂系统，重启后生效";
	case BB_RET_BAD_SLOTS: return "找不到两个系统槽，或无法确定当前运行的槽";
	case BB_RET_OEM_SLOT: return "原厂系统槽已损坏，不能返回";
	case BB_RET_BC_IO: return "启动配置分区读取失败或是坏块";
	case BB_RET_BC_INVALID: return "两份启动配置都无法解析";
	case BB_RET_CHANGED: return "启动配置在检查之后发生了变化，请重新检查";
	case BB_RET_WRITE_BC1: return "备份启动配置写入失败，主启动配置未改动，当前仍会启动桥接系统";
	case BB_RET_WRITE_BC0: return "主启动配置写入失败；备份启动配置已指向原厂系统，重启后应由备份引导";
	case BB_RET_NO_MEM: return "内存不足";
	}
	return "未知错误";
}
