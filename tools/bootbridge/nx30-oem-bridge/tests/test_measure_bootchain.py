import json
import tempfile
import unittest
from pathlib import Path

from importlib.util import module_from_spec, spec_from_file_location


ROOT = Path(__file__).resolve().parents[3]
TOOL = Path(__file__).resolve().parents[1] / "tools" / "measure_bootchain.py"
spec = spec_from_file_location("measure_bootchain", TOOL)
measure = module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(measure)


class MeasureBootchainTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, text):
        path = self.root / name
        path.write_text(text, encoding="ascii")
        return path

    def test_current_capture_keeps_unknown_pbl_and_nand_page_format(self):
        flash = self.write(
            "flash.raw",
            "md.l 8c03000 10\n"
            "08c03000: 0000000b 07980000 00000004 00000002\n"
            "08c03010: 00000800 0000000b 00000000 00000000\n"
            "08c03020: 00000000 00000000 00000000 00000000\n"
            "08c03030: 00000000 00000000 00000000 00000000\n",
        )
        parsed = measure.parse_memory_dump(flash)
        self.assertEqual(parsed["word_count"], 16)
        self.assertEqual(parsed["raw_words"][4]["value"], "0x00000800")
        self.assertEqual(parsed["base_address"], "0x08c03000")

        boot = self.write("bootargs.raw", "H3C# printenv bootargs\nbootargs=console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4\n")
        nand = self.write("nand.raw", "Device 0: nand0, sector size 128 KiB\nPage size 2048 b\nOOB size 64 b\nErase size 131072 b\n")
        history = {"historical_bridge4_cold_boot": "PASS", "transfer_to_current_candidate": False}
        report = measure.build_report(parsed, measure.parse_bootargs(boot), measure.parse_nand_info(nand), history)
        self.assertEqual(report["installation_qualification"], "UNAVAILABLE")
        self.assertEqual(report["current_capture"]["earlyloader_pointer_chain"]["status"], "UNKNOWN")
        self.assertEqual(report["current_capture"]["earlyloader_pointer_chain"]["interpreted_pbl_parameter"], "UNKNOWN")
        self.assertEqual(report["current_capture"]["appsbl_storage"]["low_copy_ecc"], "UNKNOWN")
        self.assertEqual(report["current_capture"]["nand_geometry"]["stored_page_ecc_format"], "UNKNOWN")
        self.assertEqual(report["current_capture"]["bootargs"]["nand_handoff_parameters"], "UNKNOWN")

    def test_discontinuous_or_short_memory_capture_rejects(self):
        path = self.write(
            "bad.raw",
            "md.l 8c03000 10\n"
            "08c03000: 0000000b 07980000 00000004 00000002\n"
            "08c03020: 00000000 00000000 00000000 00000000\n",
        )
        with self.assertRaises(measure.EvidenceError):
            measure.parse_memory_dump(path)

    def test_duplicate_or_incomplete_ecc_bootargs_do_not_choose_mode(self):
        path = self.write("bootargs.raw", "bootargs=nandspare=128 nandeccbits=4 nandspare=64\n")
        result = measure.parse_bootargs(path)
        self.assertTrue(result["duplicate_relevant_keys"])
        self.assertEqual(result["nand_handoff_parameters"], "UNKNOWN")

    def test_verified_inline_substructure_pointer_uses_effective_plus_0x20_selector(self):
        shared = self.write(
            "shared.raw",
            "md.l 08c08420 c\n"
            "08c08420: 00000000 00000000 00000000 00000000\n"
            "08c08430: 00000000 00000000 00000000 00000000\n"
            "08c08440: 00000000 00000000 00000000 08c03000\n",
        )
        info = self.write(
            "flash-info.raw",
            "md.l 08c03000 9\n"
            "08c03000: 0000000b 07980000 00000004 00000002\n"
            "08c03010: 00000800 0000000b 00000000 00000000\n"
            "08c03020: 00000000\n",
        )
        result = measure.resolve_r012_sbl1_chain(
            measure.parse_memory_dump(shared), measure.parse_memory_dump(info)
        )
        self.assertEqual(result["selected_ecc"], 4)
        self.assertEqual(result["status"], "SOURCE_PATH_VALUES_MATCH")
        self.assertEqual(result["nand_parameter_base"], "0x08c03010")
        self.assertEqual(result["mode_field_address"], "0x08c03020")
        self.assertEqual(result["nand_parameter_base_word"], "0x00000800")
        self.assertIn("do not prove SRAM producer", result["qualification"])

    def test_plus_0x10_value_0x800_is_not_selector_plus_0x20_can_select_ecc8(self):
        shared = self.write(
            "shared.raw",
            "md.l 08c08420 c\n"
            "08c08420: 00000000 00000000 00000000 00000000\n"
            "08c08430: 00000000 00000000 00000000 00000000\n"
            "08c08440: 00000000 00000000 00000000 08c03000\n",
        )
        info = self.write(
            "flash-info.raw",
            "md.l 08c03000 9\n"
            "08c03000: 0000000b 07980000 00000004 00000002\n"
            "08c03010: 00000800 0000000b 00000000 00000000\n"
            "08c03020: 00000002\n",
        )
        result = measure.resolve_r012_sbl1_chain(
            measure.parse_memory_dump(shared), measure.parse_memory_dump(info)
        )
        self.assertEqual(result["selected_ecc"], 8)
        self.assertEqual(result["status"], "SOURCE_PATH_VALUES_MATCH")
        self.assertEqual(result["parameter"], "0x00000002")

    def test_repeated_chain_reports_stability_without_freshness(self):
        shared_text = (
            "md.l 08c08420 c\n"
            "08c08420: 00000000 00000000 00000000 00000000\n"
            "08c08430: 00000000 00000000 00000000 00000000\n"
            "08c08440: 00000000 00000000 00000000 08c03000\n"
        )
        flash_text = (
            "md.l 08c03000 9\n"
            "08c03000: 0000000b 07980000 00000004 00000002\n"
            "08c03010: 00000800 0000000b 00000000 00000000\n"
            "08c03020: 00000000\n"
        )
        result = measure.resolve_repeated_r012_sbl1_chain(
            measure.parse_memory_dump(self.write("shared-a.raw", shared_text)),
            measure.parse_memory_dump(self.write("shared-b.raw", shared_text)),
            measure.parse_memory_dump(self.write("flash-a.raw", flash_text)),
            measure.parse_memory_dump(self.write("flash-b.raw", flash_text)),
        )
        self.assertEqual(result["stability"], "STABLE_DOUBLE_READ")
        self.assertEqual(result["selected_ecc"], 4)
        self.assertIn("does not establish producer identity", result["stability_scope"])
        self.assertIn("do not prove SRAM producer", result["qualification"])

    def test_repeated_chain_mismatch_stays_unknown(self):
        shared = (
            "md.l 08c08420 c\n"
            "08c08420: 00000000 00000000 00000000 00000000\n"
            "08c08430: 00000000 00000000 00000000 00000000\n"
            "08c08440: 00000000 00000000 00000000 08c03000\n"
        )
        shared_changed = shared.replace("08c03000\n", "08c03100\n")
        flash = (
            "md.l 08c03000 9\n"
            "08c03000: 0000000b 07980000 00000004 00000002\n"
            "08c03010: 00000800 0000000b 00000000 00000000\n"
            "08c03020: 00000000\n"
        )
        result = measure.resolve_repeated_r012_sbl1_chain(
            measure.parse_memory_dump(self.write("shared-a.raw", shared)),
            measure.parse_memory_dump(self.write("shared-b.raw", shared_changed)),
            measure.parse_memory_dump(self.write("flash-a.raw", flash)),
            measure.parse_memory_dump(self.write("flash-b.raw", flash)),
        )
        self.assertEqual(result["status"], "UNKNOWN")
        self.assertEqual(result["selected_ecc"], "UNKNOWN")
        self.assertEqual(result["stability"], "MISMATCH")

    def test_bridge4_history_is_scoped_and_not_reused(self):
        install = self.root / "install.json"
        cold = self.root / "cold.json"
        final = self.root / "final.json"
        install.write_text(json.dumps({"result": "PASS", "target": {"offset": "0x500000", "size": 1310720}, "candidate": {"sha256": "a" * 64}}))
        cold.write_text(json.dumps({"result": "PASS_COLD_NAND_DEFAULT_WEB"}))
        final.write_text(json.dumps({"sample_id": "historical-sample", "scope": "specific sample and old candidate", "full9MiB_exact_expected": True}))
        result = measure.parse_bridge4_history(install, cold, final)
        self.assertEqual(result["historical_candidate_sha256"], "a" * 64)
        self.assertEqual(result["historical_target"]["offset"], "0x500000")
        self.assertFalse(result["transfer_to_current_candidate"])


if __name__ == "__main__":
    unittest.main()
