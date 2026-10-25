// SPDX-License-Identifier: GPL-2.0-or-later
#include "../runtime/backup.h"
#include "flash_mem.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	struct mem_nand *m = mem_nand_new("0:APPSBL", 0x500000, 0x20000, NULL);
	struct bb_backup_entry entry = { .part = &m->f, .role = "startup", .file = "appsbl.bin" };
	struct bb_backup_result result;
	FILE *f = tmpfile();

	if (!f || bb_backup_write(f, &entry, 1, &result) || bb_backup_verify(f, &result))
		return 1;
	/* Same size with corrupted bytes: a successful write alone is insufficient. */
	if (fseek(f, 100, SEEK_SET) || fputc(0, f) == EOF || fflush(f) || !bb_backup_verify(f, &result))
		return 1;
	if (ftruncate(fileno(f), 80) || !bb_backup_verify(f, &result))
		return 1;
	fclose(f);
	mem_nand_free(m);
	puts("backup read-back: valid accepted, corruption/truncation refused PASS");
	return 0;
}
