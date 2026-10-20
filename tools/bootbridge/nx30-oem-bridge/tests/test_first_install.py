"""Pure production policy with the externally supplied, exact D release bytes."""
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import copy
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]

class FirstInstallTests(unittest.TestCase):
    def compile_harness(self, directory):
        binary = Path(directory) / 'first-install-test'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(PROJECT / 'src'), str(PROJECT / 'src/bridge_first_install.c'),
                        str(PROJECT / 'tests/first_install_harness.c'), '-o', str(binary)], check=True)
        return binary

    def test_generic_profiles_without_payload_fixtures(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = self.compile_harness(directory)
            subprocess.run([str(binary), '--generic-only'], check=True)

    def test_exact_release_and_rejected_combinations(self):
        name = os.environ.get('BOOTBRIDGE_D_5F_PAYLOAD')
        if not name:
            self.skipTest('set BOOTBRIDGE_D_5F_PAYLOAD to the exact padded canonical D candidate')
        canonical = Path(name).resolve(strict=True)
        with tempfile.TemporaryDirectory() as directory:
            binary = self.compile_harness(directory)
            subprocess.run([str(binary), str(canonical)], check=True)

    def test_public_trial_candidate_and_rejected_combinations(self):
        name = os.environ.get('BOOTBRIDGE_D_PUBLIC_PAYLOAD')
        if not name:
            self.skipTest('set BOOTBRIDGE_D_PUBLIC_PAYLOAD to the exact padded public D candidate')
        public = Path(name).resolve(strict=True)
        with tempfile.TemporaryDirectory() as directory:
            binary = self.compile_harness(directory)
            subprocess.run([str(binary), '--public', str(public)], check=True)

    def test_abc_public_candidates_select_only_under_their_compiled_board_class(self):
        archive_name = os.environ.get('BOOTBRIDGE_PUBLIC_ARCHIVE')
        if not archive_name:
            self.skipTest('set BOOTBRIDGE_PUBLIC_ARCHIVE to the exact reviewed public archive')
        import hashlib
        archive = Path(archive_name).resolve(strict=True)
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(),
                         'bcca16ce8060b6f09094c10d0af40f9e48343d62da8e08ffe6cadbca8725c732')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payloads = {}
            with tarfile.open(archive, 'r:gz') as tf:
                for board in 'ABC':
                    member = f'abcd-uboot-public-review-20261016/payloads/HW-{board}/appsbl.partition.bin'
                    payloads[board] = root / f'{board}.partition.bin'
                    payloads[board].write_bytes(tf.extractfile(member).read())
            binary = self.compile_harness(root)
            for board in 'ABC':
                subprocess.run([str(binary), '--board', board, str(payloads[board])],
                               check=True, capture_output=True, text=True)
                for wrong_board in 'ABCD':
                    if wrong_board == board:
                        continue
                    result = subprocess.run([str(binary), '--board', wrong_board,
                                             str(payloads[board])], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0,
                        f'{board} payload must not be selected under {wrong_board}')

    def test_build_record_never_accepts_a_self_declared_pass(self):
        name = os.environ.get('BOOTBRIDGE_D_5F_PAYLOAD')
        if not name:
            self.skipTest('exact canonical candidate required')
        payload_path = Path(name).resolve(strict=True)
        sys.path.insert(0, str(PROJECT / 'tools'))
        import build
        payload = payload_path.read_bytes()
        profile = ['h3c-oem-efba-128m-2k-sp64']
        report = dict(derivative_commit=build.D_FIRST_INSTALL_COMMIT,
                      source_sha256=build.D_FIRST_INSTALL_SOURCE_SHA, build_profile='release',
                      appsbl_load_profiles=profile, appsbl_storage_profiles=profile,
                      supported_nand_profile_ids=profile)
        self.assertTrue(build.finite_d_loader_record(payload, report))
        for field in report:
            bad = copy.deepcopy(report)
            bad[field] = ['other'] if isinstance(bad[field], list) else 'unreviewed'
            bad['install_qualification_available'] = True
            bad['boot_chain_qualification'] = 'PASS'
            self.assertFalse(build.finite_d_loader_record(payload, bad), field)
        self.assertFalse(build.finite_d_loader_record(payload[:-1] + b'X', report))
