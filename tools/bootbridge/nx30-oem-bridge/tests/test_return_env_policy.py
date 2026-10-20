#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Host checks for exact source-bound return ENV bootargs policy."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "RC": "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44",
    "RT": "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b",
    "RW": "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5",
    "NX": "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922",
}
DEFAULTS = (
    "console=ttyMSM0,115200n8",
    "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3",
)
NX_BDF = ("console=ttyMSM0,115200n8 cnss2.bdf_integrated=0x10 "
          "cnss2.bdf_pci0=0x60 cnss2.bdf_pci1=0x60 "
          "cnss2.skip_radio_bmap=4 loglevel=3")
HARNESS = r'''#include <stdlib.h>
#include "bridge_return_env_policy.h"
int main(int argc, char **argv) {
    int expected;
    if (argc != 5) return 2;
    expected = atoi(argv[4]);
    return !!br_return_bootargs_allowed(argv[1], argv[2], argv[3]) == expected ? 0 : 1;
}
'''


class ReturnEnvPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = shutil.which(os.environ.get("CC", "cc"))
        if not cc:
            raise unittest.SkipTest("host C compiler unavailable")
        # Use a fresh directory on TMPDIR's disk to avoid filling the project tree.
        cls.temp = tempfile.TemporaryDirectory(prefix="return-env-policy-",
                                                dir=os.environ.get("TMPDIR") or None)
        temp = Path(cls.temp.name)
        source = temp / "policy.c"
        cls.binary = temp / "policy"
        source.write_text(HARNESS, encoding="ascii")
        subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "src"), str(source), "-o", str(cls.binary)],
                       check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def check_policy(self, source, route, value, allowed):
        subprocess.run([str(self.binary), source, route, value, "1" if allowed else "0"],
                       check=True, capture_output=True, text=True)

    def test_four_exact_sources_accept_both_default_strings(self):
        routes = {"RC": "config@mp03.1", "RT": "config@mp02.1",
                  "RW": "config@mp03.1-c2", "NX": "config@mp02.1"}
        for name, source in SOURCES.items():
            for value in DEFAULTS:
                with self.subTest(source=name, bootargs=value):
                    self.check_policy(source, routes[name], value, True)

    def test_reviewed_nx_mp02_bdf_variant_is_narrowly_allowed(self):
        self.check_policy(SOURCES["NX"], "config@mp02.1", NX_BDF, True)
        for name in ("RC", "RT", "RW"):
            with self.subTest(source=name):
                self.check_policy(SOURCES[name], "config@mp02.1", NX_BDF, False)
        for route in ("config@mp03.1", "config@mp03.1-c2", "config@other"):
            with self.subTest(route=route):
                self.check_policy(SOURCES["NX"], route, NX_BDF, False)

    def test_unknown_source_and_production_synthetic_hash_are_rejected(self):
        self.check_policy("f" * 64, "config@mp02.1", DEFAULTS[0], False)
        # The compile intentionally omits BRIDGE_INSTALL_TEST.
        self.check_policy("a" * 64, "config@mp02.1", DEFAULTS[0], False)

    def test_bootarg_injection_mutations_are_rejected(self):
        source = SOURCES["NX"]
        route = "config@mp02.1"
        dangerous = (
            "init=/bin/sh",
            "console=ttyMSM0,115200n8 init=/bin/sh",
            "console=ttyMSM0,115200n8 mtdparts=",
            "console=ttyMSM0,115200n8 mem=256M",
            "console=ttyMSM0,115200n8 root=/dev/mtdblock1",
        )
        for value in dangerous:
            with self.subTest(value=value):
                self.check_policy(source, route, value, False)

    def test_reordered_extra_missing_duplicate_and_empty_values_are_rejected(self):
        source = SOURCES["NX"]
        route = "config@mp02.1"
        mutations = (
            "cnss2.skip_radio_bmap=4 loglevel=3 console=ttyMSM0,115200n8",
            "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3 extra=1",
            "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4",
            "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 "
            "cnss2.skip_radio_bmap=4 loglevel=3",
            "",
        )
        for value in mutations:
            with self.subTest(value=value):
                self.check_policy(source, route, value, False)


if __name__ == "__main__":
    unittest.main()
