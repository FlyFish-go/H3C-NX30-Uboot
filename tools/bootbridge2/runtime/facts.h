/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Read-only hardware facts. The runtime only collects; deciding what the
 * facts mean (board group, candidate) is the rule pack's job. Nothing secret
 * from ENV (passwords, keys, MACs, serial suffixes) is kept. */
#ifndef BB_FACTS_H
#define BB_FACTS_H

#include <stddef.h>
#include <stdint.h>

#define BB_ART5G_OFFSET		0x26800u	/* QCN6122 calibration block in 0:ART */
#define BB_ART5G_BYTES		0x60u

struct bb_facts {
	/* Boot route: from the device tree model the OEM APPSBL selected,
	 * e.g. "Qualcomm Technologies, Inc. IPQ5018/AP-MP02.1" -> "mp02.1". */
	int have_route;
	char dt_model[96];
	char route[24];

	/* Platform tuple from 0:CDT. */
	int have_machid;
	uint32_t machid;

	/* NAND as probed by the running kernel plus the MTD view of it. */
	int have_nand_id, have_nand_model, have_nand_geometry;
	unsigned nand_mfr, nand_chip;
	char nand_vendor[32], nand_model[64];
	unsigned nand_mib, nand_erase_kib, nand_page, nand_oob;
	unsigned mtd_oob, mtd_ecc_strength;

	/* First bytes of the 5 GHz calibration block (FEM flags live here). */
	int have_art5g;
	uint8_t art5g[BB_ART5G_BYTES];

	/* Non-secret ENV fields. */
	int env_crc_ok;
	char productname[32];
	char item_code[11];	/* first 10 characters of manucode only */

	/* SHA-256 of both APPSBL partitions, "" when unreadable. */
	char appsbl_sha256[2][65];
};

/* Parsers, pure and host-testable. Each returns 0 when it found what it needs. */
int bb_facts_parse_dt_model(struct bb_facts *f, const char *model);
int bb_facts_parse_cdt(struct bb_facts *f, const uint8_t *cdt, size_t len);
int bb_facts_parse_kmsg(struct bb_facts *f, const char *log);
int bb_facts_parse_env(struct bb_facts *f, const uint8_t *env, size_t len);
int bb_facts_parse_art(struct bb_facts *f, const uint8_t *art, size_t len);

/* Collect everything on the running device (facts_linux.c). Missing pieces
 * are left with their have_* flag cleared. */
void bb_facts_collect(struct bb_facts *f);

/* Write the facts as one JSON object. Returns bytes written or -1. */
int bb_facts_json(const struct bb_facts *f, char *out, size_t len);

#endif
