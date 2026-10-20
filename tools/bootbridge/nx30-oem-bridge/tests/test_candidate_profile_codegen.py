# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import struct
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("candidate_profile_codegen", ROOT / "tools/candidate_profile_codegen.py")
codegen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codegen)
sys.path.insert(0, str(ROOT / "tools"))
import build


def sha(data):
    return hashlib.sha256(data).hexdigest()


def elf_with_bss(start=0x4A930000, end=0x4A931000):
    # Minimal ELF32 containing a full SHT_SYMTAB and linked string table.
    strings = b"\0__bss_start\0__bss_end\0"
    sym = bytes(16) + struct.pack("<IIIBBH", 1, start, 0, 0x10, 0, 1) + struct.pack("<IIIBBH", 13, end, 0, 0x10, 0, 1)
    shoff = 52
    symoff = shoff + 3 * 40
    stroff = symoff + len(sym)
    total = stroff + len(strings)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 40, 1, 0x4A920000, 0, shoff, 0x05000200,
                         52, 32, 0, 40, 3, 0)
    sections = [bytes(40), struct.pack("<10I", 0, 2, 0, 0, symoff, len(sym), 2, 1, 4, 16),
                struct.pack("<10I", 0, 3, 0, 0, stroff, len(strings), 0, 0, 1, 0)]
    return header + b"".join(sections) + sym + strings


def fixture():
    elf = elf_with_bss()
    load_data = b"L" * 0x1000
    load_addr, load_off = 0x4A920000, 0x3000
    hash_addr = (load_addr + len(load_data) + 0xFFF) & ~0xFFF
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 40, 1, load_addr,
                         52, 0, 0x05000200, 52, 32, 4, 40, 0, 0)
    ph = b"".join(struct.pack("<8I", *x) for x in (
        (0, 0, 0, 0, 0xB4, 0, 0x07000000, 0),
        (0, 0x1000, hash_addr, hash_addr, 0xA8, 0x1000, 0, 0x1000),
        (1, load_off, load_addr, load_addr, len(load_data), len(load_data), 5, 0x1000),
        (0x6474E551, 0x2000, 0, 0, 0, 0, 7, 0x10)))
    mbn = bytearray(header + ph + bytes(0x1000-(len(header)+len(ph))) + bytes(0xA8))
    # Qualcomm MBN-v3 header and LOAD hashes.
    words = (21, 3, 0, hash_addr + 0x1028, 128, 128,
             hash_addr + 0x10A8, 0, hash_addr + 0x10A8, 0)
    struct.pack_into("<10I", mbn, 0x1000, *words)
    mbn[0x1028:0x1048] = hashlib.sha256(mbn[:0xB4]).digest()
    mbn[0x1068:0x1088] = hashlib.sha256(load_data).digest()
    mbn.extend(bytes(0x3000-len(mbn)))
    mbn.extend(load_data)
    mbn = bytes(mbn)
    part = mbn + b"\xff" * (0x140000-len(mbn))
    cand = {"board": "C", "candidate_id": "snapshot:HW-C:profile:load4-store4:offline",
            "nand_profile_id": "nand-profile-x", "load_ecc_bits": 4, "storage_ecc_bits": 4,
            "files": {"appsbl.mbn": {"bytes": len(mbn), "sha256": sha(mbn)},
                      "appsbl.partition.bin": {"bytes": len(part), "sha256": sha(part)},
                      "appsbl.elf": {"bytes": len(elf), "sha256": sha(elf)}},
            "container": {"entry": 0x4A920000, "elf_type": "ET_EXEC", "elf_flags": 0x05000200,
                          "load": {"file_offset": 0x3000, "bytes": 0x1000,
                                   "sha256": sha(load_data)}}}
    cand["_test_mbn"] = mbn
    cand["_test_partition"] = part
    nand = {"profile_id": "nand-profile-x", "model_aliases": ["NAND-X"],
            "jedec_bytes_observed": 2, "manufacturer_id": 0xEF, "chip_id": 0xBA,
            "capacity_mib": 128, "page_bytes": 2048, "erase_bytes": 131072,
            "raw_spare_bytes": 64, "mtd_oob_bytes": 64, "ecc_bits": 4,
            "catalogue_eligible": 4}
    build = {"board": "C", "target": "CONFIG_H3C_C", "defconfig_sha256": "a"*64,
             "dts_sha256": "b"*64, "dt_compatible": "h3c,c;qca,ipq5018", "machid": "08040101",
             "mtd_oob_bytes": 64, "filter_env_root_keys": 1}
    bss = {"board": "C", "nand_profile_id": "nand-profile-x", "load_ecc_bits": 4,
           "storage_ecc_bits": 4, "elf_sha256": sha(elf), "bss_start": 0x4A930000,
           "bss_end": 0x4A931000, "bss_bytes": 0x1000, "sdram_limit": 0x4AA00000}
    return cand, elf, nand, build, bss


class CandidateProfileCodegenTests(unittest.TestCase):
    def test_public_header_candidate_identity_is_profile_bound(self):
        candidate, _elf, _nand, _build_profile, _bss = fixture()
        components = {"sbl_sha256": "a" * 64, "sbl_valid_bytes": 128,
                      "appsbl_padded_sha256": "c" * 64}
        catalog = {"catalog_sha256": "d" * 64, "supported": ["nand-profile-x"],
                   "load": ["nand-profile-x"], "storage": ["nand-profile-x"]}
        profile = {"class": "C", "source_sha256": "e" * 64,
                   "product_name": "RC3000", "ecc": 4}
        header = build.generated_public_payload_header(
            ROOT / "src/bridge_payload.h", candidate["_test_partition"], candidate,
            catalog, components, profile)
        self.assertIn('#define BI_FIRST_INSTALL_RECORD "hw-c-public-57d1-controlled-trial-v1"',
                      header)

        fresh_profile = dict(profile, candidate_id=candidate["candidate_id"])
        fresh = build.generated_public_payload_header(
            ROOT / "src/bridge_payload.h", candidate["_test_partition"], candidate,
            catalog, components, fresh_profile)
        self.assertIn(f'#define BI_FIRST_INSTALL_RECORD "{candidate["candidate_id"]}"', fresh)
        mismatched = dict(fresh_profile, candidate_id="snapshot:HW-C:other:offline")
        with self.assertRaisesRegex(ValueError, "exactly match"):
            build.generated_public_payload_header(
                ROOT / "src/bridge_payload.h", candidate["_test_partition"], candidate,
                catalog, components, mismatched)
        empty = dict(fresh_profile, candidate_id="")
        with self.assertRaisesRegex(ValueError, "exactly match"):
            build.generated_public_payload_header(
                ROOT / "src/bridge_payload.h", candidate["_test_partition"], candidate,
                catalog, components, empty)

    def test_emits_singleton_profile_and_identity_policy(self):
        bfi, bii = codegen.render_headers(*fixture())
        self.assertIn("bfi_generated_candidate_refs[]", bfi)
        self.assertIn("0x4a931000u", bfi)
        self.assertIn("64u, 4u", bfi)
        self.assertIn("BII_EXTRA_CANDIDATE_POLICIES", bii)
        self.assertIn('"08040101"', bii)

    def test_rejects_wrong_source_nand_oob(self):
        args = list(fixture())
        args[2] = dict(args[2], mtd_oob_bytes=128)
        with self.assertRaises(codegen.ProfileCodegenError):
            codegen.render_headers(*args)

    def test_rejects_load_storage_ecc_conversion(self):
        args = list(fixture())
        args[0] = dict(args[0], storage_ecc_bits=8)
        with self.assertRaisesRegex(codegen.ProfileCodegenError, "does not convert"):
            codegen.render_headers(*args)

    def test_rejects_bss_metadata_not_bound_to_elf_symbols(self):
        args = list(fixture())
        args[4] = dict(args[4], bss_end=0x4A932000, bss_bytes=0x2000)
        with self.assertRaises(codegen.ProfileCodegenError):
            codegen.render_headers(*args)

    def test_rejects_changed_elf_bytes(self):
        args = list(fixture())
        args[1] = args[1] + b"x"
        with self.assertRaises(codegen.ProfileCodegenError):
            codegen.render_headers(*args)

    def test_accepts_only_exact_et_exec_and_et_dyn_header_pairs(self):
        args = list(fixture())
        codegen.render_headers(*args)
        dyn_elf = bytearray(args[1])
        struct.pack_into("<H", dyn_elf, 16, 3)
        struct.pack_into("<I", dyn_elf, 36, 0x05000202)
        dyn_candidate = dict(args[0], files=dict(args[0]["files"]))
        dyn_candidate["files"]["appsbl.elf"] = dict(
            args[0]["files"]["appsbl.elf"], bytes=len(dyn_elf), sha256=sha(dyn_elf))
        dyn_candidate["container"] = dict(args[0]["container"],
            elf_type="ET_DYN", elf_flags=0x05000202)
        dyn_bss = dict(args[4], elf_sha256=sha(dyn_elf))
        codegen.render_headers(dyn_candidate, bytes(dyn_elf), *args[2:4], dyn_bss)
        for elf_type, flags in ((2, 0x05000202), (3, 0x05000200),
                                (2, 0x05000000), (3, 0x05000200)):
            bad = bytearray(args[1])
            struct.pack_into("<H", bad, 16, elf_type)
            struct.pack_into("<I", bad, 36, flags)
            changed_candidate = dict(args[0], files=dict(args[0]["files"]))
            changed_candidate["files"]["appsbl.elf"] = dict(
                args[0]["files"]["appsbl.elf"], bytes=len(bad), sha256=sha(bad))
            changed_candidate["container"] = dict(args[0]["container"],
                elf_type="ET_EXEC" if elf_type == 2 else "ET_DYN", elf_flags=flags)
            with self.assertRaises(codegen.ProfileCodegenError):
                codegen.render_headers(changed_candidate, bytes(bad), *args[2:])

    def test_compiled_profile_accepts_only_exact_candidate_and_board(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("host C compiler unavailable")
        candidate, elf, nand, build, bss = fixture()
        nand = dict(nand, raw_spare_bytes=128, mtd_oob_bytes=128)
        build = dict(build, mtd_oob_bytes=[128])
        bfi, bii = codegen.render_headers(candidate, elf, [nand], build, bss)
        mbn = candidate["_test_mbn"]
        mbn_len = candidate["files"]["appsbl.mbn"]["bytes"]
        self.assertEqual(len(mbn), mbn_len)
        partition = candidate["_test_partition"]
        self.assertEqual(sha(mbn), candidate["files"]["appsbl.mbn"]["sha256"])
        self.assertEqual(sha(partition), candidate["files"]["appsbl.partition.bin"]["sha256"])
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "bfi_candidate_profile.generated.h").write_text(bfi)
            (root / "bii_candidate_policy.generated.h").write_text(bii)
            (root / "candidate.bin").write_bytes(partition)
            harness = root / "harness.c"
            harness.write_text('''#include "bridge_first_install.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){(void)argc;FILE*f=fopen(argv[1],"rb");unsigned char*p=malloc(0x140000);if(!f||fread(p,1,0x140000,f)!=0x140000)return 2;
const bfi_candidate_profile*q=bfi_candidate_profile_select(BFI_BOARD_C);if(!q||q->candidate_count!=1||q->mtd_oob!=128||q->raw_spare!=128||q->accepted_mtd_oob_mask!=2)return 3;
if(!bfi_board_profile_accepts_mtd_oob(q,128)||bfi_board_profile_accepts_mtd_oob(q,64)||bfi_board_profile_accepts_mtd_oob(q,256))return 8;
if(!bfi_candidate_profile_accepts(q,q->candidates[0]))return 7;
if(!bfi_candidate_select_for_profile(q,p,0x140000))return 4;
if(bfi_board_candidate_select("B",p,0x140000))return 5;
p[0x4000]^=1;if(bfi_board_candidate_select("C",p,0x140000))return 6;return 0;}\n''')
            cmd = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                   "-I", str(ROOT / "src"), '-DBFI_PROFILE_RECORD_HEADER="bfi_candidate_profile.generated.h"',
                   str(harness), str(ROOT / "src/bridge_first_install.c"),
                   "-o", str(root / "run")]
            subprocess.run(cmd, check=True, text=True)
            subprocess.run([str(root / "run"), str(root / "candidate.bin")], check=True)
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                            "-I", str(ROOT / "src"),
                            '-DBII_CANDIDATE_POLICY_HEADER="bii_candidate_policy.generated.h"',
                            "-c", str(ROOT / "src/bridge_install_identity.c"),
                            "-o", str(root / "identity.o")], check=True, capture_output=True, text=True)

    def test_generated_install_profile_shares_candidate_record_across_translation_units(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("host C compiler unavailable")
        candidate, elf, nand, build_profile, bss = fixture()
        nand = dict(nand, raw_spare_bytes=128, mtd_oob_bytes=128,
                    accepted_mtd_oob_mask=2)
        build_profile = dict(build_profile, mtd_oob_bytes=[128])
        bfi, _ = codegen.render_headers(candidate, elf, [nand], build_profile, bss)
        identity = {"model": "RC3000", "sha256": "f" * 64}
        components = {"sbl_sha256": "a" * 64, "sbl_valid_bytes": 128,
                      "sbl_extent_sha256": "b" * 64,
                      "appsbl_padded_sha256": "c" * 64, "appsbl_valid_bytes": 128}
        profile_header = build.generated_first_install_profile_header(
            "C", identity, components, nand_profile=nand,
            candidate_id=candidate["candidate_id"])
        self.assertIn(f'#define BI_FIRST_INSTALL_RECORD "{candidate["candidate_id"]}"',
                      profile_header)
        self.assertNotIn('bfi_generated_candidate_refs', profile_header)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "bfi_candidate_profile.generated.h").write_text(bfi)
            (root / "bridge_first_install_profile.h").write_text(profile_header)
            main = root / "cross_tu.c"
            main.write_text('''#define BI_RETURN_SOURCE_PRODUCT 2u
#include "bridge_first_install_profile.h"
#include "bfi_candidate_profile.generated.h"
int main(void){const bfi_board_record*p=bfi_board_candidate_record("C");
const bfi_install_profile*i=bootbridge_first_install_profiles;
return p&&p->candidate_count==1&&p->candidates[0]==&bfi_generated_candidate&&
       i->candidates==NULL&&i->candidate_count==0&&
       i[1].candidates==NULL&&i[1].candidate_count==0?0:1;}
''')
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                "-I", str(ROOT / "src"), '-DBFI_PROFILE_RECORD_HEADER="bfi_candidate_profile.generated.h"',
                str(main), str(ROOT / "src/bridge_first_install.c"), "-o", str(root / "cross_tu")],
                check=True, capture_output=True, text=True)
            subprocess.run([str(root / "cross_tu")], check=True)


if __name__ == "__main__":
    unittest.main()
