/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Recovery backup: a stored (uncompressed) ZIP of the boot area partitions
 * and the OEM persistent data, ECC-decoded main area only, plus manifest.json.
 * The two system slots are not included. Unreadable blocks are filled with
 * 0xff and listed in the manifest. */
#ifndef BB_BACKUP_H
#define BB_BACKUP_H

#include "flash.h"

#include <stdio.h>

#define BB_BACKUP_MAX 32

struct bb_backup_entry {
	char file[48];			/* name inside the ZIP */
	const struct bb_flash *part;
	const char *role;		/* "startup" or "oem-persistent-data" */
};

struct bb_backup_result {
	uint64_t bytes;			/* ZIP size */
	char sha256[65];		/* of the ZIP */
	unsigned entries;
	unsigned bad_blocks;
	unsigned unreadable_blocks;
};

/* Write the ZIP to `out`. Returns 0 or a negative errno. */
int bb_backup_write(FILE *out, const struct bb_backup_entry *e, unsigned n,
		    struct bb_backup_result *r);

#endif
