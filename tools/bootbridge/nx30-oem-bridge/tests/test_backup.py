"""Synthetic only: actual C ZIP output parsed independently by Python."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "bridge_backup.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct fixture { FILE *file; uint64_t reads,written,first_pass; const char *mode; };
static int read_at(void *ctx,uint64_t offset,void *buffer,size_t n) {
 struct fixture *f=ctx; unsigned char *p=buffer; size_t i;
 if(!strcmp(f->mode,"readfail")) return -1;
 if(!strcmp(f->mode,"secondreadfail")&&f->reads>=f->first_pass) return -1;
 for(i=0;i<n;i++) p[i]=(unsigned char)((offset+i)%251U);
 if(!strcmp(f->mode,"changed")&&f->reads>=f->first_pass) p[0]^=1;
 f->reads+=n; return 0;
}
static int sink(void *ctx,const void *buffer,size_t n) {
 struct fixture *f=ctx;
 if(!strcmp(f->mode,"sinkfail")&&f->written+n>128) return -1;
 if(fwrite(buffer,1,n,f->file)!=n) return -1;
 f->written+=n; return 0;
}
int main(int argc,char **argv) {
 struct bbk_entry e[33]={{"sbl1.bin",0,0x20000,BBK_STARTUP},
  {"pdt_data.bin",0x5900000,0x20000,BBK_OEM_PERSISTENT_DATA}};
 struct bbk_spec s={BBK_LAYOUT_H3C_D_128M_DUAL40_V1,BBK_ECC_DECODED_MAIN_NO_OOB,
  {0x8000000,2048,131072,64,4},e,2};
 struct fixture f={0}; struct bbk_io io={&f,read_at,&f,sink}; int result;
 size_t i;
 if(argc!=3) return 2;
 f.mode=argv[1];
 if(!strcmp(f.mode,"full")) {
  strcpy(e[0].name,"startup.bin");e[0].size=0x900000;
  e[1].size=0x600000;
  strcpy(e[2].name,"pdt_data_1.bin");e[2].offset=0x5f00000;e[2].size=0x600000;e[2].role=BBK_OEM_PERSISTENT_DATA;
  strcpy(e[3].name,"exp.bin");e[3].offset=0x6500000;e[3].size=0x100000;e[3].role=BBK_OEM_PERSISTENT_DATA;
  strcpy(e[4].name,"plugin.bin");e[4].offset=0x6600000;e[4].size=0x1700000;e[4].role=BBK_OEM_PERSISTENT_DATA;
  s.entry_count=5;
 }
 if(!strcmp(f.mode,"maxentries")) {
  s.entry_count=32;
  for(i=0;i<32;i++) {
   snprintf(e[i].name,sizeof(e[i].name),"part%02u.bin",(unsigned)i);
   e[i].offset=i*2048;e[i].size=2048;e[i].role=BBK_STARTUP;
  }
 }
 if(!strcmp(f.mode,"geometry")) s.geometry.page_bytes=4096;
 if(!strcmp(f.mode,"capacity256")) {s.layout=BBK_LAYOUT_H3C_D_DUAL40_V2;s.geometry.capacity_bytes=0x10000000;}
 if(!strcmp(f.mode,"capacity")) s.geometry.capacity_bytes=UINT64_MAX;
 if(!strcmp(f.mode,"ecc")) s.geometry.ecc_bits=8;
 if(!strcmp(f.mode,"raw_oob")) s.format=(enum bbk_format)2;
 if(!strcmp(f.mode,"layout")) s.layout=(enum bbk_layout)0;
 if(!strcmp(f.mode,"duplicate")) strcpy(e[1].name,"SBL1.bin");
 if(!strcmp(f.mode,"path")) strcpy(e[0].name,"../env.bin");
 if(!strcmp(f.mode,"absolute")) strcpy(e[0].name,"/env.bin");
 if(!strcmp(f.mode,"backslash")) strcpy(e[0].name,"dir\\env.bin");
 if(!strcmp(f.mode,"reserved")) strcpy(e[0].name,"CON.bin");
 if(!strcmp(f.mode,"unterminated")) memset(e[0].name,'a',sizeof(e[0].name));
 if(!strcmp(f.mode,"overlap")) {e[1].offset=0;e[1].role=BBK_STARTUP;}
 if(!strcmp(f.mode,"system_a")) e[0].offset=0x900000;
 if(!strcmp(f.mode,"system_b")) e[0].offset=0x3100000;
 if(!strcmp(f.mode,"cross_system")) {e[0].offset=0x8e0000;e[0].size=0x40000;}
 if(!strcmp(f.mode,"tail")) {e[1].offset=0x7d00000;}
 if(!strcmp(f.mode,"cross_config")) {e[1].offset=0x5ee0000;e[1].size=0x40000;}
 if(!strcmp(f.mode,"end_overflow")) e[1].offset=UINT64_MAX-2047;
 if(!strcmp(f.mode,"size_overflow")) e[1].size=UINT64_MAX;
 if(!strcmp(f.mode,"limit64")) e[0].size=64U*1024U*1024U+2048;
 if(!strcmp(f.mode,"limit32")) e[0].size=32U*1024U*1024U+2048;
 if(!strcmp(f.mode,"unaligned")) e[0].offset=1;
 if(!strcmp(f.mode,"zero")) e[0].size=0;
 if(!strcmp(f.mode,"count")) s.entry_count=33;
 if(!strcmp(f.mode,"noentries")) s.entry_count=0;
 if(!strcmp(f.mode,"unknown_role")) e[1].role=(enum bbk_role)99;
 if(!strcmp(f.mode,"ecc128_4")) s.geometry.physical_spare_bytes=128;
 if(!strcmp(f.mode,"ecc128_8")) {s.geometry.physical_spare_bytes=128;s.geometry.ecc_bits=8;}
 for(i=0;i<s.entry_count&&i<33;i++) f.first_pass+=e[i].size;
 f.file=fopen(argv[2],"wb");if(!f.file) return 3;
 result=bbk_write_zip(&s,&io);
 if(fclose(f.file)) result=BBK_SINK_FAILED;
 if(result!=BBK_OK) remove(argv[2]); /* Required caller discipline. */
 printf("{\"result\":%d,\"reads\":%" PRIu64 ",\"written\":%" PRIu64 "}\n",result,f.reads,f.written);
 return 0;
}
'''


class BackupTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="bridge-backup-")
        cls.directory = Path(cls.temp.name)
        source = cls.directory / "harness.c"
        source.write_text(HARNESS)
        cls.exe = cls.directory / "backup"
        cmd = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(ROOT / "src"), str(ROOT / "src/bridge_backup.c"),
               str(source), "-o", str(cls.exe)]
        if os.environ.get("BBK_SANITIZE") == "1":
            cmd[2:2] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
        subprocess.run(cmd, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_mode(self, mode):
        archive = self.directory / (mode + ".zip")
        result = subprocess.run([str(self.exe), mode, str(archive)], check=True,
                                capture_output=True, text=True)
        return json.loads(result.stdout), archive

    def inspect(self, archive, expected_sizes):
        with zipfile.ZipFile(archive) as z:
            self.assertIsNone(z.testzip())
            self.assertEqual(set(z.namelist()), set(expected_sizes) | {"manifest.json"})
            manifest = json.loads(z.read("manifest.json"))
            self.assertEqual(manifest["format_version"], 1)
            self.assertEqual(manifest["layout_id"], "h3c-d-128m-dual40-v1")
            self.assertEqual(manifest["data_format"], "ecc-decoded-main-area")
            self.assertFalse(manifest["contains_oob"])
            self.assertFalse(manifest["contains_ecc_bytes"])
            self.assertEqual(manifest["ecc_scope"], "read-io-mode-only")
            self.assertEqual(manifest["preloader_format"], "unknown")
            self.assertEqual(manifest["appsbl_storage_format"], "unknown")
            self.assertFalse(manifest["restore_format_qualified"])
            self.assertFalse(manifest["restore_api"])
            self.assertEqual(manifest["data_bytes"], sum(expected_sizes.values()))
            self.assertEqual(manifest["data_limit_bytes"], 64 * 1024 * 1024)
            self.assertEqual(manifest["excluded_system_slots"], [
                {"offset": 0x900000, "size": 0x2800000},
                {"offset": 0x3100000, "size": 0x2800000}])
            self.assertEqual(manifest["excluded_unpartitioned_tail"],
                             {"offset": 0x7d00000, "size": 0x300000})
            self.assertEqual(len(manifest["entries"]), len(expected_sizes))
            for entry in manifest["entries"]:
                name = entry["name"]
                data = z.read(name)
                self.assertEqual(len(data), expected_sizes[name])
                self.assertEqual(entry["size"], len(data))
                self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
                self.assertEqual(entry["crc32"], f"{zlib.crc32(data):08x}")
                self.assertFalse(entry["contains_oob"])
                self.assertEqual(entry["data_format"], "ecc-decoded-main-area")
                # Check every physical-offset-dependent byte independently.
                pattern = bytes(range(251))
                shift = entry["offset"] % 251
                block = pattern[shift:] + pattern[:shift]
                expected = (block * ((len(data) + 250) // 251))[:len(data)]
                self.assertEqual(data, expected)
                info = z.getinfo(name)
                self.assertEqual(info.compress_type, zipfile.ZIP_STORED)
                self.assertEqual(info.flag_bits, 0)
                self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                self.assertEqual((info.external_attr >> 16) & 0o777, 0o600)
            self.assertFalse({"sn", "mac", "password", "credentials"} & set(manifest))
            return manifest

    def test_archive(self):
        for mode in ["normal", "ecc128_4", "ecc128_8"]:
            with self.subTest(mode=mode):
                result, archive = self.run_mode(mode)
                self.assertEqual(result["result"], 0)
                self.assertEqual(result["reads"], 4 * 0x20000)
                self.inspect(archive, {"sbl1.bin": 0x20000, "pdt_data.bin": 0x20000})

    def test_catalogued_256m_capacity_uses_profile_specific_layout(self):
        result, archive = self.run_mode("capacity256")
        self.assertEqual(result["result"], 0)
        with zipfile.ZipFile(archive) as z:
            manifest = json.loads(z.read("manifest.json"))
        self.assertEqual(manifest["layout_id"], "h3c-dual40-v2")
        self.assertEqual(manifest["geometry"]["capacity_bytes"], 0x10000000)
        self.assertEqual(manifest["excluded_unpartitioned_tail"],
                         {"status": "not-captured-profile-specific"})

    def test_maximum_entry_count(self):
        result, archive = self.run_mode("maxentries")
        self.assertEqual(result["result"], 0)
        self.inspect(archive, {f"part{i:02d}.bin": 2048 for i in range(32)})

    @unittest.skipIf(os.environ.get("BBK_SANITIZE") == "1", "45 MiB full coverage runs once in ordinary build")
    def test_full_45mib(self):
        result, archive = self.run_mode("full")
        self.assertEqual(result["result"], 0)
        self.assertEqual(result["reads"], 2 * 45 * 1024 * 1024)
        manifest = self.inspect(archive, {"startup.bin": 0x900000, "pdt_data.bin": 0x600000,
            "pdt_data_1.bin": 0x600000, "exp.bin": 0x100000, "plugin.bin": 0x1700000})
        self.assertEqual(manifest["entries"][-1]["role"], "oem-persistent-data")

    def test_rejected_before_read_or_output(self):
        modes = ["geometry", "capacity", "ecc", "raw_oob", "layout", "duplicate", "path",
            "absolute", "backslash", "reserved", "unterminated", "overlap", "system_a",
            "system_b", "cross_system", "tail", "cross_config", "end_overflow", "size_overflow",
            "limit64", "limit32", "unaligned", "zero", "count", "noentries", "unknown_role"]
        for mode in modes:
            with self.subTest(mode=mode):
                result, archive = self.run_mode(mode)
                self.assertNotEqual(result["result"], 0)
                self.assertEqual(result["reads"], 0)
                self.assertEqual(result["written"], 0)
                self.assertFalse(archive.exists())

    def test_io_failure_and_change_discard_output(self):
        for mode, code in [("readfail", -4), ("secondreadfail", -4), ("sinkfail", -5), ("changed", -6)]:
            with self.subTest(mode=mode):
                result, archive = self.run_mode(mode)
                self.assertEqual(result["result"], code)
                self.assertFalse(archive.exists())
                if mode in ("sinkfail", "changed", "secondreadfail"):
                    self.assertGreater(result["written"], 0)


if __name__ == "__main__":
    unittest.main()
