#include "bridge_capability.h"

#include <string.h>

#define BB_CAP_REQUIRED_BYTES_MIN 1U

static enum bb_cap_reason require_observation(const struct bb_cap_observation *o,
						     uint32_t generation,
						     uint32_t minimum_bytes,
						     uint32_t allowed_origins);

static int bytes_nonzero(const uint8_t *bytes, uint32_t length)
{
	uint32_t i;
	for (i = 0; i < length; ++i) {
		if (bytes[i] != 0U)
			return 1;
	}
	return 0;
}

static int multiply_u64_checked(uint64_t left, uint64_t right, uint64_t *result)
{
	if (result == NULL || (left != 0U && right > UINT64_MAX / left))
		return 0;
	*result = left * right;
	return 1;
}

static uint32_t ecc_bit(uint64_t mode)
{
	if (mode == BB_CAP_VALUE_ECC4)
		return BB_CAP_ECC_BIT_ECC4;
	if (mode == BB_CAP_VALUE_ECC8)
		return BB_CAP_ECC_BIT_ECC8;
	return 0U;
}

static enum bb_cap_reason observation_reason(const struct bb_cap_observation *o,
						    uint32_t generation,
						    uint32_t minimum_bytes,
						    uint32_t allowed_origins)
{
	if (o->state == BB_CAP_UNKNOWN)
		return BB_CAP_REASON_EVIDENCE_UNKNOWN;
	if (o->state == BB_CAP_IO_ERROR)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->state == BB_CAP_CONFLICT)
		return BB_CAP_REASON_EVIDENCE_CONFLICT;
	if (o->state == BB_CAP_CHANGED)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (o->state != BB_CAP_VALID || o->value == 0U ||
	    o->coverage_bytes < minimum_bytes || o->stable_reads < 2U)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->generation != generation)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (o->origin >= 32U || (allowed_origins & (1U << (unsigned int)o->origin)) == 0U)
		return BB_CAP_REASON_ORIGIN_MISMATCH;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason observation_state_reason(
	const struct bb_cap_observation *o, uint32_t generation,
	uint32_t allowed_origins)
{
	if (o->state == BB_CAP_UNKNOWN)
		return BB_CAP_REASON_EVIDENCE_UNKNOWN;
	if (o->state == BB_CAP_IO_ERROR)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->state == BB_CAP_CONFLICT)
		return BB_CAP_REASON_EVIDENCE_CONFLICT;
	if (o->state == BB_CAP_CHANGED)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (o->state != BB_CAP_VALID || o->stable_reads < 2U)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->generation != generation)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (o->origin >= 32U || (allowed_origins & (1U << (unsigned int)o->origin)) == 0U)
		return BB_CAP_REASON_ORIGIN_MISMATCH;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason digest_reason(const struct bb_cap_digest_observation *o,
						uint32_t generation,
						uint32_t expected_bytes,
						uint32_t allowed_origins)
{
	if (o->state == BB_CAP_UNKNOWN)
		return BB_CAP_REASON_EVIDENCE_UNKNOWN;
	if (o->state == BB_CAP_IO_ERROR)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->state == BB_CAP_CONFLICT)
		return BB_CAP_REASON_EVIDENCE_CONFLICT;
	if (o->state == BB_CAP_CHANGED)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (o->state != BB_CAP_VALID || o->coverage_bytes == 0U ||
	    o->coverage_bytes < expected_bytes ||
	    o->stable_reads < 2U || !bytes_nonzero(o->sha256, BB_CAP_SHA256_BYTES))
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->generation != generation)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (o->origin >= 32U || (allowed_origins & (1U << (unsigned int)o->origin)) == 0U)
		return BB_CAP_REASON_ORIGIN_MISMATCH;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason range_reason(const struct bb_cap_range_observation *o,
				       uint32_t generation)
{
	if (o->state == BB_CAP_UNKNOWN)
		return BB_CAP_REASON_EVIDENCE_UNKNOWN;
	if (o->state == BB_CAP_IO_ERROR)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->state == BB_CAP_CONFLICT)
		return BB_CAP_REASON_EVIDENCE_CONFLICT;
	if (o->state == BB_CAP_CHANGED || o->generation != generation)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (o->state != BB_CAP_VALID || o->range.size == 0U ||
	    o->range.offset > UINT64_MAX - o->range.size || o->stable_reads < 2U)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (o->origin != BB_CAP_ORIGIN_BOUNDED_LAYOUT)
		return BB_CAP_REASON_ORIGIN_MISMATCH;
	return BB_CAP_REASON_NONE;
}

static int same_range(const struct bb_cap_range *a, const struct bb_cap_range *b)
{
	return a->offset == b->offset && a->size == b->size;
}

static int ranges_overlap(const struct bb_cap_range *a, const struct bb_cap_range *b)
{
	return a->offset < b->offset + b->size && b->offset < a->offset + a->size;
}

static enum bb_cap_reason check_write_target_count(
	const struct bb_cap_facts *facts, uint32_t expected)
{
	enum bb_cap_reason reason = require_observation(&facts->write_target_count,
		facts->generation, BB_CAP_REQUIRED_BYTES_MIN,
		1U << BB_CAP_ORIGIN_BOUNDED_LAYOUT);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->write_target_count.value != expected ||
	    expected > BB_CAP_MAX_WRITE_TARGETS)
		return BB_CAP_REASON_TARGET_MISMATCH;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason check_exact_write_targets(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate,
	uint32_t count)
{
	uint32_t i;
	enum bb_cap_reason reason;
	if (candidate->write_target_count != count)
		return BB_CAP_REASON_BAD_CANDIDATE;
	reason = check_write_target_count(facts, count);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	for (i = 0; i < count; ++i) {
		reason = range_reason(&facts->write_target[i], facts->generation);
		if (reason != BB_CAP_REASON_NONE)
			return reason;
		if (candidate->write_targets[i].size == 0U ||
		    candidate->write_targets[i].offset > UINT64_MAX - candidate->write_targets[i].size ||
		    candidate->write_targets[i].offset + candidate->write_targets[i].size >
			facts->nand_size_bytes.value ||
		    !same_range(&facts->write_target[i].range, &candidate->write_targets[i]))
			return BB_CAP_REASON_TARGET_MISMATCH;
	}
	if (count == 2U && ranges_overlap(&candidate->write_targets[0],
					  &candidate->write_targets[1]))
		return BB_CAP_REASON_BAD_CANDIDATE;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason candidate_reason(
	const struct bb_cap_candidate_requirements *candidate)
{
	if (candidate->schema != BB_CAP_CURRENT_SCHEMA ||
	    !bytes_nonzero(candidate->payload_sha256, BB_CAP_SHA256_BYTES) ||
	    !bytes_nonzero(candidate->source_contract_sha256, BB_CAP_SHA256_BYTES) ||
	    candidate->supported_operations == 0U ||
	    (candidate->supported_operations & ~((1U << BB_CAP_OP_COUNT) - 1U)) != 0U)
		return BB_CAP_REASON_BAD_CANDIDATE;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason require_observation(const struct bb_cap_observation *o,
						     uint32_t generation,
						     uint32_t minimum_bytes,
						     uint32_t allowed_origins)
{
	return observation_reason(o, generation, minimum_bytes, allowed_origins);
}

static enum bb_cap_reason check_common_wired(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate)
{
	enum bb_cap_reason reason;
	uint32_t allowed;
	if (candidate->accepted_routes == 0U || candidate->accepted_switches == 0U ||
	    candidate->accepted_phy_masks == 0U || candidate->minimum_dram_bytes == 0U)
		return BB_CAP_REASON_BAD_CANDIDATE;

	allowed = (1U << BB_CAP_ORIGIN_SMEM) | (1U << BB_CAP_ORIGIN_MMIO);
	reason = require_observation(&facts->soc_class, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, allowed);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->soc_class.value != BB_CAP_VALUE_IPQ50XX)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;

	allowed = (1U << BB_CAP_ORIGIN_SMEM);
	reason = require_observation(&facts->dram_bytes, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, allowed);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->dram_bytes.value < candidate->minimum_dram_bytes)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;

	allowed = (1U << BB_CAP_ORIGIN_DT);
	reason = require_observation(&facts->route, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, allowed);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->route.value >= 32U ||
	    (candidate->accepted_routes & (1U << (unsigned int)facts->route.value)) == 0U)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;

	allowed = (1U << BB_CAP_ORIGIN_READONLY_MDIO);
	reason = require_observation(&facts->switch_kind, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, allowed);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->switch_kind.value >= 32U ||
	    (candidate->accepted_switches & (1U << (unsigned int)facts->switch_kind.value)) == 0U)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;

	reason = require_observation(&facts->phy_mask, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_WIRED_TOPOLOGY);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->phy_mask.value >= 32U ||
	    (candidate->accepted_phy_masks &
	     BB_CAP_PHY_MASK_BIT(facts->phy_mask.value)) == 0U)
		return BB_CAP_REASON_PHY_TOPOLOGY_MISMATCH;

	reason = require_observation(&facts->lan_link_mask, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_READONLY_MDIO);
	if (facts->lan_link_mask.state == BB_CAP_VALID &&
	    facts->lan_link_mask.value == 0U)
		return BB_CAP_REASON_NO_LAN_PATH;
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if ((facts->lan_link_mask.value & facts->phy_mask.value) == 0U)
		return BB_CAP_REASON_NO_LAN_PATH;

	reason = require_observation(&facts->mac_policy, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_MAC_VALIDATION);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (candidate->mac_policy != BB_CAP_MAC_REQUIRE_UNICAST &&
	    candidate->mac_policy != BB_CAP_MAC_ALLOW_VOLATILE_LOCAL)
		return BB_CAP_REASON_BAD_CANDIDATE;
	if (facts->mac_policy.value != BB_CAP_VALUE_MAC_UNICAST &&
	    !(facts->mac_policy.value == BB_CAP_VALUE_MAC_VOLATILE_LOCAL &&
	      candidate->mac_policy == BB_CAP_MAC_ALLOW_VOLATILE_LOCAL))
		return BB_CAP_REASON_MAC_UNUSABLE;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason check_nand_read(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate)
{
	enum bb_cap_reason reason;
	uint32_t ecc;
	if (candidate->supported_ecc_modes == 0U)
		return BB_CAP_REASON_BAD_CANDIDATE;

	reason = require_observation(&facts->nand_size_bytes, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_QPIC);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (candidate->minimum_nand_bytes == 0U ||
	    facts->nand_size_bytes.value < candidate->minimum_nand_bytes)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;
	reason = require_observation(&facts->nand_read_only, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_QPIC);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->nand_read_only.value != 1U)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	reason = require_observation(&facts->nand_io_ecc, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_QPIC);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	ecc = ecc_bit(facts->nand_io_ecc.value);
	if (ecc == 0U || (candidate->supported_ecc_modes & ecc) == 0U)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason check_chain_common(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate)
{
	enum bb_cap_reason reason;
	uint32_t ecc;
	if (candidate->expected_boot_source == 0U || candidate->accepted_layouts == 0U ||
	    candidate->expected_pbl_ecc == 0U || candidate->expected_appsbl_ecc == 0U ||
	    candidate->required_sbl_bytes == 0U ||
	    !bytes_nonzero(candidate->required_sbl_sha256, BB_CAP_SHA256_BYTES))
		return BB_CAP_REASON_BAD_CANDIDATE;

	/* A source observation is deliberately required to come from the bounded
	 * hardware handoff path, not a product string or the candidate label. */
	reason = require_observation(&facts->boot_source, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_MMIO);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->boot_source.value != candidate->expected_boot_source)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;

	reason = require_observation(&facts->pbl_ecc, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_MMIO);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	ecc = ecc_bit(facts->pbl_ecc.value);
	if (ecc == 0U || (candidate->supported_ecc_modes & ecc) == 0U ||
	    facts->pbl_ecc.value != candidate->expected_pbl_ecc)
		return BB_CAP_REASON_ECC_UNSUPPORTED;

	reason = require_observation(&facts->appsbl_storage_ecc, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	ecc = ecc_bit(facts->appsbl_storage_ecc.value);
	if (ecc == 0U || (candidate->supported_ecc_modes & ecc) == 0U ||
	    facts->appsbl_storage_ecc.value != candidate->expected_appsbl_ecc)
		return BB_CAP_REASON_ECC_UNSUPPORTED;

	reason = digest_reason(&facts->sbl_digest, facts->generation,
			       candidate->required_sbl_bytes,
			       1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->sbl_digest.offset != candidate->required_sbl_offset ||
	    memcmp(facts->sbl_digest.sha256, candidate->required_sbl_sha256,
		   BB_CAP_SHA256_BYTES) != 0)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;

	reason = require_observation(&facts->layout, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_BOUNDED_LAYOUT);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->layout.value >= 32U ||
	    (candidate->accepted_layouts & (1U << (unsigned int)facts->layout.value)) == 0U)
		return BB_CAP_REASON_LAYOUT_UNSUPPORTED;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason check_system_boot(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate)
{
	enum bb_cap_reason reason;
	uint32_t ecc;

	reason = check_chain_common(facts, candidate);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = check_nand_read(facts, candidate);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = require_observation(&facts->bootconfig_pair, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->bootconfig_pair.value != BB_CAP_VALUE_BC_TWO_COPIES_MATCH &&
	    facts->bootconfig_pair.value != BB_CAP_VALUE_BC_FALLBACK_VALID)
		return BB_CAP_REASON_BC_UNSAFE;

	reason = require_observation(&facts->nand_ecc_sample, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	ecc = ecc_bit(facts->nand_ecc_sample.value);
	if (ecc == 0U || (candidate->supported_ecc_modes & ecc) == 0U)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	reason = require_observation(&facts->linux_handoff_route, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_DT);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->linux_handoff_route.value != facts->route.value ||
	    facts->linux_handoff_route.value >= 32U ||
	    (candidate->accepted_routes &
	     (1U << (unsigned int)facts->linux_handoff_route.value)) == 0U)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;
	reason = require_observation(&facts->linux_handoff_ecc, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN, 1U << BB_CAP_ORIGIN_DT);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->linux_handoff_ecc.value != facts->nand_ecc_sample.value)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason check_bc_transaction(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate)
{
	enum bb_cap_reason reason = check_chain_common(facts, candidate);
	uint32_t ecc;
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = check_nand_read(facts, candidate);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = require_observation(&facts->bootconfig_pair, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->bootconfig_pair.value != BB_CAP_VALUE_BC_TWO_COPIES_MATCH)
		return BB_CAP_REASON_BC_UNSAFE;
	reason = require_observation(&facts->bootconfig_ecc, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->bootconfig_ecc.value != facts->nand_io_ecc.value)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	ecc = ecc_bit(facts->bootconfig_ecc.value);
	if (ecc == 0U || (candidate->supported_ecc_modes & ecc) == 0U)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	reason = require_observation(&facts->write_window, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_QPIC);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (candidate->expected_write_window != BB_CAP_VALUE_WINDOW_BC_ONLY ||
	    facts->write_window.value != BB_CAP_VALUE_WINDOW_BC_ONLY)
		return BB_CAP_REASON_WRITE_WINDOW_MISMATCH;
	reason = check_exact_write_targets(facts, candidate, 2U);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason check_appsbl_evidence_envelope(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate)
{
	const struct bb_cap_evidence_envelope *e = &facts->evidence;
	if (e->state == BB_CAP_UNKNOWN)
		return BB_CAP_REASON_EVIDENCE_UNKNOWN;
	if (e->state == BB_CAP_IO_ERROR)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (e->state == BB_CAP_CONFLICT)
		return BB_CAP_REASON_EVIDENCE_CONFLICT;
	if (e->state == BB_CAP_CHANGED)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (e->state != BB_CAP_VALID)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (e->origin != BB_CAP_ORIGIN_SESSION_PROVIDER)
		return BB_CAP_REASON_ORIGIN_MISMATCH;
	if (facts->generation == 0U || e->generation != facts->generation)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (!bytes_nonzero(e->boot_id_sha256, BB_CAP_SHA256_BYTES) ||
	    !bytes_nonzero(e->session_id, BB_CAP_SESSION_ID_BYTES))
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (memcmp(facts->current_boot_id_sha256, e->boot_id_sha256,
		   BB_CAP_SHA256_BYTES) != 0 ||
	    memcmp(facts->current_session_id, e->session_id,
		   BB_CAP_SESSION_ID_BYTES) != 0)
		return BB_CAP_REASON_SESSION_MISMATCH;
	if (memcmp(e->candidate_sha256, candidate->payload_sha256,
		   BB_CAP_SHA256_BYTES) != 0 ||
	    memcmp(e->source_contract_sha256, candidate->source_contract_sha256,
		   BB_CAP_SHA256_BYTES) != 0)
		return BB_CAP_REASON_CANDIDATE_MISMATCH;
	return BB_CAP_REASON_NONE;
}

static int appsbl_page_bitmap_complete(const uint8_t *bitmap, uint32_t pages)
{
	uint32_t page;
	if (pages == 0U || pages > BB_CAP_MAX_APPSBL_PAGES)
		return 0;
	for (page = 0U; page < pages; ++page) {
		if ((bitmap[page / 8U] & (uint8_t)(1U << (page % 8U))) == 0U)
			return 0;
	}
	/* Bits beyond the candidate-pinned extent must stay clear. This rejects
	 * tail samples that do not belong to the scanned physical range. */
	for (; page < BB_CAP_MAX_APPSBL_PAGES; ++page) {
		if ((bitmap[page / 8U] & (uint8_t)(1U << (page % 8U))) != 0U)
			return 0;
	}
	return 1;
}

static enum bb_cap_reason check_appsbl_copy_scan(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate,
	uint32_t index)
{
	const struct bb_cap_appsbl_copy_scan *s = &facts->appsbl_copy[index];
	const struct bb_cap_evidence_envelope *e = &facts->evidence;
	const struct bb_cap_range *expected = &candidate->write_targets[index];
	uint32_t expected_pages;
	uint64_t codewords_per_page;
	uint64_t corrected_bitflip_limit;

	if (s->state == BB_CAP_UNKNOWN)
		return BB_CAP_REASON_EVIDENCE_UNKNOWN;
	if (s->state == BB_CAP_IO_ERROR)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (s->state == BB_CAP_CONFLICT)
		return BB_CAP_REASON_EVIDENCE_CONFLICT;
	if (s->state == BB_CAP_CHANGED)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (s->state != BB_CAP_VALID)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (s->generation != facts->generation)
		return BB_CAP_REASON_EVIDENCE_STALE;
	if (memcmp(s->boot_id_sha256, e->boot_id_sha256,
		   BB_CAP_SHA256_BYTES) != 0 ||
	    memcmp(s->session_id, e->session_id,
		   BB_CAP_SESSION_ID_BYTES) != 0)
		return BB_CAP_REASON_SESSION_MISMATCH;
	if (s->origin != BB_CAP_ORIGIN_MTD_READ)
		return BB_CAP_REASON_ORIGIN_MISMATCH;
	if (s->ecc_mode != BB_CAP_VALUE_ECC4)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	if (candidate->appsbl_page_bytes == 0U ||
	    candidate->appsbl_ecc_step_bytes != BB_CAP_ECC4_STEP_BYTES ||
	    candidate->appsbl_page_bytes % candidate->appsbl_ecc_step_bytes != 0U ||
	    !bytes_nonzero(candidate->expected_appsbl_copy_sha256[index],
			   BB_CAP_SHA256_BYTES))
		return BB_CAP_REASON_BAD_CANDIDATE;
	if (s->ecc_step_bytes != candidate->appsbl_ecc_step_bytes)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	if (expected->size == 0U || expected->offset > UINT64_MAX - expected->size ||
	    expected->size % candidate->appsbl_page_bytes != 0U ||
	    s->offset != expected->offset || s->size != expected->size ||
	    s->page_bytes != candidate->appsbl_page_bytes)
		return BB_CAP_REASON_TARGET_MISMATCH;
	codewords_per_page = s->page_bytes / s->ecc_step_bytes;
	expected_pages = (uint32_t)(expected->size / candidate->appsbl_page_bytes);
	if (expected_pages > BB_CAP_MAX_APPSBL_PAGES ||
	    (uint64_t)expected_pages * candidate->appsbl_page_bytes != expected->size ||
	    s->total_pages != expected_pages || s->covered_pages != expected_pages ||
	    s->read_ok_pages != expected_pages || s->counter_pages != expected_pages ||
	    s->read_page_samples != expected_pages ||
	    s->counter_page_samples != expected_pages ||
	    s->duplicate_read_pages != 0U || s->duplicate_counter_pages != 0U ||
	    s->out_of_range_samples != 0U ||
	    !appsbl_page_bitmap_complete(s->read_page_bitmap, expected_pages) ||
	    !appsbl_page_bitmap_complete(s->counter_page_bitmap, expected_pages) ||
	    s->read_failed_pages != 0U || s->uncorrectable_pages != 0U ||
	    s->bad_blocks != 0U)
		return BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE;
	if (s->corrected_pages > expected_pages)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (s->corrected_pages == 0U) {
		if (s->corrected_bitflips != 0U || s->max_bitflips != 0U)
			return BB_CAP_REASON_EVIDENCE_INVALID;
	} else if (s->max_bitflips == 0U ||
		   s->max_bitflips > BB_CAP_ECC4_MAX_BITFLIPS ||
		   s->corrected_bitflips < s->corrected_pages ||
		   s->corrected_bitflips <
			(uint64_t)s->max_bitflips + s->corrected_pages - 1U ||
		   !multiply_u64_checked(s->corrected_pages,
					 codewords_per_page,
					 &corrected_bitflip_limit) ||
		   !multiply_u64_checked(corrected_bitflip_limit,
					 s->max_bitflips,
					 &corrected_bitflip_limit) ||
		   s->corrected_bitflips > corrected_bitflip_limit)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (!bytes_nonzero(s->decoded_sha256, BB_CAP_SHA256_BYTES))
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (memcmp(s->decoded_sha256, candidate->expected_appsbl_copy_sha256[index],
		   BB_CAP_SHA256_BYTES) != 0)
		return BB_CAP_REASON_DIGEST_MISMATCH;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason check_appsbl_write(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate)
{
	enum bb_cap_reason reason = check_chain_common(facts, candidate);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = check_nand_read(facts, candidate);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = check_appsbl_evidence_envelope(facts, candidate);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = check_appsbl_copy_scan(facts, candidate, 0U);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = check_appsbl_copy_scan(facts, candidate, 1U);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = require_observation(&facts->target_appsbl_storage_ecc, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->nand_io_ecc.value != facts->target_appsbl_storage_ecc.value ||
	    facts->target_appsbl_storage_ecc.value != candidate->expected_appsbl_ecc)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	reason = require_observation(&facts->bootconfig_ecc, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_MTD_READ);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->bootconfig_ecc.value != facts->nand_io_ecc.value)
		return BB_CAP_REASON_ECC_UNSUPPORTED;
	reason = require_observation(&facts->write_window, facts->generation,
				     BB_CAP_REQUIRED_BYTES_MIN,
				     1U << BB_CAP_ORIGIN_QPIC);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (candidate->expected_write_window != BB_CAP_VALUE_WINDOW_APPSBL_EXACT ||
	    facts->write_window.value != BB_CAP_VALUE_WINDOW_APPSBL_EXACT)
		return BB_CAP_REASON_WRITE_WINDOW_MISMATCH;
	reason = check_write_target_count(facts, 1U);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (candidate->write_target_count != 2U)
		return BB_CAP_REASON_BAD_CANDIDATE;
	reason = range_reason(&facts->write_target[0], facts->generation);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = observation_state_reason(&facts->active_appsbl_index, facts->generation,
					 1U << BB_CAP_ORIGIN_BOUNDED_LAYOUT);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	reason = observation_state_reason(&facts->target_appsbl_index, facts->generation,
					 1U << BB_CAP_ORIGIN_BOUNDED_LAYOUT);
	if (reason != BB_CAP_REASON_NONE)
		return reason;
	if (facts->active_appsbl_index.value > 1U ||
	    facts->target_appsbl_index.value > 1U)
		return BB_CAP_REASON_EVIDENCE_INVALID;
	if (facts->active_appsbl_index.value == facts->target_appsbl_index.value)
		return BB_CAP_REASON_ACTIVE_TARGET_MISMATCH;
	if (!same_range(&facts->write_target[0].range,
			&candidate->write_targets[facts->target_appsbl_index.value]))
		return BB_CAP_REASON_TARGET_MISMATCH;
	if (candidate->write_targets[0].size == 0U || candidate->write_targets[1].size == 0U ||
	    candidate->write_targets[0].offset > UINT64_MAX - candidate->write_targets[0].size ||
	    candidate->write_targets[1].offset > UINT64_MAX - candidate->write_targets[1].size ||
	    candidate->write_targets[0].offset + candidate->write_targets[0].size >
		facts->nand_size_bytes.value ||
	    candidate->write_targets[1].offset + candidate->write_targets[1].size >
		facts->nand_size_bytes.value ||
	    ranges_overlap(&candidate->write_targets[0], &candidate->write_targets[1]))
		return BB_CAP_REASON_BAD_CANDIDATE;
	return BB_CAP_REASON_NONE;
}

static enum bb_cap_reason evaluate_one(
	const struct bb_cap_facts *facts,
	const struct bb_cap_candidate_requirements *candidate,
	enum bb_cap_operation op)
{
	enum bb_cap_reason reason;
	uint32_t bit = BB_CAP_OP_BIT(op);

	if ((candidate->supported_operations & bit) == 0U)
		return BB_CAP_REASON_CANDIDATE_UNSUPPORTED;
	if (op == BB_CAP_OP_WIRED_RECOVERY)
		return check_common_wired(facts, candidate);
	if (op == BB_CAP_OP_NAND_READ)
		return check_nand_read(facts, candidate);
	if (op == BB_CAP_OP_NAND_METADATA_SAMPLE) {
		reason = check_nand_read(facts, candidate);
		if (reason != BB_CAP_REASON_NONE)
			return reason;
		if ((candidate->supported_ecc_modes &
		     (BB_CAP_ECC_BIT_ECC4 | BB_CAP_ECC_BIT_ECC8)) !=
		    (BB_CAP_ECC_BIT_ECC4 | BB_CAP_ECC_BIT_ECC8))
			return BB_CAP_REASON_ECC_UNSUPPORTED;
		return BB_CAP_REASON_NONE;
	}
	if (op == BB_CAP_OP_SYSTEM_BOOT)
		return check_system_boot(facts, candidate);
	if (op == BB_CAP_OP_BC_TRANSACTION)
		return check_bc_transaction(facts, candidate);
	if (op == BB_CAP_OP_APPSBL_WRITE)
		return check_appsbl_write(facts, candidate);
	return BB_CAP_REASON_BAD_ARGUMENT;
}

int bb_capability_evaluate(const struct bb_cap_facts *facts,
			   const struct bb_cap_candidate_requirements *candidate,
			   uint32_t requested_operations,
			   struct bb_cap_decision *decision)
{
	uint32_t valid_mask = (1U << BB_CAP_OP_COUNT) - 1U;
	enum bb_cap_reason c_reason;
	unsigned int op;

	if (decision == NULL)
		return -1;
	memset(decision, 0, sizeof(*decision));
	if (facts == NULL || candidate == NULL || requested_operations == 0U ||
	    (requested_operations & ~valid_mask) != 0U) {
		for (op = 0; op < BB_CAP_OP_COUNT; ++op) {
			if (requested_operations & BB_CAP_OP_BIT(op)) {
				decision->denied_operations |= BB_CAP_OP_BIT(op);
				decision->operation[op].reason = BB_CAP_REASON_BAD_ARGUMENT;
			}
		}
		return -1;
	}
	c_reason = candidate_reason(candidate);
	if (c_reason != BB_CAP_REASON_NONE) {
		for (op = 0; op < BB_CAP_OP_COUNT; ++op) {
			if (requested_operations & BB_CAP_OP_BIT(op)) {
				decision->denied_operations |= BB_CAP_OP_BIT(op);
				decision->operation[op].reason = c_reason;
			}
		}
		return 0;
	}
	for (op = 0; op < BB_CAP_OP_COUNT; ++op) {
		enum bb_cap_reason reason;
		uint32_t bit = BB_CAP_OP_BIT(op);
		if ((requested_operations & bit) == 0U)
			continue;
		reason = evaluate_one(facts, candidate, (enum bb_cap_operation)op);
		decision->operation[op].reason = reason;
		if (reason == BB_CAP_REASON_NONE) {
			decision->granted_operations |= bit;
			decision->operation[op].state = BB_CAP_GRANTED;
		} else {
			decision->operation[op].state = BB_CAP_DENIED;
			decision->denied_operations |= bit;
		}
	}
	return 0;
}

const char *bb_capability_reason_name(enum bb_cap_reason reason)
{
	static const char *const names[] = {
		"none", "bad_argument", "bad_candidate", "candidate_scope",
		"candidate_unsupported", "evidence_unknown", "evidence_invalid",
		"evidence_conflict", "evidence_stale", "origin_mismatch",
		"candidate_mismatch", "phy_topology_mismatch", "no_lan_path",
		"mac_unusable", "ecc_unsupported", "layout_unsupported",
		"chain_unproven", "bc_unsafe", "write_window_mismatch",
		"target_mismatch", "active_target_mismatch", "session_mismatch",
		"ecc_coverage_incomplete", "digest_mismatch"
	};
	if ((unsigned int)reason >= (sizeof(names) / sizeof(names[0])))
		return "unknown_reason";
	return names[(unsigned int)reason];
}
