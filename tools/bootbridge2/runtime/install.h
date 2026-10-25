/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Install a U-Boot candidate into both APPSBL copies, then point BOOTCONFIG
 * back at the OEM system so the next boot runs OEM firmware on the new U-Boot.
 *
 * Order: the copy the boot chain does NOT currently use is written first and
 * verified; only then the active copy. BOOTCONFIG is touched last, through
 * the independent OEM return module. */
#ifndef BB_INSTALL_H
#define BB_INSTALL_H

#include "oem_return.h"
#include "rules.h"

enum bb_inst_code {
	BB_INST_READY = 0,	/* plan computed, commit allowed */
	BB_INST_DONE,		/* both copies written + verified, BOOTCONFIG on OEM */
	BB_INST_PAYLOAD,	/* candidate bytes do not match the rule's size/hash */
	BB_INST_PARTITIONS,	/* APPSBL partitions missing, unequal or wrong size */
	BB_INST_BAD_BLOCK,	/* a bad block in an APPSBL partition */
	BB_INST_RETURN,		/* OEM return is not possible: install refused */
	BB_INST_WRITE_FIRST,	/* inactive copy failed; the active copy is untouched */
	BB_INST_WRITE_SECOND,	/* active copy failed after the inactive one succeeded */
	BB_INST_RETURN_FAILED,	/* U-Boot installed, but BOOTCONFIG could not be switched */
	BB_INST_NO_MEM,
	BB_INST_BOOTCONFIG,	/* active APPSBL cannot be determined safely */
};

struct bb_inst_inputs {
	const struct bb_flash *appsbl[2];	/* 0:APPSBL, 0:APPSBL_1 in any order */
	struct bb_ret_inputs ret;
};

struct bb_inst_plan {
	enum bb_inst_code code;
	uint8_t *image;			/* full partition image: payload + 0xff */
	uint32_t image_len;
	int first, second;		/* indexes into appsbl[]: write order */
	int already[2];			/* copy already holds the image */
	struct bb_ret_plan ret;
	enum bb_ret_code ret_code;
};

void bb_inst_plan(const struct bb_inst_inputs *in, const struct bb_candidate *c,
		  const uint8_t *payload, size_t len, struct bb_inst_plan *p);
enum bb_inst_code bb_inst_commit(const struct bb_inst_inputs *in, struct bb_inst_plan *p);
void bb_inst_plan_free(struct bb_inst_plan *p);

const char *bb_inst_code_str(enum bb_inst_code c);
const char *bb_inst_code_text(enum bb_inst_code c);

#endif
