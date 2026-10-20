"""Exercise the actual builder boundaries without firmware/device execution."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import build

class BuilderBoundaries(unittest.TestCase):
    def test_explicit_public_candidate_archive_selects_exact_board_payload(self):
        archive = Path(__file__).resolve().parents[3] / "artifacts/ABCD-UBOOT-SOURCE-AND-CANDIDATES-20261016.tar.gz"
        payload, descriptor = build.public_candidate_from_archive(archive, "B")
        self.assertEqual(descriptor["board"], "B")
        self.assertEqual(len(payload), build.APPSBL_BYTES)
        self.assertEqual(build.sha(payload), descriptor["files"]["appsbl.partition.bin"]["sha256"])
        self.assertEqual(build.sha(payload), "912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589")

    def test_public_candidate_archive_rejects_unknown_class_and_unpinned_archive(self):
        archive = Path(__file__).resolve().parents[3] / "artifacts/ABCD-UBOOT-SOURCE-AND-CANDIDATES-20261016.tar.gz"
        with self.assertRaisesRegex(ValueError, "explicitly A, B, C or D"):
            build.public_candidate_from_archive(archive, "RT3000")
        with self.assertRaisesRegex(ValueError, "candidate selection failed"):
            build.public_candidate_from_archive(archive.with_name("missing-candidates.tar.gz"), "A")

    def test_four_public_candidates_bind_explicit_class_route_and_source_bootchain(self):
        root = Path(__file__).resolve().parents[3]
        archive = root / "artifacts/ABCD-UBOOT-SOURCE-AND-CANDIDATES-20261016.tar.gz"
        original_root = root / "work/bootbridge-live-resume-20261015/release-real-inputs"
        expected = {
            "A": ("RC3000", "8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb",
                  "config@mp03.1", "58761eb63252eda1893350b4eee1bb56d13911e453ee0e4b3ec9aac99382a4b8"),
            "B": ("RT3000", "912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589",
                  "config@mp02.1", "097230865a47f5ff21b01fcfa7b219fac32a948cda115447f68631bbc7831092"),
            "C": ("RT3000", "57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e",
                  "config@mp03.1-c2", "097230865a47f5ff21b01fcfa7b219fac32a948cda115447f68631bbc7831092"),
            "D": ("NX30", "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362",
                  "config@mp02.1", "7f1f7bf0938aae5d6c5816d6816524d5877eefb41e9fbdf4427b24bf34fb2b48"),
        }
        for board, (product, payload_sha, route, appsbl_sha) in expected.items():
            payload, descriptor = build.public_candidate_from_archive(archive, board)
            self.assertEqual(build.sha(payload), payload_sha)
            source = (original_root / product / "original.bin").read_bytes()
            identity = build.identify_official(source)
            components = build.official_bootchain_components(source, identity)
            self.assertEqual(components["appsbl_padded_sha256"], appsbl_sha)
            self.assertEqual(components["sbl_valid_bytes"], 178176)
            self.assertEqual(components["sbl_extent_sha256"],
                "842f8853ec46e3f69c7ce4528001117b9c2c78864de254dc99dc617f59ea5e21")
            header = build.generated_first_install_profile_header(board, identity, components)
            self.assertIn(f'#define BI_HARDWARE_CLASS "{board}"', header)
            self.assertIn(f'#define BI_FIRST_INSTALL_ROUTE "{route}"', header)
            self.assertIn(f'#define BI_EXPECTED_PRODUCT "{product}"', header)
            self.assertIn(f'#define BI_EXPECTED_OEM_APPSBL_SHA256 "{appsbl_sha}"', header)
            self.assertIn("#define BI_FIRST_INSTALL_OEM_EXIT_POLICY 2u", header)
            self.assertIn("#define BI_FIRST_INSTALL_PROFILE_COUNT 4u", header)
            self.assertIn("bootbridge_first_install_profiles[BI_FIRST_INSTALL_PROFILE_COUNT]", header)
            self.assertIn(f'"hw-{board.lower()}-public-profile-app1-root0-v2"', header)
            self.assertIn(f'"hw-{board.lower()}-public-profile-app0-root1-v2"', header)
            self.assertIn('0x900000u, 0x2800000u, 0x3100000u, 1u, 0x2800000u', header)
            self.assertIn('0x3100000u, 0x2800000u, 0x900000u, 0u, 0x2800000u', header)
            self.assertIn("NULL, 0u", header)
            if board == "B":
                self.assertIn('128u, 128u, 8u, 2048u, 131072u', header)

    def test_profile_hardware_class_is_independent_from_official_product(self):
        root = Path(__file__).resolve().parents[3]
        official = (root / "work/bootbridge-live-resume-20261015/release-real-inputs/RW3000/original.bin").read_bytes()
        identity = build.identify_official(official)
        components = build.official_bootchain_components(official, identity)
        # The low-level profile generator keeps hardware class and official
        # product independent; the release runner separately limits pairings
        # to the evidence-backed matrix.
        header = build.generated_first_install_profile_header("C", identity, components)
        self.assertIn('#define BI_HARDWARE_CLASS "C"', header)
        self.assertIn('#define BI_EXPECTED_PRODUCT "RW3000"', header)
        self.assertIn('#define BI_FIRST_INSTALL_ROUTE "config@mp03.1-c2"', header)
        self.assertNotIn("BI_EXPECTED_PRODUCT \"RT3000\"", header)

    def test_public_payload_header_has_actual_oem_baseline_and_candidate_sequence(self):
        root=Path(__file__).resolve().parents[3]
        archive=root/'artifacts/ABCD-UBOOT-SOURCE-AND-CANDIDATES-20261016.tar.gz'
        payload,descriptor=build.public_candidate_from_archive(archive,'B')
        official=(root/'work/bootbridge-live-resume-20261015/release-real-inputs/RT3000/original.bin').read_bytes()
        identity=build.identify_official(official);components=build.official_bootchain_components(official,identity)
        profile=dict(build.PUBLIC_BOARD_INPUTS['B'], **{'class':'B','product_name':identity['model'],
            'source_sha256':identity['sha256'],'appsbl_sha256':components['appsbl_padded_sha256']})
        catalog={'catalog_sha256':'a'*64,'supported':[profile['nand_profile_id']],
            'load':[profile['nand_profile_id']],'storage':[profile['nand_profile_id']]}
        header=build.generated_public_payload_header(build.PROJECT/'src/bridge_payload.h',payload,
            descriptor,catalog,components,profile)
        scope='bb-hw-b-'+identity['sha256'][:12]+'-trial-v1'
        self.assertIn(f'#define BI_VERSION_SCOPE "{scope}"',header)
        self.assertIn(f'"{components["appsbl_padded_sha256"]}", "{scope}", UINT64_C(1310720), UINT64_C(0), BV_OEM_BASELINE',header)
        self.assertIn(f'"{build.sha(payload)}", "{scope}", UINT64_C(1310720), UINT64_C(1), BV_CUSTOM_RELEASE',header)
        self.assertIn('#define BI_VERSION_REGISTRY_COUNT 2u',header)

    def test_public_pool_emits_exact_raw_and_autoresize_policy_for_selected_geometry(self):
        identity = next(v for v in build.OFFICIAL_SOURCES.values() if v["model"] == "RT3000")
        seen = []
        def manifest(path, *, expected_ubi_sha256, candidate_sha256, observed_geometry):
            seen.append(dict(observed_geometry))
            return {"schema":"bootbridge-oem-slot-manifest-v1", "expected_ubi_sha256":expected_ubi_sha256,
                "candidate_sha256":candidate_sha256, "volume_table_sha256":"2"*64,
                "geometry":dict(observed_geometry), "immutable":[
                    {"name":name,"size_bytes":4096,"sha256":str(i+3)*64,"type":2}
                    for i,name in enumerate(("kernel","wifi_fw","bt_fw","ubi_rootfs"))]}
        row = bytearray(172)
        struct.pack_into(">III", row, 0, 1, 1, 0)
        row[12] = 1
        struct.pack_into(">H", row, 14, len(b"rootfs_data"))
        row[16:16+len(b"rootfs_data")] = b"rootfs_data"
        row[144] = 1
        struct.pack_into(">I", row, 168, build.vendor_ubi.crc(row[:168]))
        raw_table = bytearray(128 * 172)
        raw_table[4*172:5*172] = row
        for index in range(128):
            offset = index * 172
            struct.pack_into(">I", raw_table, offset + 168,
                             build.vendor_ubi.crc(raw_table[offset:offset+168]))
        raw_table = bytes(raw_table)
        table_meta = {"volume_table_raw_sha256": build.sha(raw_table)}
        parsed_tables = [{"raw": raw_table}, {"raw": raw_table}]
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); include=root/'include';include.mkdir()
            with mock.patch.object(build.oem_manifest,'build_manifest',side_effect=manifest), \
                    mock.patch.object(build.vendor_ubi,'parse',return_value=(table_meta,[],[],parsed_tables)):
                report=build.generate_oem_header(root,include,b'ubi','1'*64,identity,
                    exit_policy_override=2,selected_geometry=build.PUBLIC_BOARD_INPUTS['B'])
            self.assertEqual(report['count'],4)
            self.assertEqual(report['base_count'],2)
            self.assertEqual({(g['physical_offset'],g['oob_bytes'],g['ecc_bits']) for g in seen},
                {(0x900000,128,8),(0x3100000,128,8)})
            generated=(include/'bridge_oem_manifest.h').read_text()
            self.assertEqual(generated.count(',30015u,2u,3u,30015u,0u}'),2)
            self.assertEqual(generated.count(',30015u,2u,3u,40015u,2u}'),2)
            self.assertIn('128u,8u', generated)
            self.assertEqual(report['exit_policy'],2)
            self.assertEqual(report['source_volume_table_sha256'],build.sha(raw_table))

    def test_public_pool_rejects_noncanonical_source_autoresize_row(self):
        raw_table = bytearray(128 * 172)
        row_offset = 4 * 172
        row = raw_table[row_offset:row_offset+172]
        struct.pack_into(">III", row, 0, 2, 1, 0)
        row[12] = 1
        struct.pack_into(">H", row, 14, len(b"rootfs_data"))
        row[16:16+len(b"rootfs_data")] = b"rootfs_data"
        row[144] = 1
        struct.pack_into(">I", row, 168, build.vendor_ubi.crc(row[:168]))
        raw_table[row_offset:row_offset+172] = row
        for index in range(128):
            offset = index * 172
            struct.pack_into(">I", raw_table, offset + 168,
                             build.vendor_ubi.crc(raw_table[offset:offset+168]))
        raw_table = bytes(raw_table)
        meta = {"volume_table_raw_sha256":build.sha(raw_table)}
        tables = [{"raw":raw_table},{"raw":raw_table}]
        with mock.patch.object(build.vendor_ubi,'parse',return_value=(meta,[],[],tables)):
            with self.assertRaisesRegex(ValueError,"exact rootfs_data reserve=1/autoresize=1"):
                build.public_autoresize_source_table(Path("unused"))

    def test_unknown_package_and_mutable_input_rejected(self):
        for raw in (b'H3C_+k'+b'\0'*200, bytearray(200), b'RX3000'):
            with self.assertRaises(ValueError): build.identify_official(raw)
        self.assertEqual({v['model'] for v in build.OFFICIAL_SOURCES.values()},
                         {'NX30','RC3000','RT3000','RW3000'})

    def test_source_bound_regular_copy_and_cross_version_rejection(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); src=root/'source'; out=root/'out'
            (src/'lib').mkdir(parents=True);out.mkdir()
            (src/'lib/module.ko').write_bytes(b'model one')
            # Host extraction permissions are untrusted; the SquashFS inode is
            # the authority for the staged mode.
            (src/'lib/module.ko').chmod(0o600)
            def source_run(args, **kwargs):
                if args[1] == '-lls':
                    return subprocess.CompletedProcess(args,0,
                        '-rwxr-xr-x root/root 9 2025-09-12 14:48 squashfs-root/lib/module.ko\n')
                return subprocess.CompletedProcess(args,0,b'model one')
            with mock.patch.object(build.subprocess, 'run', side_effect=source_run):
                build.copy_stock_bound(src,out,root/'stock.squashfs','lib/module.ko')
            self.assertEqual((out/'lib/module.ko').read_bytes(),b'model one')
            self.assertEqual((out/'lib/module.ko').stat().st_mode & 0o777,0o755)
            def mismatch_run(args, **kwargs):
                if args[1] == '-lls':
                    return subprocess.CompletedProcess(args,0,
                        '-rwxr-xr-x root/root 9 2025-09-12 14:48 squashfs-root/lib/module.ko\n')
                return subprocess.CompletedProcess(args,0,b'model two')
            with mock.patch.object(build.subprocess,'run',side_effect=mismatch_run):
                with self.assertRaisesRegex(ValueError,'does not match'):
                    build.copy_stock_bound(src,out,root/'stock.squashfs','lib/module.ko')

    def test_stock_link_target_binding_and_escape_rejection(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);src=root/'source';out=root/'out'
            (src/'lib').mkdir(parents=True);out.mkdir()
            (src/'lib/libc.so').write_bytes(b'ELF fixture')
            (src/'lib/ld.so').symlink_to('libc.so')
            def listing(target):return subprocess.CompletedProcess([],0,'lrwxrwxrwx root/root 7 2025-09-12 14:48 squashfs-root/lib/ld.so -> '+target+'\n')
            with mock.patch.object(build.subprocess,'run',return_value=listing('wrong.so')):
                with self.assertRaisesRegex(ValueError,'differs'): build.copy_stock_bound(src,out,root/'s','lib/ld.so')
            with mock.patch.object(build.subprocess,'run',return_value=listing('libc.so')):
                build.copy_stock_bound(src,out,root/'s','lib/ld.so')
            self.assertEqual((out/'lib/ld.so').readlink(),Path('libc.so'))
            (src/'lib/evil.so').symlink_to('/etc/passwd')
            with self.assertRaisesRegex(ValueError,'escapes'): build.copy_stock_bound(src,out,root/'s','lib/evil.so')

    def test_non_executable_elf_interpreter_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); (root/'bin').mkdir(); (root/'lib').mkdir()
            executable=root/'bin/busybox'; executable.write_bytes(b'ELF fixture'); executable.chmod(0o755)
            (root/'lib/libc.so').write_bytes(b'ELF interpreter fixture')
            (root/'lib/libc.so').chmod(0o644)
            (root/'lib/ld-musl-arm.so.1').symlink_to('libc.so')
            def readelf(args, **kwargs):
                if args[1] == '-l':
                    return subprocess.CompletedProcess(args,0,
                        '      [Requesting program interpreter: /lib/ld-musl-arm.so.1]\n')
                return subprocess.CompletedProcess(args,0,'')
            with mock.patch.object(build.subprocess,'run',side_effect=readelf):
                with self.assertRaisesRegex(ValueError,'PT_INTERP is not executable'):
                    build.audit_elf_runtime(root,'bin/busybox',require_interpreter=True)

    def test_executable_interpreter_and_shared_library_need_no_own_interpreter(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td); (root/'bin').mkdir(); (root/'lib').mkdir()
            for rel in ['bin/busybox','lib/libc.so','lib/librpc.so']:
                (root/rel).write_bytes(b'ELF fixture'); (root/rel).chmod(0o755)
            (root/'lib/ld-musl-arm.so.1').symlink_to('libc.so')
            def readelf(args, **kwargs):
                if args[1] == '-l' and Path(args[2]).name == 'busybox':
                    text='[Requesting program interpreter: /lib/ld-musl-arm.so.1]'
                elif args[1] == '-d' and Path(args[2]).name == 'busybox':
                    text='(NEEDED) Shared library: [librpc.so]'
                else: text=''
                return subprocess.CompletedProcess(args,0,text)
            with mock.patch.object(build.subprocess,'run',side_effect=readelf):
                build.audit_elf_runtime(root,'bin/busybox',require_interpreter=True)

    def test_oem_rules_are_derived_from_internal_padded_input(self):
        seen=[]
        def manifest(path,**kw):
            self.assertEqual(path.read_bytes(),b'ubi'+b'\xff'*(41943040-3))
            self.assertEqual(kw['expected_ubi_sha256'],build.sha(path.read_bytes()))
            seen.append(kw['observed_geometry']);return kw
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);inc=root/'include';inc.mkdir()
            with mock.patch.object(build.oem_manifest,'build_manifest',side_effect=manifest), mock.patch.object(build.oem_manifest,'c_pool_header',return_value='header') as header:
                report=build.generate_oem_header(root,inc,b'ubi','1'*64,build.OFFICIAL_SOURCES[build.SOURCE_SHA])
            self.assertEqual(report['count'],6)
            self.assertEqual(len({tuple(sorted(g.items())) for g in seen}),6)
            self.assertEqual((inc/'bridge_oem_manifest.h').read_text(),'header')
            self.assertEqual(header.call_count,1)
            entries=header.call_args.args[0]
            self.assertEqual(len(entries),6)
            self.assertTrue(all(e['exit_policy']==1 and e['product']==1 and e['table_policy']==0 for e in entries))
            self.assertEqual(report['source_package_sha256'],build.SOURCE_SHA)
            self.assertIn('multi-version',report['older_oem_versions'])

if __name__=='__main__':unittest.main()
