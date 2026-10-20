"""Build-reviewed OEM namespaces must never promote unknown exit policies."""
import copy
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build
import build_oem_manifest as gen
import audit as package_audit


class OemPoolBuildTests(unittest.TestCase):
    @staticmethod
    def source_table():
        names = ("kernel", "wifi_fw", "bt_fw", "ubi_rootfs", "rootfs_data")
        reserves = (50, 50, 3, 137, 1)
        kinds = (2, 2, 2, 1, 1)
        table = bytearray(128 * 172)
        for index in range(128):
            row = index * 172
            if index < 5:
                struct.pack_into(">III", table, row, reserves[index], 1, 0)
                table[row + 12] = kinds[index]
                struct.pack_into(">H", table, row + 14, len(names[index]))
                table[row + 16:row + 16 + len(names[index])] = names[index].encode()
                table[row + 144] = 1 if index == 4 else 0
            struct.pack_into(">I", table, row + 168,
                             build.vendor_ubi.crc(table[row:row + 168]))
        return bytes(table)

    def test_observed_post_attach_table_is_one_exact_row_transform(self):
        source = self.source_table()
        expected = bytearray(source)
        row = 4 * 172
        struct.pack_into(">I", expected, row, 56)
        expected[row + 144] = 0
        struct.pack_into(">I", expected, row + 168,
                         build.vendor_ubi.crc(expected[row:row + 168]))
        with mock.patch.object(build, "OEM_R012_SOURCE_TABLE_SHA256", build.sha(source)), \
             mock.patch.object(build, "OEM_R012_HW_D_POST_TABLE_SHA256", build.sha(expected)):
            actual = build.observed_hw_d_r012_post_attach_table(source)
        with mock.patch.object(package_audit, "R012_SOURCE_TABLE_SHA", build.sha(source)), \
             mock.patch.object(package_audit, "R012_HW_D_POST_TABLE_SHA", build.sha(expected)):
            audited = package_audit.audit_hw_d_post_attach_table(source)
        self.assertEqual(actual, bytes(expected))
        self.assertEqual(audited, actual)
        self.assertEqual(struct.unpack_from(">I", actual, row)[0], 56)
        self.assertEqual(actual[row + 144], 0)
        with mock.patch.object(build, "OEM_R012_SOURCE_TABLE_SHA256", build.sha(source)), \
             mock.patch.object(build, "OEM_R012_HW_D_POST_TABLE_SHA256", "0" * 64):
            with self.assertRaisesRegex(ValueError, "table hash"):
                build.observed_hw_d_r012_post_attach_table(source)

    def test_generator_adds_only_the_canonical_d_b_exact_variant(self):
        identity = build.OFFICIAL_SOURCES[build.SOURCE_SHA]
        source = self.source_table()
        expected = bytearray(source)
        row = 4 * 172
        struct.pack_into(">I", expected, row, 56)
        expected[row + 144] = 0
        struct.pack_into(">I", expected, row + 168,
                         build.vendor_ubi.crc(expected[row:row + 168]))
        table_meta = {"volume_table_raw_sha256": build.sha(source)}
        table_rows = [{"raw": source}, {"raw": source}]

        def manifest(path, *, expected_ubi_sha256, candidate_sha256, observed_geometry):
            return {"schema": "bootbridge-oem-slot-manifest-v1",
                "expected_ubi_sha256": expected_ubi_sha256,
                "geometry": observed_geometry, "candidate_sha256": candidate_sha256,
                "volume_table_sha256": build.sha(source),
                "immutable": [{"name": name, "size_bytes": n + 1,
                    "sha256": str(n + 1) * 64, "type": kind}
                    for n, (name, kind) in enumerate(zip(
                        ("kernel", "wifi_fw", "bt_fw", "ubi_rootfs"), (2, 2, 2, 1)))]}

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); include = root / "include"; include.mkdir()
            with mock.patch.object(build, "OEM_R012_SOURCE_TABLE_SHA256", build.sha(source)), \
                 mock.patch.object(build, "OEM_R012_HW_D_POST_TABLE_SHA256", build.sha(expected)), \
                 mock.patch.object(build.vendor_ubi, "parse", return_value=(table_meta, {}, b"", table_rows)), \
                 mock.patch.object(build.oem_manifest, "build_manifest", side_effect=manifest), \
                 mock.patch.object(build.oem_manifest, "c_pool_header", return_value="header") as header:
                report = build.generate_oem_header(root, include, b"ubi",
                    build.D_FIRST_INSTALL_SHA, identity)
            self.assertEqual(report["count"], 7)
            self.assertEqual(report["base_count"], 6)
            self.assertEqual(report["hw_d_r012_post_attach"]["table_sha256"], build.sha(expected))
            entries = header.call_args.args[0]
            self.assertEqual(len(entries), 7)
            post = entries[-1]
            self.assertEqual(post["alias"], build.OEM_R012_HW_D_POST_ALIAS)
            self.assertEqual(post["content_group"], build.OEM_R012_HW_D_POST_GROUP)
            self.assertEqual(post["manifest"]["geometry"], {
                "physical_offset": 0x3100000, "size_bytes": 41943040,
                "erase_bytes": 131072, "page_bytes": 2048,
                "oob_bytes": 64, "ecc_bits": 4})
            self.assertEqual(post["manifest"]["volume_table_sha256"], build.sha(expected))
            self.assertEqual(post["manifest"]["candidate_sha256"], build.D_FIRST_INSTALL_SHA)
            self.assertEqual(report["hw_d_r012_post_attach"]["candidate_sha256"], build.D_FIRST_INSTALL_SHA)
            self.assertEqual(post["manifest"]["immutable"], entries[4]["manifest"]["immutable"])
            compiled = gen.c_pool_header(entries)
            self.assertEqual(compiled.count("10012u,1u,1u,10012u,0u"), 6)
            self.assertEqual(compiled.count("10013u,1u,1u,10013u,0u"), 1)
            with tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                (d / "rule.h").write_text(compiled)
                (d / "main.c").write_text('#include <stdio.h>\n#include "rule.h"\n'
                    'int main(void){printf("%zu\\n", sizeof(BI_OEM_POOL)/sizeof(BI_OEM_POOL[0]));return 0;}\n')
                subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-I", str(build.PROJECT / "src"), str(d / "main.c"), "-o", str(d / "count")], check=True)
                self.assertEqual(subprocess.check_output([str(d / "count")]).strip(), b"7")

    def test_public_d_payload_generates_candidate_bound_pool_and_audit_expectation(self):
        archive_name = os.environ.get("BOOTBRIDGE_PUBLIC_ARCHIVE")
        if not archive_name:
            self.skipTest("set BOOTBRIDGE_PUBLIC_ARCHIVE for public-D pool generation test")
        archive = Path(archive_name).resolve(strict=True)
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), build.D_PUBLIC_SOURCE_ARCHIVE_SHA)
        member = "abcd-uboot-public-review-20261016/payloads/HW-D/appsbl.partition.bin"
        with tarfile.open(archive, "r:gz") as tf:
            payload = tf.extractfile(member).read()
        candidate_sha = hashlib.sha256(payload).hexdigest()
        self.assertEqual(candidate_sha, build.D_PUBLIC_FIRST_INSTALL_SHA)

        identity = build.OFFICIAL_SOURCES[build.SOURCE_SHA]
        source = self.source_table()
        expected = bytearray(source)
        row = 4 * 172
        struct.pack_into(">I", expected, row, 56)
        expected[row + 144] = 0
        struct.pack_into(">I", expected, row + 168,
                         build.vendor_ubi.crc(expected[row:row + 168]))
        table_meta = {"volume_table_raw_sha256": build.sha(source)}
        table_rows = [{"raw": source}, {"raw": source}]

        def manifest(path, *, expected_ubi_sha256, candidate_sha256, observed_geometry):
            return {"schema": "bootbridge-oem-slot-manifest-v1",
                "expected_ubi_sha256": expected_ubi_sha256,
                "geometry": observed_geometry, "candidate_sha256": candidate_sha256,
                "volume_table_sha256": build.sha(source),
                "immutable": [{"name": name, "size_bytes": n + 1,
                    "sha256": str(n + 1) * 64, "type": kind}
                    for n, (name, kind) in enumerate(zip(
                        ("kernel", "wifi_fw", "bt_fw", "ubi_rootfs"), (2, 2, 2, 1)))]}

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); include = root / "include"; include.mkdir()
            with mock.patch.object(build, "OEM_R012_SOURCE_TABLE_SHA256", build.sha(source)), \
                 mock.patch.object(build, "OEM_R012_HW_D_POST_TABLE_SHA256", build.sha(expected)), \
                 mock.patch.object(build.vendor_ubi, "parse", return_value=(table_meta, {}, b"", table_rows)), \
                 mock.patch.object(build.oem_manifest, "build_manifest", side_effect=manifest), \
                 mock.patch.object(build.oem_manifest, "c_pool_header", return_value="header") as header:
                report = build.generate_oem_header(root, include, b"ubi", candidate_sha, identity)
                entries = header.call_args.args[0]
                self.assertEqual(report["count"], 7)
                self.assertEqual(report["hw_d_r012_post_attach"]["candidate_sha256"], candidate_sha)
                post = entries[-1]
                self.assertEqual(post["manifest"]["candidate_sha256"], candidate_sha)
                audited, audit_report = package_audit.audit_hw_d_post_attach_manifest(
                    build.SOURCE_SHA, candidate_sha, entries[3]["manifest"], source)
                self.assertEqual(audited, post["manifest"])
                self.assertEqual(audit_report, report["hw_d_r012_post_attach"])
                other_source = next(value for key, value in build.OFFICIAL_SOURCES.items()
                                    if key != build.SOURCE_SHA)
                for candidate, source_identity in (("a" * 64, identity), (candidate_sha, other_source)):
                    with self.subTest(candidate=candidate, source=source_identity["model"]):
                        rejected_report = build.generate_oem_header(
                            root, include, b"ubi", candidate, source_identity)
                        rejected_entries = header.call_args.args[0]
                        self.assertEqual(rejected_report["count"], 6)
                        self.assertIsNone(rejected_report["hw_d_r012_post_attach"])
                        self.assertEqual(len(rejected_entries), 6)

            compiled = gen.c_pool_header(entries)
            source_c = root / "public-pool.c"
            exe = root / "public-pool"
            source_c.write_text('#include <stdio.h>\n#include "rule.h"\n'
                'int main(void){return fwrite(&BI_OEM_POOL[6],sizeof(BI_OEM_POOL[6]),1,stdout)!=1;}\n')
            (root / "rule.h").write_text(compiled)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I", str(build.PROJECT / "src"), str(source_c), "-o", str(exe)], check=True)
            actual = subprocess.check_output([str(exe)])
            geometry = post["manifest"]["geometry"]
            encoded = struct.pack("<QQIIII", geometry["physical_offset"],
                geometry["size_bytes"], geometry["erase_bytes"], geometry["page_bytes"],
                geometry["oob_bytes"], geometry["ecc_bits"])
            encoded += bytes.fromhex(candidate_sha + post["manifest"]["volume_table_sha256"])
            for volume in post["manifest"]["immutable"]:
                encoded += struct.pack("<Q", volume["size_bytes"])
                encoded += bytes.fromhex(volume["sha256"])
                encoded += struct.pack("<I", volume["type"]) + bytes(4)
            encoded += struct.pack("<IIIII", build.OEM_R012_HW_D_POST_GROUP,
                post["exit_policy"], post["product"], build.OEM_R012_HW_D_POST_ALIAS,
                post["table_policy"]) + bytes(4)
            self.assertEqual(len(actual), 312)
            self.assertEqual(actual, encoded)

        self.assertIsNone(package_audit.audit_hw_d_post_attach_manifest(
            build.SOURCE_SHA, "0" * 64, entries[3]["manifest"], source))
        self.assertIsNone(package_audit.audit_hw_d_post_attach_manifest(
            "0" * 64, candidate_sha, entries[3]["manifest"], source))

    def test_exact_four_source_domains_and_exit_policy(self):
        expected = {'NX30': (1, 10012, 1), 'RC3000': (2, 21301, 0),
                    'RT3000': (3, 30015, 0), 'RW3000': (4, 40012, 0)}
        for identity in build.OFFICIAL_SOURCES.values():
            with self.subTest(model=identity['model']):
                entry, = build.oem_pool_entries([{'synthetic': True}], identity)
                self.assertEqual((entry['product'], entry['alias'], entry['exit_policy']),
                                 expected[identity['model']])
                self.assertEqual(entry['source_package_sha256'], identity['sha256'])
                self.assertEqual(entry['content_group'], entry['alias'])
                self.assertEqual(entry['table_policy'], 0)

    def test_unreviewed_or_relabelled_source_is_rejected(self):
        valid = build.OFFICIAL_SOURCES[build.SOURCE_SHA]
        for change in ({'model': 'RX3000'}, {'identity': 'NX30V100R999'},
                       {'sha256': 'a' * 64}, {'bytes': valid['bytes'] - 1},
                       {'kernel_fit_sha256': 'b' * 64}, {'extra': True}):
            bad = dict(valid, **change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                build.oem_pool_entries([], bad)
        for bad in (None, {}, 'NX30'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                build.oem_pool_entries([], bad)

    def test_compiled_rule_has_exact_arm_compatible_binding(self):
        # This checks the complete rule bytes, not C text from the same renderer.
        import struct
        manifest = {'schema': 'bootbridge-oem-slot-manifest-v1',
            'expected_ubi_sha256': '7' * 64, 'geometry': {'physical_offset': 0x900000,
            'size_bytes': 41943040, 'erase_bytes': 131072, 'page_bytes': 2048,
            'oob_bytes': 64, 'ecc_bits': 4}, 'candidate_sha256': '1' * 64,
            'volume_table_sha256': '2' * 64,
            'immutable': [{'name': name, 'size_bytes': 1, 'sha256': str(i) * 64, 'type': kind}
                          for name, i, kind in zip(gen.NAMES[:4], (3, 4, 5, 6), (2, 2, 2, 1))]}
        for identity in build.OFFICIAL_SOURCES.values():
            entries = build.oem_pool_entries([copy.deepcopy(manifest)], identity)
            with self.subTest(model=identity['model']), tempfile.TemporaryDirectory() as temp:
                d = Path(temp)
                (d / 'rule.h').write_text(gen.c_pool_header(entries))
                (d / 'main.c').write_text('#include <stdio.h>\n#include "rule.h"\n'
                    'int main(void){return fwrite(BI_OEM_POOL,sizeof(BI_OEM_POOL[0]),1,stdout)!=1;}\n')
                subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-I', str(ROOT / 'src'), str(d / 'main.c'), '-o', str(d / 'test')], check=True)
                actual = subprocess.check_output([str(d / 'test')])
                encoded = struct.pack('<QQIIII', 0x900000, 41943040, 131072, 2048, 64, 4)
                encoded += bytes.fromhex('1' * 64 + '2' * 64)
                for v in manifest['immutable']:
                    encoded += struct.pack('<Q', v['size_bytes']) + bytes.fromhex(v['sha256'])
                    encoded += struct.pack('<I', v['type']) + bytes(4)
                e, = entries
                encoded += struct.pack('<IIIII', *(e[k] for k in
                    ('content_group', 'exit_policy', 'product', 'alias', 'table_policy'))) + bytes(4)
                self.assertEqual(len(actual), 312)
                self.assertEqual(actual, encoded)


if __name__ == '__main__':
    unittest.main()
