/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BRIDGE_INSTALL_IDENTITY_H
#define BRIDGE_INSTALL_IDENTITY_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BII_OK=0, BII_POLICY, BII_SIZE, BII_CRC, BII_FORMAT,
    BII_PRODUCT, BII_ENV_MACHID, BII_CMDLINE_MACHID, BII_BOOTARGS
} bii_code;

typedef struct {
    char board[16], product[64], machid[16];
    unsigned have_bootcmd, have_bootargs, trial_only;
} bii_result;

/* The board and candidate identify one exact provider payload. The official
 * source digest and embedded model identify a reviewed source context; the
 * exact source/observed-product/board matrix below controls ENV productname.
 * NX30_2i5x is accepted only for NX source/D. Product identity never detects
 * hardware. ENV bootcmd is informational:
 * this candidate's release autoboot path ignores imported commands and calls
 * bootipq directly. */
bii_code bii_check_public_candidate(const char *board, const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid,
                                   const char *candidate_sha256,
                                   const uint8_t *env, size_t env_bytes,
                                   const char *cmdline, bii_result *out);
/* Preserve all present ENV/cmdline assertions. For the exact RC R013L01/C
 * source only, a unique live C-compatible route may supply the platform
 * assertion absent from that observed stock ENV. No context keeps the
 * original strict missing-field behavior. This never grants write readiness. */
bii_code bii_check_public_candidate_runtime(const char *board, const char *source_product,
                                   const char *official_source_sha256,
                                   const char *machid, const char *candidate_sha256,
                                   const uint8_t *env, size_t env_bytes,
                                   const char *cmdline, const uint8_t *compatible,
                                   size_t compatible_bytes, bii_result *out);
/* Exact source-to-observed-ENV product mapping for OEM return. The active DT
 * route is checked against the compiled source registry by the caller. */
int bii_return_env_product_allowed(const char *official_source_sha256,
                                   const char *source_product,
                                   const char *route,
                                   const char *observed_product);
const char *bii_code_name(bii_code);
#endif
