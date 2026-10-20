#!/usr/bin/env python3
"""Legacy per-class rows remain separate and therefore fail closed on duplicate sources."""
import hashlib
import importlib.util
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parent
EVIDENCE=ROOT.parent.parent/"work/offline-release-20261016/desktop-pair-review-build2"
SPEC=importlib.util.spec_from_file_location("offline_builder_test",HERE/"build_offline_catalog.py")
BUILDER=importlib.util.module_from_spec(SPEC); sys.modules[SPEC.name]=BUILDER; SPEC.loader.exec_module(BUILDER)

class OfflineCatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("gcc"): raise unittest.SkipTest("gcc unavailable")
        if not (EVIDENCE/"evidence.json").is_file(): raise unittest.SkipTest("controlled assembly evidence unavailable")
        cls.temp=tempfile.TemporaryDirectory(prefix="bb-offline-catalog-")
        cls.base=pathlib.Path(cls.temp.name); cls.gen=cls.base/"generated"
        n,d=BUILDER.build(EVIDENCE,cls.gen); cls.count=n; cls.digest=d
        cls.exe=cls.base/"offline-test"
        subprocess.run(["gcc","-std=c99","-Wall","-Wextra","-Werror","-DBB_OFFLINE_TEST",
                        "-I"+str(cls.gen),"-I"+str(ROOT),"-I"+str(ROOT/"catalog"),
                        str(ROOT/"offline/test_offline_rules.c"),str(ROOT/"offline/bb_offline_rules.c"),
                        str(ROOT/"catalog/bb_catalog.c"),"-o",str(cls.exe)],check=True)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def run_loader(self,data_path,success):
        env=dict(__import__("os").environ,BB_OFFLINE_TEST_DATA=str(data_path))
        result=subprocess.run([str(self.exe)],env=env,capture_output=True,text=True)
        self.assertEqual(result.returncode,0 if success else 10,result.stdout+result.stderr)
    def test_valid_sidecar_keeps_provider_rows_and_selector_refuses_duplicate_sources(self):
        data=(self.gen/"BootBridge.rules.dat").read_bytes()
        header=(self.gen/"bb_offline_catalog_generated.h").read_text()
        self.assertEqual(hashlib.sha256(data).hexdigest(),self.digest)
        self.assertIn("BB_OFFLINE_DATA_SIZE %dULL"%len(data),header)
        self.assertEqual(self.count,len(json.loads((EVIDENCE/"catalog.json").read_text())["rules"]))
        self.assertIn("bb_offline_bundles[]",header)
        self.assertIn("BB_BUNDLE_CATALOG_SCHEMA",(ROOT/"catalog/bb_catalog.h").read_text())
        catalog=json.loads((EVIDENCE/"catalog.json").read_text())
        rt=[r for r in catalog["rules"] if r["model"]=="RT3000"]
        self.assertEqual({r["board"] for r in rt},{"HW-B","HW-C"})
        self.assertEqual(len({r["input_id"] for r in rt}),1,"RT B/C must share the exact OEM input identity")
        self.run_loader(self.gen/"BootBridge.rules.dat",True)
    def test_missing_truncated_and_tampered_sidecars_refuse(self):
        src=self.gen/"BootBridge.rules.dat"; raw=src.read_bytes()
        missing=self.base/"missing.dat"; self.run_loader(missing,False)
        short=self.base/"short.dat"; short.write_bytes(raw[:-1]); self.run_loader(short,False)
        bad=self.base/"bad.dat"; altered=bytearray(raw); altered[len(altered)//2]^=1; bad.write_bytes(altered); self.run_loader(bad,False)

if __name__=="__main__": unittest.main()
