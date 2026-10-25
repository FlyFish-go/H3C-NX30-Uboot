/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Return to the OEM system: point the BOOTCONFIG rootfs selector back at the
 * slot the bridge is NOT running from.
 *
 * Deliberately independent of everything else in BootBridge: it does not look
 * at ENV, rules, candidates, board identification or install state. It only
 * requires that the OEM slot still holds a bootable UBI system and that the
 * BOOTCONFIG copies can be parsed and safely rewritten. */
#ifndef BB_OEM_RETURN_H
#define BB_OEM_RETURN_H

#include "bootconfig.h"
#include "flash.h"
#include "ubi_check.h"

enum bb_ret_code {
	BB_RET_READY = 0,	/* plan computed, commit allowed */
	BB_RET_ALREADY,		/* both copies already select the OEM slot */
	BB_RET_DONE,		/* commit finished and verified */
	BB_RET_BAD_SLOTS,	/* rootfs slots missing / unequal / running slot unknown */
	BB_RET_OEM_SLOT,	/* OEM slot failed the UBI check */
	BB_RET_BC_IO,		/* BOOTCONFIG read error or bad block 0 */
	BB_RET_BC_INVALID,	/* neither copy parses */
	BB_RET_CHANGED,		/* flash changed between plan and commit */
	BB_RET_WRITE_BC1,	/* BOOTCONFIG1 write failed; BOOTCONFIG untouched */
	BB_RET_WRITE_BC0,	/* BOOTCONFIG write failed after BOOTCONFIG1 succeeded */
	BB_RET_NO_MEM,
};

struct bb_ret_inputs {
	const struct bb_flash *bc[2];		/* BOOTCONFIG, BOOTCONFIG1 */
	const struct bb_flash *slot[2];		/* rootfs partitions, any order */
	uint32_t running_offset;		/* physical offset of the slot we run from */
};

struct bb_ret_plan {
	enum bb_ret_code code;
	uint32_t oem_offset;
	uint32_t target_selector;	/* selector value that boots the OEM slot */
	uint32_t new_age;
	int valid[2];			/* which BOOTCONFIG copies parsed */
	struct bb_bc bc[2];
	enum bb_bc_status bc_status[2];
	enum bb_ubi_status ubi_status;
	struct bb_ubi_report ubi;
	uint8_t *image[2];		/* block 0 of each copy as read */
	uint8_t *updated[2];		/* block 0 after the change */
};

/* Read-only: inspect flash and decide. Always fills `plan->code`. */
void bb_ret_plan(const struct bb_ret_inputs *in, struct bb_ret_plan *plan);

/* Write BOOTCONFIG1 then BOOTCONFIG, each verified by read-back. Requires a
 * plan with code BB_RET_READY made from the same inputs. Returns the result. */
enum bb_ret_code bb_ret_commit(const struct bb_ret_inputs *in, struct bb_ret_plan *plan);

void bb_ret_plan_free(struct bb_ret_plan *plan);
const char *bb_ret_code_str(enum bb_ret_code c);
const char *bb_ret_code_text(enum bb_ret_code c);	/* Chinese, for the page */

#endif
