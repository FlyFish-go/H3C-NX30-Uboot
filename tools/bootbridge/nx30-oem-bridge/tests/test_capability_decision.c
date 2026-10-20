#include "bridge_capability.h"

#include <stdio.h>
#include <string.h>

#define BIT_ORIGIN(origin) (1U << (unsigned int)(origin))
#define ROUTE_BIT(route) (1U << (unsigned int)(route))
#define SWITCH_BIT(sw) (1U << (unsigned int)(sw))

static void set_obs(struct bb_cap_observation *o, uint64_t value,
		   enum bb_cap_origin origin, uint32_t generation)
{
	o->state = BB_CAP_VALID;
	o->origin = origin;
	o->value = value;
	o->coverage_bytes = 4U;
	o->generation = generation;
	o->stable_reads = 2U;
}

static void set_digest(struct bb_cap_digest_observation *o,
		       const uint8_t digest[BB_CAP_SHA256_BYTES],
		       uint32_t generation)
{
	o->state = BB_CAP_VALID;
	o->origin = BB_CAP_ORIGIN_MTD_READ;
	memcpy(o->sha256, digest, BB_CAP_SHA256_BYTES);
	o->offset = 0x10000U;
	o->coverage_bytes = 0x10000U;
	o->generation = generation;
	o->stable_reads = 2U;
}

static void set_range(struct bb_cap_range_observation *o, uint64_t offset,
		      uint64_t size, uint32_t generation)
{
	o->state = BB_CAP_VALID;
	o->origin = BB_CAP_ORIGIN_BOUNDED_LAYOUT;
	o->range.offset = offset;
	o->range.size = size;
	o->generation = generation;
	o->stable_reads = 2U;
}

static void fill_candidate(struct bb_cap_candidate_requirements *c,
			   uint32_t operations)
{
	unsigned int i;
	memset(c, 0, sizeof(*c));
	c->schema = BB_CAP_CURRENT_SCHEMA;
	for (i = 0; i < BB_CAP_SHA256_BYTES; ++i) {
		c->payload_sha256[i] = (uint8_t)(i + 1U);
		c->source_contract_sha256[i] = (uint8_t)(0xa0U + i);
		c->required_sbl_sha256[i] = (uint8_t)(0x50U + i);
		c->expected_appsbl_copy_sha256[0][i] = (uint8_t)(0x70U + i);
		c->expected_appsbl_copy_sha256[1][i] = (uint8_t)(0x90U + i);
	}
	c->supported_operations = operations;
	c->accepted_routes = ROUTE_BIT(BB_CAP_VALUE_ROUTE_MP02_1);
	c->accepted_switches = SWITCH_BIT(BB_CAP_VALUE_SWITCH_QCA8337);
	c->accepted_phy_masks = BB_CAP_PHY_MASK_BIT(0x0eU);
	c->accepted_layouts = (1U << BB_CAP_VALUE_LAYOUT_B_FIXED_SLOTS) |
		(1U << BB_CAP_VALUE_LAYOUT_OEM_MP02_1);
	c->supported_ecc_modes = BB_CAP_ECC_BIT_ECC4 | BB_CAP_ECC_BIT_ECC8;
	c->expected_boot_source = BB_CAP_VALUE_BOOT_NAND;
	c->expected_pbl_ecc = BB_CAP_VALUE_ECC4;
	c->expected_appsbl_ecc = BB_CAP_VALUE_ECC4;
	c->required_sbl_offset = 0x10000U;
	c->required_sbl_bytes = 0x10000U;
	c->appsbl_page_bytes = 2048U;
	c->minimum_dram_bytes = 128U * 1024U * 1024U;
	c->minimum_nand_bytes = 128U * 1024U * 1024U;
	c->mac_policy = BB_CAP_MAC_ALLOW_VOLATILE_LOCAL;
	c->write_target_count = 2U;
	c->appsbl_ecc_step_bytes = BB_CAP_ECC4_STEP_BYTES;
	c->write_targets[0].offset = 0x1000U;
	c->write_targets[0].size = 0x1000U;
	c->write_targets[1].offset = 0x2000U;
	c->write_targets[1].size = 0x1000U;
}

static void fill_wired(struct bb_cap_facts *f, uint32_t generation,
		       uint64_t phy_mask)
{
	memset(f, 0, sizeof(*f));
	f->generation = generation;
	set_obs(&f->soc_class, BB_CAP_VALUE_IPQ50XX, BB_CAP_ORIGIN_SMEM, generation);
	set_obs(&f->route, BB_CAP_VALUE_ROUTE_MP02_1, BB_CAP_ORIGIN_DT, generation);
	set_obs(&f->dram_bytes, 256U * 1024U * 1024U, BB_CAP_ORIGIN_SMEM, generation);
	set_obs(&f->switch_kind, BB_CAP_VALUE_SWITCH_QCA8337,
		BB_CAP_ORIGIN_READONLY_MDIO, generation);
	set_obs(&f->phy_mask, phy_mask, BB_CAP_ORIGIN_WIRED_TOPOLOGY, generation);
	set_obs(&f->lan_link_mask, phy_mask, BB_CAP_ORIGIN_READONLY_MDIO, generation);
	set_obs(&f->mac_policy, BB_CAP_VALUE_MAC_UNICAST,
		BB_CAP_ORIGIN_MAC_VALIDATION, generation);
	set_obs(&f->nand_size_bytes, 128U * 1024U * 1024U, BB_CAP_ORIGIN_QPIC, generation);
	set_obs(&f->nand_read_only, 1U, BB_CAP_ORIGIN_QPIC, generation);
	set_obs(&f->nand_io_ecc, BB_CAP_VALUE_ECC4, BB_CAP_ORIGIN_QPIC, generation);
}

static void fill_chain(struct bb_cap_facts *f,
		       const struct bb_cap_candidate_requirements *c)
{
	set_obs(&f->boot_source, BB_CAP_VALUE_BOOT_NAND, BB_CAP_ORIGIN_MMIO, f->generation);
	set_obs(&f->pbl_ecc, BB_CAP_VALUE_ECC4, BB_CAP_ORIGIN_MMIO, f->generation);
	set_obs(&f->appsbl_storage_ecc, BB_CAP_VALUE_ECC4, BB_CAP_ORIGIN_MTD_READ, f->generation);
	set_obs(&f->target_appsbl_storage_ecc, BB_CAP_VALUE_ECC4,
		BB_CAP_ORIGIN_MTD_READ, f->generation);
	set_digest(&f->sbl_digest, c->required_sbl_sha256, f->generation);
	set_obs(&f->layout, BB_CAP_VALUE_LAYOUT_OEM_MP02_1,
		BB_CAP_ORIGIN_BOUNDED_LAYOUT, f->generation);
	set_obs(&f->bootconfig_ecc, BB_CAP_VALUE_ECC4, BB_CAP_ORIGIN_MTD_READ, f->generation);
	set_obs(&f->bootconfig_pair, BB_CAP_VALUE_BC_TWO_COPIES_MATCH,
		BB_CAP_ORIGIN_MTD_READ, f->generation);
	set_obs(&f->nand_ecc_sample, BB_CAP_VALUE_ECC4, BB_CAP_ORIGIN_MTD_READ, f->generation);
	set_obs(&f->linux_handoff_route, BB_CAP_VALUE_ROUTE_MP02_1,
		BB_CAP_ORIGIN_DT, f->generation);
	set_obs(&f->linux_handoff_ecc, BB_CAP_VALUE_ECC4, BB_CAP_ORIGIN_DT, f->generation);
	set_obs(&f->write_window, BB_CAP_VALUE_WINDOW_BC_ONLY,
		BB_CAP_ORIGIN_QPIC, f->generation);
	set_obs(&f->write_target_count, 2U, BB_CAP_ORIGIN_BOUNDED_LAYOUT, f->generation);
	set_range(&f->write_target[0], 0x1000U, 0x1000U, f->generation);
	set_range(&f->write_target[1], 0x2000U, 0x1000U, f->generation);
}

static void fill_appsbl_scan(struct bb_cap_appsbl_copy_scan *s,
			     const struct bb_cap_candidate_requirements *c,
			     const struct bb_cap_evidence_envelope *e,
			     uint32_t index, uint32_t generation)
{
	uint32_t page;
	memset(s, 0, sizeof(*s));
	s->state = BB_CAP_VALID;
	s->origin = BB_CAP_ORIGIN_MTD_READ;
	s->offset = c->write_targets[index].offset;
	s->size = c->write_targets[index].size;
	memcpy(s->boot_id_sha256, e->boot_id_sha256, BB_CAP_SHA256_BYTES);
	memcpy(s->session_id, e->session_id, BB_CAP_SESSION_ID_BYTES);
	s->generation = generation;
	s->ecc_mode = BB_CAP_VALUE_ECC4;
	s->page_bytes = c->appsbl_page_bytes;
	s->ecc_step_bytes = c->appsbl_ecc_step_bytes;
	s->total_pages = (uint32_t)(s->size / s->page_bytes);
	s->covered_pages = s->total_pages;
	s->read_ok_pages = s->total_pages;
	s->counter_pages = s->total_pages;
	s->read_page_samples = s->total_pages;
	s->counter_page_samples = s->total_pages;
	for (page = 0U; page < s->total_pages; ++page) {
		s->read_page_bitmap[page / 8U] |= (uint8_t)(1U << (page % 8U));
		s->counter_page_bitmap[page / 8U] |= (uint8_t)(1U << (page % 8U));
	}
	memcpy(s->decoded_sha256, c->expected_appsbl_copy_sha256[index],
	       BB_CAP_SHA256_BYTES);
}

static void fill_appsbl_evidence(struct bb_cap_facts *f,
				 const struct bb_cap_candidate_requirements *c)
{
	struct bb_cap_evidence_envelope *e = &f->evidence;
	unsigned int i;
	memset(e, 0, sizeof(*e));
	e->state = BB_CAP_VALID;
	e->origin = BB_CAP_ORIGIN_SESSION_PROVIDER;
	for (i = 0; i < BB_CAP_SHA256_BYTES; ++i)
		e->boot_id_sha256[i] = (uint8_t)(0x20U + i);
	for (i = 0; i < BB_CAP_SESSION_ID_BYTES; ++i)
		e->session_id[i] = (uint8_t)(0xc0U + i);
	memcpy(f->current_boot_id_sha256, e->boot_id_sha256, BB_CAP_SHA256_BYTES);
	memcpy(f->current_session_id, e->session_id, BB_CAP_SESSION_ID_BYTES);
	memcpy(e->candidate_sha256, c->payload_sha256, BB_CAP_SHA256_BYTES);
	memcpy(e->source_contract_sha256, c->source_contract_sha256,
	       BB_CAP_SHA256_BYTES);
	e->generation = f->generation;
	fill_appsbl_scan(&f->appsbl_copy[0], c, e, 0U, f->generation);
	fill_appsbl_scan(&f->appsbl_copy[1], c, e, 1U, f->generation);
}

static void fill_valid_appsbl_write_fixture(
	struct bb_cap_facts *f, struct bb_cap_candidate_requirements *c,
	uint32_t generation)
{
	uint32_t op = BB_CAP_OP_BIT(BB_CAP_OP_APPSBL_WRITE);
	fill_candidate(c, op);
	c->expected_write_window = BB_CAP_VALUE_WINDOW_APPSBL_EXACT;
	c->write_targets[0].offset = 0x500000U;
	c->write_targets[0].size = 0x140000U;
	c->write_targets[1].offset = 0x640000U;
	c->write_targets[1].size = 0x140000U;
	fill_wired(f, generation, 0x0fU);
	fill_chain(f, c);
	set_obs(&f->write_window, BB_CAP_VALUE_WINDOW_APPSBL_EXACT,
		BB_CAP_ORIGIN_QPIC, generation);
	set_obs(&f->write_target_count, 1U, BB_CAP_ORIGIN_BOUNDED_LAYOUT, generation);
	set_range(&f->write_target[0], c->write_targets[1].offset,
		  c->write_targets[1].size, generation);
	set_obs(&f->active_appsbl_index, 0U, BB_CAP_ORIGIN_BOUNDED_LAYOUT, generation);
	set_obs(&f->target_appsbl_index, 1U, BB_CAP_ORIGIN_BOUNDED_LAYOUT, generation);
	fill_appsbl_evidence(f, c);
}

static int expect_decision(const struct bb_cap_facts *f,
			   const struct bb_cap_candidate_requirements *c,
			   uint32_t op, int expected_grant,
			   enum bb_cap_reason expected_reason)
{
	struct bb_cap_decision d;
	if (bb_capability_evaluate(f, c, op, &d) != 0) {
		fprintf(stderr, "evaluate failed for op=%u\n", op);
		return 1;
	}
	if (!!(d.granted_operations & op) != !!expected_grant ||
	    d.operation[__builtin_ctz(op)].reason != expected_reason) {
		fprintf(stderr, "op=%u granted=%u reason=%s expected_grant=%d expected_reason=%s\n",
			op, !!(d.granted_operations & op),
			bb_capability_reason_name(d.operation[__builtin_ctz(op)].reason),
			expected_grant, bb_capability_reason_name(expected_reason));
		return 1;
	}
	return 0;
}

static int test_b_d_wired_selection(void)
{
	struct bb_cap_facts f;
	struct bb_cap_candidate_requirements b, d, multi;
	uint32_t op = BB_CAP_OP_BIT(BB_CAP_OP_WIRED_RECOVERY);
	fill_wired(&f, 7U, 0x0eU); /* The observed wiring set matches B's audited DT. */
	fill_candidate(&b, op);
	if (expect_decision(&f, &b, op, 1, BB_CAP_REASON_NONE)) return 1;

	/* A D-only payload expects PHY 0..3 and cannot be flashed across the B
	 * 1..3 topology just because both are called B_OR_D or share machid/NAND. */
	fill_candidate(&d, op);
	d.accepted_phy_masks = BB_CAP_PHY_MASK_BIT(0x0fU);
	if (expect_decision(&f, &d, op, 0, BB_CAP_REASON_PHY_TOPOLOGY_MISMATCH)) return 1;

	fill_wired(&f, 8U, 0x0fU); /* D-shaped wiring evidence, not product identity. */
	if (expect_decision(&f, &d, op, 1, BB_CAP_REASON_NONE)) return 1;

	/* A true multi-payload must explicitly carry both code paths. */
	fill_candidate(&multi, op);
	multi.accepted_phy_masks = BB_CAP_PHY_MASK_BIT(0x0eU) |
		BB_CAP_PHY_MASK_BIT(0x0fU);
	if (expect_decision(&f, &multi, op, 1, BB_CAP_REASON_NONE)) return 1;
	fill_wired(&f, 9U, 0x0eU);
	if (expect_decision(&f, &multi, op, 1, BB_CAP_REASON_NONE)) return 1;
	return 0;
}

static int test_unknown_conflict_and_link_fail_closed(void)
{
	struct bb_cap_facts f;
	struct bb_cap_candidate_requirements c;
	uint32_t op = BB_CAP_OP_BIT(BB_CAP_OP_WIRED_RECOVERY);
	fill_candidate(&c, op);
	fill_wired(&f, 2U, 0x0eU);
	f.phy_mask.state = BB_CAP_UNKNOWN;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_UNKNOWN)) return 1;
	fill_wired(&f, 2U, 0x0eU);
	f.switch_kind.state = BB_CAP_CONFLICT;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_CONFLICT)) return 1;
	fill_wired(&f, 2U, 0x0eU);
	f.lan_link_mask.value = 0U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_NO_LAN_PATH)) return 1;
	fill_wired(&f, 2U, 0x0eU);
	f.lan_link_mask.value = 0x01U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_NO_LAN_PATH)) return 1;
	fill_wired(&f, 2U, 0x0eU);
	f.dram_bytes.origin = BB_CAP_ORIGIN_DT; /* Fallback declaration is not a measured table. */
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ORIGIN_MISMATCH)) return 1;
	fill_wired(&f, 2U, 0x0eU);
	f.mac_policy.value = BB_CAP_VALUE_MAC_VOLATILE_LOCAL;
	c.mac_policy = BB_CAP_MAC_REQUIRE_UNICAST;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_MAC_UNUSABLE)) return 1;

	/* C's captured RTL8367S probe uses a register-write window. A legacy
	 * mutating MDIO origin cannot satisfy the read-only provider contract. */
	fill_wired(&f, 3U, 0x0eU);
	fill_candidate(&c, op);
	c.accepted_switches = SWITCH_BIT(BB_CAP_VALUE_SWITCH_RTL8367S);
	f.switch_kind.value = BB_CAP_VALUE_SWITCH_RTL8367S;
	f.switch_kind.origin = BB_CAP_ORIGIN_MDIO;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ORIGIN_MISMATCH)) return 1;
	f.switch_kind.state = BB_CAP_UNKNOWN;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_UNKNOWN)) return 1;
	return 0;
}

static int test_ecc_read_grants_are_separate_from_boot_and_write(void)
{
	struct bb_cap_facts f;
	struct bb_cap_candidate_requirements c;
	struct bb_cap_decision d;
	uint32_t read = BB_CAP_OP_BIT(BB_CAP_OP_NAND_READ);
	uint32_t sample = BB_CAP_OP_BIT(BB_CAP_OP_NAND_METADATA_SAMPLE);
	uint32_t boot = BB_CAP_OP_BIT(BB_CAP_OP_SYSTEM_BOOT);
	uint32_t appsbl = BB_CAP_OP_BIT(BB_CAP_OP_APPSBL_WRITE);
	fill_candidate(&c, read | sample | boot | appsbl);
	c.supported_ecc_modes = BB_CAP_ECC_BIT_ECC4; /* D-like read candidate. */
	fill_wired(&f, 11U, 0x0fU);
	f.nand_io_ecc.value = BB_CAP_VALUE_ECC4;
	if (expect_decision(&f, &c, read, 1, BB_CAP_REASON_NONE)) return 1;
	if (expect_decision(&f, &c, sample, 0, BB_CAP_REASON_ECC_UNSUPPORTED)) return 1;
	f.nand_io_ecc.value = BB_CAP_VALUE_ECC8;
	if (expect_decision(&f, &c, read, 0, BB_CAP_REASON_ECC_UNSUPPORTED)) return 1;

	/* A working ECC4 NAND read does not make PBL/APPSBL encoding known. */
	f.nand_io_ecc.value = BB_CAP_VALUE_ECC4;
	if (expect_decision(&f, &c, boot, 0, BB_CAP_REASON_EVIDENCE_UNKNOWN)) return 1;
	if (expect_decision(&f, &c, appsbl, 0, BB_CAP_REASON_EVIDENCE_UNKNOWN)) return 1;

	/* A candidate without write operations cannot acquire them from observations. */
	c.supported_operations = read;
	if (bb_capability_evaluate(&f, &c, appsbl, &d) != 0 ||
	    d.granted_operations != 0U ||
	    d.operation[BB_CAP_OP_APPSBL_WRITE].reason != BB_CAP_REASON_CANDIDATE_UNSUPPORTED)
		return 1;
	return 0;
}

static int test_complete_chain_and_exact_window(void)
{
	struct bb_cap_facts f;
	struct bb_cap_candidate_requirements c;
	uint32_t boot = BB_CAP_OP_BIT(BB_CAP_OP_SYSTEM_BOOT);
	uint32_t bc = BB_CAP_OP_BIT(BB_CAP_OP_BC_TRANSACTION);
	uint32_t appsbl = BB_CAP_OP_BIT(BB_CAP_OP_APPSBL_WRITE);
	fill_candidate(&c, boot | bc | appsbl | BB_CAP_OP_BIT(BB_CAP_OP_NAND_READ));
	c.expected_write_window = BB_CAP_VALUE_WINDOW_BC_ONLY;
	fill_wired(&f, 14U, 0x0eU);
	fill_chain(&f, &c);
	if (expect_decision(&f, &c, boot, 1, BB_CAP_REASON_NONE)) return 1;
	if (expect_decision(&f, &c, bc, 1, BB_CAP_REASON_NONE)) return 1;
	/* APPSBL policy is reached only after a pinned same-session full-copy pass. */
	c.expected_write_window = BB_CAP_VALUE_WINDOW_APPSBL_EXACT;
	set_obs(&f.write_window, BB_CAP_VALUE_WINDOW_APPSBL_EXACT,
		BB_CAP_ORIGIN_QPIC, f.generation);
	f.write_target_count.value = 1U;
	set_obs(&f.active_appsbl_index, 0U, BB_CAP_ORIGIN_BOUNDED_LAYOUT, f.generation);
	set_obs(&f.target_appsbl_index, 1U, BB_CAP_ORIGIN_BOUNDED_LAYOUT, f.generation);
	c.write_targets[0].offset = 0x500000U;
	c.write_targets[0].size = 0x140000U;
	c.write_targets[1].offset = 0x640000U;
	c.write_targets[1].size = 0x140000U;
	set_range(&f.write_target[0], c.write_targets[1].offset,
		  c.write_targets[1].size, f.generation);
	fill_appsbl_evidence(&f, &c);
	/* An exact ECC4 scan still cannot pass through the wrong write window. */
	set_obs(&f.write_window, BB_CAP_VALUE_WINDOW_BC_ONLY,
		BB_CAP_ORIGIN_QPIC, f.generation);
	if (expect_decision(&f, &c, appsbl, 0, BB_CAP_REASON_WRITE_WINDOW_MISMATCH)) return 1;
	set_obs(&f.write_window, BB_CAP_VALUE_WINDOW_APPSBL_EXACT,
		BB_CAP_ORIGIN_QPIC, f.generation);
	if (expect_decision(&f, &c, appsbl, 1, BB_CAP_REASON_NONE)) return 1;
	f.write_window.state = BB_CAP_CONFLICT;
	if (expect_decision(&f, &c, appsbl, 0, BB_CAP_REASON_EVIDENCE_CONFLICT)) return 1;
	set_obs(&f.write_window, BB_CAP_VALUE_WINDOW_APPSBL_EXACT,
		BB_CAP_ORIGIN_QPIC, f.generation);
	f.write_target[0].range.offset++;
	if (expect_decision(&f, &c, appsbl, 0, BB_CAP_REASON_TARGET_MISMATCH)) return 1;
	set_range(&f.write_target[0], c.write_targets[1].offset,
		  c.write_targets[1].size, f.generation);
	
	/* Stale evidence, changed slot records and Linux ECC disagreement all close. */
	f.appsbl_storage_ecc.generation = f.generation - 1U;
	if (expect_decision(&f, &c, appsbl, 0, BB_CAP_REASON_EVIDENCE_STALE)) return 1;
	f.appsbl_storage_ecc.generation = f.generation;
	f.target_appsbl_storage_ecc.state = BB_CAP_UNKNOWN;
	if (expect_decision(&f, &c, appsbl, 0, BB_CAP_REASON_EVIDENCE_UNKNOWN)) return 1;
	set_obs(&f.target_appsbl_storage_ecc, BB_CAP_VALUE_ECC4,
		BB_CAP_ORIGIN_MTD_READ, f.generation);
	f.bootconfig_pair.value = BB_CAP_VALUE_BC_CONFLICT;
	if (expect_decision(&f, &c, bc, 0, BB_CAP_REASON_BC_UNSAFE)) return 1;
	f.bootconfig_pair.value = BB_CAP_VALUE_BC_TWO_COPIES_MATCH;
	f.linux_handoff_ecc.value = BB_CAP_VALUE_ECC8;
	if (expect_decision(&f, &c, boot, 0, BB_CAP_REASON_ECC_UNSUPPORTED)) return 1;
	f.linux_handoff_ecc.value = BB_CAP_VALUE_ECC4;
	set_obs(&f.active_appsbl_index, 1U, BB_CAP_ORIGIN_BOUNDED_LAYOUT, f.generation);
	if (expect_decision(&f, &c, appsbl, 0, BB_CAP_REASON_ACTIVE_TARGET_MISMATCH)) return 1;
	return 0;
}

static int test_appsbl_evidence_is_session_bound_and_complete(void)
{
	struct bb_cap_facts f;
	struct bb_cap_candidate_requirements c;
	uint32_t op = BB_CAP_OP_BIT(BB_CAP_OP_APPSBL_WRITE);

	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	if (expect_decision(&f, &c, op, 1, BB_CAP_REASON_NONE)) return 1;
	/* Counted correctable reads are visible and may pass; an inconsistent
	 * aggregate cannot be silently normalized into a clean read. */
	f.appsbl_copy[0].corrected_pages = 2U;
	f.appsbl_copy[0].corrected_bitflips = 3U;
	f.appsbl_copy[0].max_bitflips = 2U;
	if (expect_decision(&f, &c, op, 1, BB_CAP_REASON_NONE)) return 1;
	f.appsbl_copy[0].corrected_pages = 0U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_INVALID)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].corrected_pages = 2U;
	f.appsbl_copy[0].corrected_bitflips = 1U;
	f.appsbl_copy[0].max_bitflips = 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_INVALID)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].corrected_pages = 2U;
	f.appsbl_copy[0].corrected_bitflips = 2U;
	f.appsbl_copy[0].max_bitflips = 2U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_INVALID)) return 1;
	/* ECC max is per 512-byte codeword, while the correction aggregate spans
	 * four codewords per 2 KiB page: this total is feasible and must pass. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].corrected_pages = 2U;
	f.appsbl_copy[0].corrected_bitflips = 5U;
	f.appsbl_copy[0].max_bitflips = 2U;
	if (expect_decision(&f, &c, op, 1, BB_CAP_REASON_NONE)) return 1;
	/* Two corrected pages each contain four codewords: 17 exceeds the physical
	 * maximum 2 pages * 4 codewords/page * 2 bits/codeword. */
	f.appsbl_copy[0].corrected_bitflips = 17U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_INVALID)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].corrected_pages = 1U;
	f.appsbl_copy[0].corrected_bitflips = 5U;
	f.appsbl_copy[0].max_bitflips = BB_CAP_ECC4_MAX_BITFLIPS + 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_INVALID)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	c.appsbl_ecc_step_bytes = 1024U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_BAD_CANDIDATE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].ecc_step_bytes = 1024U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_UNSUPPORTED)) return 1;

	/* A stale envelope is checked against the independently supplied current
	 * invocation binding before either copy can be accepted. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.evidence.session_id[0] ^= 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_SESSION_MISMATCH)) return 1;

	/* A scan replayed from a different provider session cannot be mixed in. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[1].session_id[0] ^= 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_SESSION_MISMATCH)) return 1;

	/* An older generation in the same nominal session is stale evidence. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[1].generation--;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_STALE)) return 1;

	/* Candidate and source-contract pins are independently checked. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.evidence.candidate_sha256[3] ^= 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_CANDIDATE_MISMATCH)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.evidence.source_contract_sha256[7] ^= 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_CANDIDATE_MISMATCH)) return 1;

	/* A complete range with one skipped/unobserved page is not full coverage. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].covered_pages--;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;

	/* Every page needs a paired ECC counter sample; read/uncorrectable/bad-block
	 * outcomes also close the grant even if the top-level byte count looks full. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].counter_pages--;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].read_page_bitmap[12U / 8U] &= (uint8_t)~(1U << (12U % 8U));
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].counter_page_bitmap[510U / 8U] &=
		(uint8_t)~(1U << (510U % 8U));
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].duplicate_read_pages = 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].duplicate_counter_pages = 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].out_of_range_samples = 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;

	/* Candidate-pinned ranges shorter than the bitmap bound must have clear
	 * tail bits; ranges longer than the explicit bound are rejected. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	c.write_targets[0].size = 639U * c.appsbl_page_bytes;
	f.appsbl_copy[0].size = c.write_targets[0].size;
	f.appsbl_copy[0].total_pages = 639U;
	f.appsbl_copy[0].covered_pages = 639U;
	f.appsbl_copy[0].read_ok_pages = 639U;
	f.appsbl_copy[0].counter_pages = 639U;
	f.appsbl_copy[0].read_page_samples = 639U;
	f.appsbl_copy[0].counter_page_samples = 639U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	c.write_targets[0].size = (BB_CAP_MAX_APPSBL_PAGES + 1U) *
		c.appsbl_page_bytes;
	f.appsbl_copy[0].size = c.write_targets[0].size;
	f.appsbl_copy[0].total_pages = BB_CAP_MAX_APPSBL_PAGES + 1U;
	f.appsbl_copy[0].covered_pages = BB_CAP_MAX_APPSBL_PAGES + 1U;
	f.appsbl_copy[0].read_ok_pages = BB_CAP_MAX_APPSBL_PAGES + 1U;
	f.appsbl_copy[0].counter_pages = BB_CAP_MAX_APPSBL_PAGES + 1U;
	f.appsbl_copy[0].read_page_samples = BB_CAP_MAX_APPSBL_PAGES + 1U;
	f.appsbl_copy[0].counter_page_samples = BB_CAP_MAX_APPSBL_PAGES + 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].read_failed_pages = 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[1].uncorrectable_pages = 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[1].bad_blocks = 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_COVERAGE_INCOMPLETE)) return 1;

	/* ECC8 is explicitly outside this reader; a non-matching full hash is also
	 * rejected instead of being treated as a successful source observation. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].ecc_mode = BB_CAP_VALUE_ECC8;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_ECC_UNSUPPORTED)) return 1;
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[0].decoded_sha256[0] ^= 1U;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_DIGEST_MISMATCH)) return 1;

	/* Unknown evidence is not upgraded by the rest of the valid chain. */
	fill_valid_appsbl_write_fixture(&f, &c, 21U);
	f.appsbl_copy[1].state = BB_CAP_UNKNOWN;
	if (expect_decision(&f, &c, op, 0, BB_CAP_REASON_EVIDENCE_UNKNOWN)) return 1;
	return 0;
}

static int test_missing_candidate_pin_is_denied(void)
{
	struct bb_cap_facts f;
	struct bb_cap_candidate_requirements c;
	uint32_t op = BB_CAP_OP_BIT(BB_CAP_OP_WIRED_RECOVERY);
	fill_wired(&f, 1U, 0x0eU);
	fill_candidate(&c, op);
	memset(c.source_contract_sha256, 0, sizeof(c.source_contract_sha256));
	return expect_decision(&f, &c, op, 0, BB_CAP_REASON_BAD_CANDIDATE);
}

int main(void)
{
	if (test_b_d_wired_selection() ||
	    test_unknown_conflict_and_link_fail_closed() ||
	    test_ecc_read_grants_are_separate_from_boot_and_write() ||
	    test_complete_chain_and_exact_window() ||
	    test_appsbl_evidence_is_session_bound_and_complete() ||
	    test_missing_candidate_pin_is_denied())
		return 1;
	puts("capability-decision: PASS");
	return 0;
}
