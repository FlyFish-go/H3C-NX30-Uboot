#ifndef BRIDGE_CAPABILITY_H
#define BRIDGE_CAPABILITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BB_CAP_SHA256_BYTES 32U
#define BB_CAP_SESSION_ID_BYTES 16U
#define BB_CAP_ECC4_MAX_BITFLIPS 4U /* per 512-byte ECC codeword */
#define BB_CAP_ECC4_STEP_BYTES 512U
/* Fixed memory bound: the qualified 0x140000-byte APPSBL extent contains 640
 * 2 KiB pages. Larger candidate ranges fail closed until this bound is
 * deliberately revised and reviewed. */
#define BB_CAP_MAX_APPSBL_PAGES 640U
#define BB_CAP_APPSBL_BITMAP_BYTES ((BB_CAP_MAX_APPSBL_PAGES + 7U) / 8U)
#define BB_CAP_CURRENT_SCHEMA 1U
#define BB_CAP_MAX_GENERATION 0xffffffffU
#define BB_CAP_MAX_WRITE_TARGETS 2U

enum bb_cap_state {
	BB_CAP_UNKNOWN = 0,
	BB_CAP_VALID = 1,
	BB_CAP_IO_ERROR = 2,
	BB_CAP_CONFLICT = 3,
	BB_CAP_CHANGED = 4
};

enum bb_cap_origin {
	BB_CAP_ORIGIN_NONE = 0,
	BB_CAP_ORIGIN_MMIO = 1,
	BB_CAP_ORIGIN_SMEM = 2,
	BB_CAP_ORIGIN_MDIO = 3,
	BB_CAP_ORIGIN_QPIC = 4,
	BB_CAP_ORIGIN_MTD_READ = 5,
	BB_CAP_ORIGIN_DT = 6,
	BB_CAP_ORIGIN_BOUNDED_LAYOUT = 7,
	BB_CAP_ORIGIN_READONLY_MDIO = 8,
	BB_CAP_ORIGIN_WIRED_TOPOLOGY = 9,
	BB_CAP_ORIGIN_MAC_VALIDATION = 10,
	BB_CAP_ORIGIN_SESSION_PROVIDER = 11
};

enum bb_cap_value {
	BB_CAP_VALUE_NONE = 0,
	BB_CAP_VALUE_IPQ50XX = 1,
	BB_CAP_VALUE_ROUTE_MP02_1 = 1,
	BB_CAP_VALUE_ROUTE_MP03_1_C2 = 2,
	BB_CAP_VALUE_SWITCH_UNKNOWN = 0,
	BB_CAP_VALUE_SWITCH_QCA8337 = 1,
	BB_CAP_VALUE_SWITCH_RTL8367S = 2,
	BB_CAP_VALUE_MAC_INVALID = 0,
	BB_CAP_VALUE_MAC_UNICAST = 1,
	BB_CAP_VALUE_MAC_VOLATILE_LOCAL = 2,
	BB_CAP_VALUE_ECC4 = 4,
	BB_CAP_VALUE_ECC8 = 8,
	BB_CAP_VALUE_ECC4_AND_8 = 12,
	BB_CAP_VALUE_LAYOUT_UNKNOWN = 0,
	BB_CAP_VALUE_LAYOUT_B_FIXED_SLOTS = 1,
	BB_CAP_VALUE_LAYOUT_OEM_MP02_1 = 2,
	BB_CAP_VALUE_BC_UNKNOWN = 0,
	BB_CAP_VALUE_BC_TWO_COPIES_MATCH = 1,
	BB_CAP_VALUE_BC_FALLBACK_VALID = 2,
	BB_CAP_VALUE_BC_CONFLICT = 3,
	BB_CAP_VALUE_SBL_INCOMPATIBLE = 0,
	BB_CAP_VALUE_SBL_COMPATIBLE = 1,
	BB_CAP_VALUE_WINDOW_NONE = 0,
	BB_CAP_VALUE_WINDOW_BC_ONLY = 1,
	BB_CAP_VALUE_WINDOW_APPSBL_EXACT = 2,
	BB_CAP_VALUE_LINUX_ECC4 = 4,
	BB_CAP_VALUE_LINUX_ECC8 = 8,
	BB_CAP_VALUE_BOOT_NAND = 1,
	BB_CAP_VALUE_BOOT_NOR = 2
};

enum bb_cap_operation {
	BB_CAP_OP_WIRED_RECOVERY = 0,
	BB_CAP_OP_NAND_READ = 1,
	BB_CAP_OP_NAND_METADATA_SAMPLE = 2,
	BB_CAP_OP_SYSTEM_BOOT = 3,
	BB_CAP_OP_BC_TRANSACTION = 4,
	BB_CAP_OP_APPSBL_WRITE = 5,
	BB_CAP_OP_COUNT = 6
};

enum bb_cap_mac_policy {
	BB_CAP_MAC_REQUIRE_UNICAST = 1,
	BB_CAP_MAC_ALLOW_VOLATILE_LOCAL = 2
};

enum bb_cap_grant_state {
	BB_CAP_DENIED = 0,
	BB_CAP_GRANTED = 1
};

#define BB_CAP_OP_BIT(op) (1U << (unsigned int)(op))
#define BB_CAP_ECC_BIT_ECC4 (1U << 0)
#define BB_CAP_ECC_BIT_ECC8 (1U << 1)
#define BB_CAP_PHY_MASK_BIT(mask) (1U << (unsigned int)(mask))

enum bb_cap_reason {
	BB_CAP_REASON_NONE = 0,
	BB_CAP_REASON_BAD_ARGUMENT,
	BB_CAP_REASON_BAD_CANDIDATE,
	BB_CAP_REASON_CANDIDATE_SCOPE,
	BB_CAP_REASON_CANDIDATE_UNSUPPORTED,
	BB_CAP_REASON_EVIDENCE_UNKNOWN,
	BB_CAP_REASON_EVIDENCE_INVALID,
	BB_CAP_REASON_EVIDENCE_CONFLICT,
	BB_CAP_REASON_EVIDENCE_STALE,
	BB_CAP_REASON_ORIGIN_MISMATCH,
	BB_CAP_REASON_CANDIDATE_MISMATCH,
	BB_CAP_REASON_PHY_TOPOLOGY_MISMATCH,
	BB_CAP_REASON_NO_LAN_PATH,
	BB_CAP_REASON_MAC_UNUSABLE,
	BB_CAP_REASON_ECC_UNSUPPORTED,
	BB_CAP_REASON_LAYOUT_UNSUPPORTED,
	BB_CAP_REASON_CHAIN_UNPROVEN,
	BB_CAP_REASON_BC_UNSAFE,
	BB_CAP_REASON_WRITE_WINDOW_MISMATCH,
	BB_CAP_REASON_TARGET_MISMATCH,
	BB_CAP_REASON_ACTIVE_TARGET_MISMATCH,
	BB_CAP_REASON_SESSION_MISMATCH,
	BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE,
	BB_CAP_REASON_DIGEST_MISMATCH
};

struct bb_cap_observation {
	enum bb_cap_state state;
	enum bb_cap_origin origin;
	uint64_t value;
	uint32_t coverage_bytes;
	uint32_t generation;
	uint8_t stable_reads;
};

struct bb_cap_digest_observation {
	enum bb_cap_state state;
	enum bb_cap_origin origin;
	uint8_t sha256[BB_CAP_SHA256_BYTES];
	uint64_t offset;
	uint32_t coverage_bytes;
	uint32_t generation;
	uint8_t stable_reads;
};

struct bb_cap_range {
	uint64_t offset;
	uint64_t size;
};

struct bb_cap_range_observation {
	enum bb_cap_state state;
	enum bb_cap_origin origin;
	struct bb_cap_range range;
	uint32_t generation;
	uint8_t stable_reads;
};

/* APPSBL-write evidence must be captured in one fresh boot/session and pinned
 * to the exact immutable candidate and source contract. IDs are opaque
 * digests/tokens supplied by a trusted observer, never inferred from product
 * strings or NAND IDs. */
struct bb_cap_evidence_envelope {
	enum bb_cap_state state;
	enum bb_cap_origin origin;
	uint8_t boot_id_sha256[BB_CAP_SHA256_BYTES];
	uint8_t session_id[BB_CAP_SESSION_ID_BYTES];
	uint8_t candidate_sha256[BB_CAP_SHA256_BYTES];
	uint8_t source_contract_sha256[BB_CAP_SHA256_BYTES];
	uint32_t generation;
};

/* One complete ECC4 decoded-main-data pass over a physical APPSBL copy.
 * corrected counts are reported, not ignored; a nonzero count is acceptable
 * only when every page has a counter sample and the decoded extent hash is
 * the manifest-pinned expected copy hash. Bad-block skipping is not qualified
 * by this schema, so any bad block closes the decision. The trusted reader
 * sets each page-index bit on its deterministic physical-page loop, refuses
 * a second set of an already-set bit, counts out-of-range indexes, then seals
 * the aggregate. The pure evaluator verifies bitmap/count closure but cannot
 * authenticate that caller-populated process. */
struct bb_cap_appsbl_copy_scan {
	enum bb_cap_state state;
	enum bb_cap_origin origin;
	uint64_t offset;
	uint64_t size;
	uint8_t boot_id_sha256[BB_CAP_SHA256_BYTES];
	uint8_t session_id[BB_CAP_SESSION_ID_BYTES];
	uint32_t generation;
	uint32_t ecc_mode;
	uint32_t page_bytes;
	uint32_t ecc_step_bytes;
	uint32_t total_pages;
	uint32_t covered_pages;
	uint32_t read_ok_pages;
	uint32_t read_failed_pages;
	uint32_t uncorrectable_pages;
	uint32_t counter_pages;
	uint32_t corrected_pages;
	uint64_t corrected_bitflips;
	uint32_t max_bitflips;
	uint32_t bad_blocks;
	uint32_t read_page_samples;
	uint32_t counter_page_samples;
	uint32_t duplicate_read_pages;
	uint32_t duplicate_counter_pages;
	uint32_t out_of_range_samples;
	uint8_t read_page_bitmap[BB_CAP_APPSBL_BITMAP_BYTES];
	uint8_t counter_page_bitmap[BB_CAP_APPSBL_BITMAP_BYTES];
	uint8_t decoded_sha256[BB_CAP_SHA256_BYTES];
};

struct bb_cap_facts {
	uint32_t generation;
	struct bb_cap_observation soc_class;
	struct bb_cap_observation route;
	struct bb_cap_observation dram_bytes;
	struct bb_cap_observation switch_kind;
	struct bb_cap_observation phy_mask;
	struct bb_cap_observation lan_link_mask;
	struct bb_cap_observation mac_policy;
	struct bb_cap_observation nand_size_bytes;
	struct bb_cap_observation nand_read_only;
	struct bb_cap_observation nand_io_ecc;
	struct bb_cap_observation nand_ecc_sample;
	struct bb_cap_observation boot_source;
	struct bb_cap_observation pbl_ecc;
	struct bb_cap_observation appsbl_storage_ecc;
	struct bb_cap_observation target_appsbl_storage_ecc;
	struct bb_cap_digest_observation sbl_digest;
	struct bb_cap_observation layout;
	struct bb_cap_observation bootconfig_ecc;
	struct bb_cap_observation bootconfig_pair;
	struct bb_cap_observation write_target_count;
	struct bb_cap_range_observation write_target[BB_CAP_MAX_WRITE_TARGETS];
	struct bb_cap_observation active_appsbl_index;
	struct bb_cap_observation target_appsbl_index;
	struct bb_cap_observation linux_handoff_route;
	struct bb_cap_observation linux_handoff_ecc;
	struct bb_cap_observation write_window;
	struct bb_cap_appsbl_copy_scan appsbl_copy[2];
	/* Issued by the current trusted invocation/provider. This independent
	 * binding prevents an old internally-consistent envelope from being
	 * replayed as current evidence. Appended to preserve existing field offsets. */
	uint8_t current_boot_id_sha256[BB_CAP_SHA256_BYTES];
	uint8_t current_session_id[BB_CAP_SESSION_ID_BYTES];
	struct bb_cap_evidence_envelope evidence;
};

/* Requirements bind an immutable candidate to capabilities; they do not
 * identify the physical product. PHY masks are exact wired topologies (B=0x0e,
 * D=0x0f per the captured canonical B/D DT audit). Facts must come from a
 * bounded topology provider, not the payload's own DT/profile label. A dual
 * build must name both exact masks and carry both implementations. */
struct bb_cap_candidate_requirements {
	uint32_t schema;
	uint8_t payload_sha256[BB_CAP_SHA256_BYTES];
	uint8_t source_contract_sha256[BB_CAP_SHA256_BYTES];
	uint32_t supported_operations;
	uint32_t accepted_routes;
	uint32_t accepted_switches;
	uint32_t accepted_phy_masks;
	uint32_t accepted_layouts;
	uint32_t supported_ecc_modes;
	uint32_t expected_boot_source;
	uint32_t expected_pbl_ecc;
	uint32_t expected_appsbl_ecc;
	uint32_t expected_write_window;
	uint32_t write_target_count;
	struct bb_cap_range write_targets[BB_CAP_MAX_WRITE_TARGETS];
	uint8_t required_sbl_sha256[BB_CAP_SHA256_BYTES];
	uint64_t required_sbl_offset;
	uint32_t required_sbl_bytes;
	uint64_t minimum_dram_bytes;
	uint64_t minimum_nand_bytes;
	enum bb_cap_mac_policy mac_policy;
	/* Appended so existing candidate requirement field offsets are preserved. */
	uint32_t appsbl_page_bytes;
	uint8_t expected_appsbl_copy_sha256[2][BB_CAP_SHA256_BYTES];
	uint32_t appsbl_ecc_step_bytes;
};

struct bb_cap_operation_result {
	enum bb_cap_reason reason;
	enum bb_cap_grant_state state;
};

struct bb_cap_decision {
	uint32_t granted_operations;
	uint32_t denied_operations;
	struct bb_cap_operation_result operation[BB_CAP_OP_COUNT];
};

/* Evaluate only requested operations. Candidate hashes/requirements must be
 * validated by the caller's trusted manifest path before this pure function is
 * called. This module intentionally has no model, machid, NAND-ID, ENV, or UI
 * qualification input. */
int bb_capability_evaluate(const struct bb_cap_facts *facts,
			   const struct bb_cap_candidate_requirements *candidate,
			   uint32_t requested_operations,
			   struct bb_cap_decision *decision);

const char *bb_capability_reason_name(enum bb_cap_reason reason);

#ifdef __cplusplus
}
#endif

#endif
