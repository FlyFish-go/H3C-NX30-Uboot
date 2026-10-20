#!/usr/bin/env python3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import nand_schema
import build
import candidate_manifest


def test_catalog_exact_profiles_and_model_names():
    doc = nand_schema.profiles()
    assert doc["profile_count"] == 20
    ids = {row["profile_id"] for row in doc["profiles"]}
    assert len(ids) == 20
    efba = next(row for row in doc["profiles"] if row["manufacturer_id"] == 0xef and row["chip_id"] == 0xba)
    assert efba["oem_model"] == "W25N01GWZEIG"
    assert "W25N01GW" not in efba["model_aliases"]
    assert efba["capacity_mib"] == 128 and efba["raw_spare_bytes"] == 64
    assert efba["page_bytes"] == 2048 and efba["erase_bytes"] == 131072
    assert all(row["model_aliases"] == [row["oem_model"]] for row in doc["profiles"])
    same_name = [row for row in doc["profiles"] if row["qpic_model"] == row["oem_model"]]
    distinct_name = [row for row in doc["profiles"] if row["qpic_model"] != row["oem_model"]]
    assert same_name and distinct_name
    assert all(row["qpic_model"] not in row["model_aliases"] for row in distinct_name)


def test_oem_source_table_mask_is_not_board_mask():
    doc = nand_schema.profiles()
    assert set(nand_schema.TABLE_MASKS) == {"A0", "A1", "B", "C", "D"}
    assert nand_schema.TABLE_MASKS["C"] == 0x10
    row = next(r for r in doc["profiles"] if r["profile_id"] == "h3c-oem-efba-128m-2k-sp64")
    assert row["source_table_mask"] == 0x1f
    assert "catalogue_eligible" not in row


def test_oem_window_capacity_stays_distinct_from_qpic_die_capacity():
    doc = nand_schema.profiles()
    c822 = next(row for row in doc["profiles"] if row["profile_id"] == "h3c-oem-c822-256m-2k-sp64")
    assert c822["capacity_mib"] == 256
    assert c822["qpic_capacity_mib"] == 256
    c845_45 = next(row for row in doc["profiles"] if row["chip_id"] == 0x45)
    assert c845_45["capacity_mib"] == 128 and c845_45["qpic_capacity_mib"] == 512


def test_profile_set_digest_is_source_and_catalog_bound():
    doc = nand_schema.profiles()
    source = "a" * 64
    set_id, ids = nand_schema.profile_set_id(source, ["A0", "D"], doc)
    assert set_id.startswith("nandset-" + source[:12] + "-")
    assert "h3c-oem-efba-128m-2k-sp64" in ids
    assert nand_schema.profile_set_id(source, ["A0", "D"], doc) == (set_id, ids)


def test_runtime_tuple_eligibility_intersects_source_ecc_and_oob_masks():
    rows = nand_schema.runtime_profiles(["D"], candidate_board_mask=8)
    efba = [r for r in rows if r["profile_id"] == "h3c-oem-efba-128m-2k-sp64"]
    assert len(efba) == 1
    assert efba[0]["ecc_bits"] == 4 and efba[0]["mtd_oob_bytes"] == 64
    assert efba[0]["catalogue_eligible"] == 8
    assert not nand_schema.runtime_profiles(["D"], candidate_board_mask=0)[0]["catalogue_eligible"]


def test_generated_abi_v2_provider_header():
    digest = "a" * 64
    profile = nand_schema.runtime_profiles(["A0", "C"], candidate_board_mask=1)[0]
    row = {"record_id": "hw-a-test-variant", "candidate_id": "b" * 40,
        "hardware_class": "A", "source_sha256": digest, "product": "RC3000",
        "route": "config@mp03.1", "route_compatible": "h3c,rc3000",
        "source_revision_sha256": digest, "stock_kernel_sha256": digest,
        "bridge_kernel_sha256": digest, "nand_dt_sha256": digest,
        "profile_set_id": "nandset-aaaaaaaaaaaa-0123456789abcdef",
        "candidate_set_id": "candset-0123456789abcdef", "nand_profiles": [profile],
        "provider_path": "/usr/sbin/bridge-candidates/c-0123456789abcdef0123/provider",
        "provider_sha256": digest, "provider_bytes": 1234,
        "payload_path": "/etc/bridge/providers/c-0123456789abcdef0123/uboot.bin",
        "payload_sha256": digest, "payload_bytes": 0x140000}
    header = build.generated_provider_bundle_header(digest, "RC3000", [row])
    assert "BI_PROVIDER_BUNDLE_ABI_VERSION 2u" in header
    assert "bdi_nand_profile" in header and "catalogue_eligible" not in header
    assert "candset-0123456789abcdef" in header


def test_candidate_set_id_binds_variant_multiset_and_source_archive():
    row = {"candidate_id": "id-b", "board": "B", "nand_profile_id": "p1",
        "load_ecc_bits": 8, "storage_ecc_bits": 8, "partition_sha256": "1" * 64,
        "mbn_sha256": "2" * 64, "load_sha256": "3" * 64, "partition_bytes": 0x140000}
    source, archive = "a" * 64, "b" * 64
    original = nand_schema.candidate_set_id(source, archive, [row])
    changed = dict(row, nand_profile_id="p2")
    assert original.startswith("candset-")
    assert nand_schema.candidate_set_id(source, archive, [row]) == original
    assert nand_schema.candidate_set_id(source, archive, [changed]) != original
    assert nand_schema.candidate_set_id(source, "c" * 64, [row]) != original


def test_fresh_archive_reader_requires_three_caller_pins():
    try:
        candidate_manifest.read_candidate_archive_v2(Path("/nonexistent"), "0" * 64, "1" * 64, "2" * 64)
    except candidate_manifest.CandidateError as exc:
        assert "cannot inspect" in str(exc)
    else:
        raise AssertionError("fresh archive path unexpectedly accepted")
    try:
        candidate_manifest.read_candidate_archive_v2(Path("/nonexistent"), "0" * 64, "1" * 63, "2" * 64)
    except candidate_manifest.CandidateError as exc:
        assert "explicit" in str(exc)
    else:
        raise AssertionError("invalid fresh archive pin unexpectedly accepted")


if __name__ == "__main__":
    test_catalog_exact_profiles_and_model_names()
    test_oem_source_table_mask_is_not_board_mask()
    test_profile_set_digest_is_source_and_catalog_bound()
    test_runtime_tuple_eligibility_intersects_source_ecc_and_oob_masks()
    test_generated_abi_v2_provider_header()
    test_candidate_set_id_binds_variant_multiset_and_source_archive()
    test_fresh_archive_reader_requires_three_caller_pins()
    print("nand_schema: PASS")
