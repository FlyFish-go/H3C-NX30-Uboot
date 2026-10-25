/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Rule file: plain text, one statement per line, '#' starts a comment.
 *
 *   bbrules 1
 *   set <rule set id>
 *   board <group> <cond>...            e.g. board B route=mp02.1 art5g@3a=50
 *   nand <profile> <cond>...           e.g. nand h3c-oem-c841-... nand.id=c8:41
 *   candidate <id> board=<g> nand=<profile> ecc=<n> bytes=<n> sha256=<hex>
 *             part_bytes=<n> part_sha256=<hex> file=<path>
 *     (part_* describe the whole APPSBL partition after install: image + 0xff)
 *
 * A condition is key=value; value may list alternatives separated by '|'.
 * Keys: route, machid, nand.id, nand.model, nand.mib, nand.page, nand.erase_kib,
 * nand.oob, ecc, product, item, and art5g@<hex offset> whose value is hex bytes
 * compared at that offset of the 5 GHz calibration block.
 *
 * Every rule of a kind is evaluated. Exactly one board group, one NAND
 * profile and then one candidate must result; anything else is refused. */
#ifndef BB_RULES_H
#define BB_RULES_H

#include "facts.h"

#define BB_RULES_MAX_CANDIDATES 128

struct bb_candidate {
	char id[80];
	char board[8];
	char nand[64];
	unsigned ecc;
	unsigned bytes;
	char sha256[65];
	unsigned part_bytes;
	char part_sha256[65];
	char file[128];
};

struct bb_rules {
	char set[48];
	char *text;		/* owned copy of the rule file */
	unsigned ncand;
	struct bb_candidate cand[BB_RULES_MAX_CANDIDATES];
};

enum bb_decide {
	BB_DEC_OK = 0,
	BB_DEC_NO_BOARD,	/* no board rule matched */
	BB_DEC_AMBIGUOUS_BOARD,	/* rules for different groups matched */
	BB_DEC_NO_NAND,
	BB_DEC_AMBIGUOUS_NAND,
	BB_DEC_NO_CANDIDATE,
	BB_DEC_AMBIGUOUS_CANDIDATE,
};

struct bb_decision {
	enum bb_decide code;
	char board[64];
	char nand[64];
	const struct bb_candidate *cand;
	char detail[256];	/* which groups/profiles matched, for diagnostics */
};

/* Parse a rule file. Returns 0, or -1 with a line number in *bad_line. */
int bb_rules_load(struct bb_rules *r, const char *text, unsigned *bad_line);
void bb_rules_free(struct bb_rules *r);

void bb_rules_decide(const struct bb_rules *r, const struct bb_facts *f, struct bb_decision *d);
const char *bb_decide_str(enum bb_decide c);
const char *bb_decide_text(enum bb_decide c);

#endif
