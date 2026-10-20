/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BOOTBRIDGE_DISPATCH_H
#define BOOTBRIDGE_DISPATCH_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define BI_PROVIDER_BUNDLE_ABI_VERSION 2u
#define BDI_COMPATIBLE_MAX 2048u
#define BDI_MODEL_MAX 64u

typedef struct {
    const char *profile_id;
    const char *const *model_aliases;
    size_t model_alias_count;
    unsigned jedec_bytes_observed;
    unsigned manufacturer_id;
    unsigned chip_id;
    unsigned capacity_mib;
    unsigned page_bytes;
    unsigned erase_bytes;
    unsigned raw_spare_bytes;
    unsigned mtd_oob_bytes;
    unsigned ecc_bits;
    unsigned catalogue_eligible;
} bdi_nand_profile;

typedef struct {
    const char *record_id;
    const char *candidate_id;
    const char *hardware_class;
    const char *source_sha256;
    const char *product;
    const char *route;
    const char *route_compatible;
    const char *source_revision_sha256;
    const char *stock_kernel_sha256;
    const char *bridge_kernel_sha256;
    const char *nand_dt_sha256;
    const char *profile_set_id;
    const char *candidate_set_id;
    const bdi_nand_profile *nand_profiles;
    size_t nand_profile_count;
    const char *provider_path;
    const char *provider_sha256;
    unsigned provider_bytes;
    const char *payload_path;
    const char *payload_sha256;
    unsigned payload_bytes;
} bdi_provider_record;

typedef struct {
    uint8_t compatible[BDI_COMPATIBLE_MAX];
    size_t compatible_bytes;
    char nand_model[BDI_MODEL_MAX];
    unsigned jedec_bytes_observed;
    unsigned manufacturer_id;
    unsigned chip_id;
    unsigned capacity_mib;
    unsigned page_bytes;
    unsigned erase_bytes;
    unsigned raw_spare_bytes;
    unsigned mtd_oob_bytes;
    unsigned ecc_bits;
    unsigned have_compatible;
    unsigned have_nand_model;
    unsigned have_jedec;
    unsigned have_geometry;
    unsigned have_mtd_format;
} bdi_facts;

typedef enum {
    BDI_OK = 0,
    BDI_BAD_BUNDLE,
    BDI_FACTS_MISSING,
    BDI_FACTS_COMPAT_INVALID,
    BDI_FACTS_KERNEL_ID_INVALID,
    BDI_FACTS_MTD_SYSFS_INVALID,
    BDI_FACTS_MTD_DEVICE_INVALID,
    BDI_FACTS_PROC_MTD_INVALID,
    BDI_FACTS_CONFLICT,
    BDI_NO_MATCH,
    BDI_CANDIDATE_UNAVAILABLE,
    BDI_CANDIDATE_AMBIGUOUS,
    BDI_AMBIGUOUS,
    BDI_BAD_PATH,
    BDI_BAD_FILE,
    BDI_BAD_HASH,
    BDI_EXEC_FAILED
} bdi_result;

typedef struct {
    unsigned offset;
    unsigned size;
    unsigned erase_bytes;
    unsigned page_bytes;
    unsigned oob_bytes;
    unsigned ecc_bits;
    unsigned dev_major;
    unsigned dev_minor;
} bdi_mtd_sysfs_values;

/* Text parsers are shared by the production collector and host fixtures. */
int bdi_parse_compatible(const uint8_t *data, size_t bytes, bdi_facts *facts);
int bdi_parse_kernel_identity(const char *data, size_t bytes, bdi_facts *facts);
int bdi_parse_mtd_sysfs(const char *type, const char *offset, const char *size,
                        const char *erase, const char *page, const char *oob,
                        const char *ecc, const char *dev,
                        bdi_mtd_sysfs_values *values);
/* offset_read_status follows read_sysfs_attr_optional(): 0=present, 1=absent,
 * -1=read error. This shared adapter keeps collector/parser polarity aligned. */
int bdi_parse_mtd_sysfs_optional_offset(int offset_read_status, const char *offset,
                        const char *type, const char *size, const char *erase,
                        const char *page, const char *oob, const char *ecc,
                        const char *dev, bdi_mtd_sysfs_values *values);
int bdi_parse_proc_mtd0(const char *data, size_t bytes, unsigned expected_size,
                        unsigned expected_erase);

/* Pure source-bound matcher used by production and host tests. A result is
 * returned only when the complete runtime tuple identifies exactly one row. */
const bdi_provider_record *bdi_select_provider(
    const bdi_provider_record *records, size_t count, const char *bundle_source_sha256,
    const char *bundle_product, const bdi_facts *facts,
    const bdi_nand_profile **matched_profile, bdi_result *result);
/* Resolve full read-only candidate probes for flat variant rows. `ready[i]`
 * is 1 for an exact install-ready contract, 0 for a valid nonmatch, and -1
 * for an invalid/failed probe. */
const bdi_provider_record *bdi_select_inspected_provider(
    const bdi_provider_record *records, size_t count, const char *bundle_source_sha256,
    const char *bundle_product, const bdi_facts *facts, const int *ready,
    const bdi_nand_profile **matched_profile, bdi_result *result);
int bdi_inspect_report_matches(const char *json, const bdi_provider_record *record);

/* Open an absolute bundle path beneath rootfd without following any path
 * component; verify a root-owned, non-writable regular file and exact length
 * and digest. The returned descriptor is positioned at offset zero. */
int bdi_open_verified_at(int rootfd, const char *path, const char *sha256,
                         uint64_t bytes, int executable, bdi_result *result);

/* Resolve the kernel's /sys/class/mtd/mtd0 link only when it resolves inside
 * the same sysfs tree at an exact .../mtd/mtd0 endpoint. Exported for fixture
 * tests of the legitimate class symlink and escape/mismatch rejection. */
int bdi_resolve_mtd0_sysfs(const char *root, char *resolved, size_t cap);

const char *bdi_result_name(bdi_result result);

#endif
