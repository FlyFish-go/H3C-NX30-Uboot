// SPDX-License-Identifier: GPL-2.0-or-later
/* bb-oem-return: standalone "return to OEM system" tool.
 *
 *   bb-oem-return check    read-only, prints a JSON report
 *   bb-oem-return commit   checks again, then rewrites both BOOTCONFIG copies
 *
 * The two rootfs slots are the OEM partitions "rootfs" and "rootfs_1"; other
 * names (e.g. on a QSDK test system) can be given after the command.
 *
 * Exit status: 0 ready/already/done, 1 refused, 2 write failure, 3 usage. */
#include "flash_mtd.h"
#include "oem_return.h"
#include "operation_lock.h"

#include <stdio.h>
#include <sys/reboot.h>
#include <stdlib.h>
#include <string.h>

static int sys_int(const char *path, int *out)
{
	FILE *f = fopen(path, "r");
	int ok;

	if (!f)
		return -1;
	ok = fscanf(f, "%d", out) == 1;
	fclose(f);
	return ok ? 0 : -1;
}

/* Physical offset of the rootfs slot that an attached UBI device sits on. */
static int running_offset(const struct bb_flash *slots, uint32_t *out)
{
	char path[64];

	for (int u = 0; u < 8; u++) {
		struct bb_flash f;
		int mtd;

		snprintf(path, sizeof(path), "/sys/class/ubi/ubi%d/mtd_num", u);
		if (sys_int(path, &mtd) || bb_mtd_open(mtd, 0, &f))
			continue;
		f.close(f.ctx);
		if (f.offset == slots[0].offset || f.offset == slots[1].offset) {
			*out = f.offset;
			return 0;
		}
	}
	return -1;
}

static void json_report(const struct bb_ret_plan *p, uint32_t running)
{
	printf("{\"schema\":\"bb-oem-return-v1\",\"code\":\"%s\",\"message\":\"%s\","
	       "\"running_offset\":%u,\"oem_offset\":%u,\"target_selector\":%u,"
	       "\"oem_slot\":{\"status\":\"%s\",\"pebs\":%u,\"bad\":%u,\"unreadable\":%u,"
	       "\"volumes\":%u,\"kernel_lebs\":%u,\"kernel_bytes\":%u,\"kernel_static\":%s},"
	       "\"bootconfig\":[",
	       bb_ret_code_str(p->code), bb_ret_code_text(p->code), running, p->oem_offset,
	       p->target_selector, bb_ubi_status_str(p->ubi_status), p->ubi.pebs,
	       p->ubi.bad_pebs, p->ubi.unreadable, p->ubi.volumes, p->ubi.kernel_lebs,
	       p->ubi.kernel_bytes, p->ubi.kernel_static ? "true" : "false");
	for (int c = 0; c < 2; c++)
		printf("%s{\"status\":\"%s\",\"age\":%u,\"selector\":%u}", c ? "," : "",
		       bb_bc_status_str(p->bc_status[c]), p->bc[c].age, p->bc[c].rootfs_selector);
	printf("],\"new_age\":%u}\n", p->new_age);
}

int main(int argc, char **argv)
{
	static const char *const bc_names[2] = { "0:BOOTCONFIG", "0:BOOTCONFIG1" };
	const char *slot_names[2] = { "rootfs", "rootfs_1" };
	struct bb_flash bc[2], slots[2];
	struct bb_ret_inputs in = {0};
	struct bb_ret_plan plan;
	int commit, rc, lock = -1;

	if ((argc != 2 && argc != 4) || (strcmp(argv[1], "check") && strcmp(argv[1], "commit") &&
	     strcmp(argv[1], "commit-reboot"))) {
		fprintf(stderr, "usage: %s check|commit|commit-reboot [slot_a slot_b]\n", argv[0]);
		return 3;
	}
	if (argc == 4) {
		slot_names[0] = argv[2];
		slot_names[1] = argv[3];
	}
	commit = strcmp(argv[1], "check") != 0;
	if (commit && (lock = bb_operation_lock()) < 0) {
		puts("{\"schema\":\"bb-oem-return-v1\",\"code\":\"busy\","
		     "\"message\":\"另一项操作正在进行，或无法获取操作锁\"}");
		return 1;
	}

	memset(&plan, 0, sizeof(plan));
	plan.code = BB_RET_BAD_SLOTS;
	for (int i = 0; i < 2; i++) {
		int idx = bb_mtd_find(slot_names[i], 0);

		if (idx < 0 || bb_mtd_open(idx, 0, &slots[i]))
			goto report;
		in.slot[i] = &slots[i];
	}
	if (running_offset(slots, &in.running_offset))
		goto report;
	plan.code = BB_RET_BC_IO;
	for (int i = 0; i < 2; i++) {
		int idx = bb_mtd_find(bc_names[i], 0);

		if (idx < 0 || bb_mtd_open(idx, commit, &bc[i]))
			goto report;
		in.bc[i] = &bc[i];
	}

	bb_ret_plan(&in, &plan);
	if (commit && plan.code == BB_RET_READY)
		bb_ret_commit(&in, &plan);
report:
	json_report(&plan, in.running_offset);
	switch (plan.code) {
	case BB_RET_READY:
	case BB_RET_ALREADY:
	case BB_RET_DONE:
		rc = 0;
		break;
	case BB_RET_WRITE_BC1:
	case BB_RET_WRITE_BC0:
		rc = 2;
		break;
	default:
		rc = 1;
	}
	bb_ret_plan_free(&plan);
	for (int i = 0; i < 2; i++) {
		if (in.bc[i])
			bc[i].close(bc[i].ctx);
		if (in.slot[i])
			slots[i].close(slots[i].ctx);
	}
	if (!rc && commit && !strcmp(argv[1], "commit-reboot")) {
		sync();
		sleep(1);
		if (reboot(RB_AUTOBOOT)) {
			perror("bb-oem-return: reboot");
			rc = 2;
		}
	}
	if (lock >= 0)
		close(lock);
	return rc;
}
