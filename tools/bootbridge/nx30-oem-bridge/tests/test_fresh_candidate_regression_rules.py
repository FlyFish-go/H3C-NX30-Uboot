# SPDX-License-Identifier: GPL-2.0-or-later
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import nand_schema
from test_fresh_candidate_profiles import validate_source_catalog_profile


class FreshCandidateCoverageRulesTests(unittest.TestCase):
    def test_apppsl_qpic_eligible_profiles_need_not_have_linux_mtd_rows(self):
        catalog = nand_schema.profiles()
        for profile_id, ecc in (("h3c-oem-e521-128m-2k-sp128", 4),
                                 ("h3c-oem-c881-128m-2k-sp128", 8)):
            with self.subTest(profile_id=profile_id):
                profile = next(p for p in catalog["profiles"] if p["profile_id"] == profile_id)
                runtime = nand_schema.runtime_profiles(("A0", "A1", "B", "D", "C"), 15, catalog)
                self.assertFalse(any(p["profile_id"] == profile_id for p in runtime))
                validate_source_catalog_profile(profile, ecc, sum(nand_schema.TABLE_MASKS.values()))

    def test_wrong_qpic_capacity_or_unsupported_ecc_rejects(self):
        profile = next(p for p in nand_schema.profiles()["profiles"]
                       if p["profile_id"] == "h3c-oem-e521-128m-2k-sp128")
        with self.assertRaisesRegex(ValueError, r"capacity\+ECC"):
            validate_source_catalog_profile(dict(profile, qpic_capacity_mib=1024), 4,
                                            sum(nand_schema.TABLE_MASKS.values()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
