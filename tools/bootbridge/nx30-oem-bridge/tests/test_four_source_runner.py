#!/usr/bin/env python3
import sys
from pathlib import Path
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_four_sources_remote as runner


class FourSourceRunnerTests(unittest.TestCase):
    def test_logical_target_matrix_has_exactly_seventeen_source_bound_rows(self):
        rows = runner.logical_target_matrix()
        self.assertEqual(len(rows), 17)
        self.assertEqual(len(set(rows)), 17)
        self.assertEqual({model: sorted(board for m, board in rows if m == model)
                          for model in {m for m, _ in rows}}, {
            "NX30": ["D"], "RX3000": ["A", "B", "C", "D"],
            "RC3000": ["A", "B", "C", "D"],
            "RT3000": ["A", "B", "C", "D"],
            "RW3000": ["A", "B", "C", "D"],
        })

    def test_result_groups_seventeen_targets_into_five_logical_products(self):
        groups = {model: boards for source in runner.PRODUCTS
                  for model, boards in runner.logical_coverage_groups(source)}
        self.assertEqual(len(groups), 5)
        self.assertEqual(sum(len(boards) for boards in groups.values()), 17)
        self.assertEqual(groups["NX30"], ("D",))
        self.assertEqual(groups["RX3000"], ("A", "B", "C", "D"))

    def test_rx_identity_evidence_copy_is_exactly_pinned(self):
        raw = runner.RX_EVIDENCE.read_bytes()
        self.assertEqual(len(raw), runner.RX_EVIDENCE_BYTES)
        self.assertEqual(runner.hashlib.sha256(raw).hexdigest(), runner.RX_EVIDENCE_SHA256)

    def test_grouped_receipt_binds_audited_output_and_source_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            report_path, audit_path = root / "build.json", root / "audit.json"
            report_path.write_text("{}\n", encoding="utf-8")
            audit_path.write_text('{"status":"PASS"}\n', encoding="utf-8")
            report = {"official": {"model": "NX30", "version": "NX30V100R012",
                                    "sha256": "a" * 64},
                "candidate": {"bytes": 999, "sha256": "b" * 64},
                "dispatcher": {"sha256": "d" * 64},
                "providers": [{"hardware_class": board, "candidate_id": board + "-id"}
                              for board in "ABCD"]}
            row = {"bundle_id": "NX30-aaaaaaaaaaaa",
                   "official_pin": {"sha256": "a" * 64}}
            rx_pin = {"path": str(runner.RX_EVIDENCE.resolve()),
                      "bytes": runner.RX_EVIDENCE_BYTES,
                      "sha256": runner.RX_EVIDENCE_SHA256}
            result = runner.logical_receipt(root, "RX3000", row, report,
                report_path, audit_path, ("A", "B", "C", "D"), rx_pin)
            receipt = json.loads(Path(result["evidence"]["path"]).read_text())
            self.assertEqual(result["providers"], ["A", "B", "C", "D"])
            self.assertEqual(receipt["status"], "PASS_LOGICAL_COVERAGE_PROVENANCE")
            self.assertEqual(receipt["software_contract_status"], "SOFTWARE_CONTRACT_ONLY")
            self.assertEqual(receipt["source_model"], "NX30")
            self.assertEqual(receipt["source_version"], "NX30V100R012")
            self.assertEqual(receipt["candidate_sha256"], "b" * 64)
            self.assertEqual(receipt["providers"], ["A", "B", "C", "D"])
            self.assertEqual(receipt["candidate_ids"], ["A-id", "B-id", "C-id", "D-id"])
            self.assertEqual(receipt["provider_records"], 4)
            self.assertEqual(receipt["build_report_sha256"], runner.report_ref(report_path)["sha256"])
            self.assertEqual(receipt["audit_report_sha256"], runner.report_ref(audit_path)["sha256"])
            self.assertEqual(receipt["dispatcher_sha256"], "d" * 64)
            self.assertEqual(receipt["hardware_validation"], "NOT_EXECUTED")
            self.assertEqual(receipt["physical_install_qualification"], "NOT_GRANTED")
            self.assertEqual(receipt["identity_evidence"], rx_pin)


if __name__ == "__main__":
    unittest.main()
