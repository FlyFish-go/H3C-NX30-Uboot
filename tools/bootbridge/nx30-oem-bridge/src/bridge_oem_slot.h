#ifndef BRIDGE_OEM_SLOT_H
#define BRIDGE_OEM_SLOT_H
#include <stddef.h>
#include <stdint.h>
/* No device paths, writes, selector interpretation or bootability claims.
 * read_at must fill exactly len bytes from a stable, ECC-decoded snapshot,
 * returning zero on success. Offsets are relative to the supplied slot.
 * Caller authenticates the manifest (e.g. compiled, versioned official rule),
 * resolves physical range independently, and verifies boot records separately.
 * An untrusted manifest supplied beside an image proves nothing. */
typedef int (*bos_read_at)(void *, uint64_t, void *, size_t);
typedef struct {
    uint64_t physical_offset, size_bytes;
    uint32_t erase_bytes, page_bytes, oob_bytes, ecc_bits;
} bos_geometry;
typedef struct { uint64_t size_bytes; uint8_t sha256[32]; uint32_t type; } bos_volume;
typedef struct {
    bos_geometry geometry;
    uint8_t candidate_sha256[32]; /* binds rule to bridge candidate */
    uint8_t volume_table_sha256[32]; /* exact two-copy official table */
    bos_volume immutable[4]; /* kernel,wifi_fw,bt_fw,ubi_rootfs */
} bos_manifest;
typedef enum {
    BOS_OK=0, BOS_ARGUMENT, BOS_UNTRUSTED_BINDING, BOS_UNSUPPORTED,
    BOS_IO, BOS_EC, BOS_VID, BOS_TABLE, BOS_VOLUME, BOS_STATIC_CRC,
    BOS_DIGEST, BOS_CHANGED, BOS_NO_MATCH, BOS_AMBIGUOUS, BOS_POLICY_UNKNOWN, BOS_LIMIT
} bos_code;
typedef struct {
    bos_code code;
    uint32_t failure_peb, scanned_pebs, rootfs_data_lebs;
    int logical_content_verified; /* never means hardware/boot chain PASS */
    uint8_t snapshot_sha256[32];
} bos_result;
/* Supports only 40MiB/128KiB/2KiB OEM five-volume layout; unrecognized
 * features fail closed. rootfs_data may have sparse LEBs and arbitrary data.
 * A narrow dynamic copy-flag record is accepted only with valid bounded data
 * CRC and a unique in-range LEB; duplicate-LEB arbitration is unsupported.
 * Filesystem health, kernel runtime and NAND OOB/ECC are NOT validated. */
bos_code bos_verify(bos_read_at read_at, void *context,
                    const bos_geometry *observed, const bos_manifest *trusted,
                    const uint8_t candidate_sha256[32], bos_result *result);
/* Pool API v2: measurements contain four whole-stream SHA256 values, not
 * leaf aggregates. This result is trusted in-process evidence, never a runtime
 * JSON trust token. Only compiled, audited rules may be supplied to selection.
 * Neither content groups nor exit-policy IDs certify bootability by themselves.
 */
#define BOS_POOL_MAX 64u
#define BOS_ALIAS_MAX 8u
#define BOS_EXIT_UNAVAILABLE 0u
#define BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0 1u
/* Installed, exact candidate's bootipq handoff. This does not qualify the
 * retained OEM APPSBL for the separate BC-only return operation. */
#define BOS_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V1 2u
#define BOS_PRODUCT_NX30 1u
#define BOS_PRODUCT_RC3000 2u
#define BOS_PRODUCT_RT3000 3u
#define BOS_PRODUCT_RW3000 4u
typedef enum {
    BOS_TABLE_RAW_EXACT=0,
    BOS_TABLE_AUTORESIZE_UNAVAILABLE=1,
    /* Public, compiled source rules only: match a runtime table after UBI
     * changes rootfs_data reserve/auto-resize fields on first attach. */
    BOS_TABLE_POSTATTACH_AUTORESIZE=2
} bos_table_policy;
typedef struct {
    bos_manifest content;
    uint32_t content_group, exit_policy, product, alias;
    bos_table_policy table_policy;
} bos_pool_rule;
typedef struct {
    bos_geometry geometry;
    uint8_t volume_table_sha256[32], snapshot_sha256[32];
    bos_volume immutable[4];
    uint32_t scanned_pebs, rootfs_data_lebs;
    uint8_t normalized_volume_table_sha256[32];
    uint32_t normalized_volume_table_valid;
} bos_measurement;
typedef struct {
    uint32_t content_group, exit_policy, product, aliases;
    uint8_t candidate_sha256[32], pool_revision_sha256[32];
    int logical_content_verified;
} bos_pool_match;
/* One physical structure pass plus one ordered immutable stream pass, <=80MiB
 * per measurement independent of pool count. Caller controls concurrent writers;
 * the physical digest includes mutable rootfs_data and must be transaction-bound.
 * Internal allocation <=256KiB; output allocation is caller-owned. */
bos_code bos_measure(bos_read_at,void *,const bos_geometry *,bos_measurement *);
bos_code bos_pool_select(const bos_measurement *,const bos_pool_rule *,size_t,
                         const uint8_t candidate_sha256[32],
                         const uint8_t compiled_pool_revision_sha256[32],bos_pool_match *);
#endif
