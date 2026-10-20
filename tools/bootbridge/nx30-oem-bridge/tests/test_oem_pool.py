#!/usr/bin/env python3
"""Synthetic production-C measurement/pool harness, actually run with ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class PoolTests(unittest.TestCase):
    def test_direct_sanitized_c_harness(self):
        with tempfile.TemporaryDirectory(prefix='oem-pool-synthetic-') as tmp:
            exe=Path(tmp)/'pool'
            subprocess.run(['cc','-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-pedantic',
                '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
                '-I'+str(ROOT/'src'),str(ROOT/'src/bridge_oem_slot.c'),
                str(ROOT/'tests/oem_pool_harness.c'),'-o',str(exe)],check=True)
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            subprocess.run([str(exe)],check=True,env=env,timeout=90)
if __name__=='__main__':unittest.main()
