/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __H3C_HW_A_RECOVERY_H__
#define __H3C_HW_A_RECOVERY_H__

#include <common.h>

int h3c_hw_a_recovery_fit_validate(const void *upload, size_t size,
				   char *sha256_hex, size_t hex_cap);
int h3c_hw_a_recovery_fit_confirm(const void *upload, size_t size,
				  ulong *staged_addr, char *config,
				  size_t config_cap);
int h3c_hw_a_recovery_fit_boot(ulong staged_addr, const char *config);
void h3c_hw_a_recovery_fit_clear(void);

/* JSON object used when a diagnostic FIT fails validation. */
#define H3C_HW_A_RECOVERY_FIT_INVALID_INFO "{\"type\":\"recovery_fit_invalid\"}"

#endif
