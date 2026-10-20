#ifndef BRIDGE_OEM_RETURN_H
#define BRIDGE_OEM_RETURN_H
#include <stdint.h>
/* Explicit compiled exit rule, separate from descriptive source metadata and
 * candidate installation qualification. No HTTP/JSON or runtime approval bit.
 * An unavailable provider is not replaced by neutral/profile/NAND labels. */
#define BR_RETURN_BC_WRITE_UNKNOWN 0u
#define BR_RETURN_BC_WRITE_REVIEWED_LINUX_MTD_V1 1u
#define BR_RETURN_EXIT_SOURCE_BOOTIPQ_V1 1u
#define BR_RETURN_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V2 2u
#define BR_RETURN_ENV_UNKNOWN 0u
#define BR_RETURN_ENV_EXPLICIT_BOOTIPQ_V1 1u
typedef struct {
    uint32_t id, product, exit_policy;
    const char *source_sha256, *stock_kernel_sha256, *bridge_kernel_sha256;
    const char *source_revision_sha256, *route, *compatible, *nand_profile;
    uint32_t ecc_bits, mtd_oob_bytes, bc_write_policy, env_policy;
    const char *compressed_kernel_sha256, *nand_dt_sha256, *evidence_revision_sha256;
} br_return_rule;
typedef struct {
    const char *official_sha256, *stock_kernel_sha256, *bridge_kernel_sha256;
    const char *source_revision_sha256, *compressed_kernel_sha256, *nand_dt_sha256;
    const char *route, *compatible;
    const char *sbl_sha256, *appsbl_sha256;
    uint32_t sbl_valid_bytes, appsbl_valid_bytes;
    uint32_t product;
} br_return_source_record;
/* Build headers may provide truthful BI_RETURN_SOURCE_* fields independently.
 * Only an explicitly reviewed BI_RETURN_RULES/COUNT table authorizes an exit
 * combination. Tables are bounded at2048 and ambiguities fail closed. */
#endif
