/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_B_BOOTARGS_H__
#define __RT3000_B_BOOTARGS_H__

#include <common.h>

#define RT3_BOOTARGS_MAX_SOURCES 3
#define RT3_BOOTARGS_LABEL_MAX 64

/* Resolve an explicit qcom_nand.0 mtdparts map against a physical extent.
 * found is zero only when no source contains an explicit mtdparts mapping.
 */
int rt3_bootargs_resolve_mtd_label(const char * const *args,
                                  const size_t *lengths,
                                  unsigned int count,
                                  u64 offset, u64 size,
                                  char *label, size_t label_size,
                                  int *found);

/* Compose B NAND bootargs from a selected, uncompressed FIT FDT. */
int rt3_bootargs_compose(const char *base_args, const char *fit_args,
                        const char *fallback_args, const void *fdt,
                        size_t fdt_size, u64 offset, u64 size,
                        char *output, size_t output_size);

#endif
