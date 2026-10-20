import json
from pathlib import Path
import unittest

from tools.test_dispatch_e2e import run_dispatch_gate

PROJECT = Path(__file__).resolve().parents[1]
FIXTURE = PROJECT / "tests/fixtures/dispatch-c-records-v1.json"


class CompleteDispatcherTableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def test_all_four_complete_source_tables_against_production_c(self):
        self.assertEqual(self.fixture["schema"], "dispatcher-c-record-fixtures-v1")
        self.assertEqual(len(self.fixture["tables"]), 4)
        self.assertEqual({row["product"] for row in self.fixture["tables"]},
                         {"RC3000", "RT3000", "RW3000", "NX30"})
        for table in self.fixture["tables"]:
            with self.subTest(product=table["product"]):
                self.assertEqual(table["provider_rows"], 68)
                result = run_dispatch_gate(table["source_sha256"], table["product"],
                                           table["c_records"],table["provider_rows"])
                self.assertEqual(result["status"], "PASS")
                self.assertEqual(result["provider_rows"], 68)
                self.assertEqual(result["tested_candidates"], 68)
                self.assertTrue(result["synthetic_inspector_readiness"])
                self.assertFalse(result["provider_executables_run"])

    def test_partial_table_is_rejected_before_compilation(self):
        table = self.fixture["tables"][0]
        with self.assertRaisesRegex(ValueError, "row count differs"):
            run_dispatch_gate(table["source_sha256"], table["product"],
                              table["c_records"][:-1],table["provider_rows"])


if __name__ == "__main__":
    unittest.main()
