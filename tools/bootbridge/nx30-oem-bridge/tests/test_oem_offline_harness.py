"""Offline-only verifier self-tests; all dispatcher records and roots are synthetic."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import subprocess
import unittest
from unittest.mock import patch
from contextlib import redirect_stdout

TOOL = Path(__file__).resolve().parents[1] / 'tools/check_oem_offline.py'
spec = importlib.util.spec_from_file_location('oem_offline_selftest', TOOL)
oem = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oem)
UBI = b'UBI#synthetic-test-payload'
FIT = b'synthetic-fit-structurally-mocked-in-unit-tests'
NEGATIVE = 'candidate-ubiformat-rc7-after-mock-target-corruption'


def metadata(data):
    return {'bytes': len(data), 'sha256': oem.sha256(data)}


def platform_row(negative=False):
    actions = ['UBIDETACH -f -p /dev/mtd15',
               'UBIFORMAT /dev/mtd16 -y -f /tmp/ubi-bridge-fixture.bin']
    if negative:
        actions.append('INJECTED_UBIFORMAT_FAILURE_AFTER_TARGET_CORRUPTION')
    actions += ['MTD -e /dev/mtd2 write - /dev/mtd2', 'MTD -e /dev/mtd3 write - /dev/mtd3']
    return {'fixture': NEGATIVE if negative else 'candidate', 'host_rc': 0, 'timed_out': False,
            'stdout': 'SANDBOX_DEPENDENCIES_RC=0\nOEM_FUNCTIONS_SOURCE_RC=1\nOEM_FUNCTIONS_EXPORTS_RC=0\nOEM_PLATFORM_SOURCE_RC=0\nOEM_PLATFORM_EXPORTS_RC=0\nPLATFORM_CHECK_RC=0\nPLATFORM_DO_RC=0\nROOTFS_PRIMARY=1\nLAYOUT=0\n',
            'stderr': '', 'actions': '\n'.join(actions) + '\n',
            'copied_oem_input_sha256': {name: oem.OEM_INPUT_SHA256[name] for name in oem.PLATFORM_FILES},
            'rootfs_primary_value': '1', 'layout_value': '0',
            'mock_ubiformat_rc': 7 if negative else 0,
            'mock_ubiformat_target': metadata(b'\0' + UBI[1:] if negative else UBI),
            'mock_mtd_writes': {'mtd2.bin': metadata(b'SYNTHETIC BOOTCONFIG0\n'.ljust(2048, b'\0')),
                                'mtd3.bin': metadata(b'SYNTHETIC BOOTCONFIG1\n'.ljust(2048, b'\0'))}}


def summary():
    return {'outer_checker': [{'fixture': name, 'host_rc': 0, 'timed_out': False,
                              'function_rc': rc, 'stdout': f'MW_APP_CheckFirmware rc={rc}\n', 'stderr': ''}
                             for name, rc in [('official', 0), ('candidate', 0),
                                              ('candidate-corrupt-checksum', -2), ('candidate-wrong-product', -2)]],
            'platform_cases': [platform_row(), platform_row(True)]}


def evaluate(record):
    with patch.object(oem, 'fdt_nodes', return_value={'/images/ubi-bridge-fixture': {'data': UBI}}):
        return oem.evaluate_summary(record, FIT)


class HarnessSummaryTests(unittest.TestCase):
    def test_source_false_tail_is_valid_when_required_functions_loaded(self):
        """R012's optional UCI condition returns 1 after defining its functions."""
        with tempfile.TemporaryDirectory(prefix='oem-source-tail-') as td:
            root = Path(td); library = root/'functions.sh'; absent = root/'uci.sh'
            library.write_text('append() { :; }\ninclude() { :; }\nconfig_get() { :; }\n'
                               f'[ -z "$IPKG_INSTROOT" -a -f "{absent}" ] && . "{absent}"\n')
            code = oem.checked_shell_source(str(library), 'OEM_FUNCTIONS', ('append','include','config_get'))
            code = code.replace('/qemu-arm /bin/busybox sh -n', '/bin/sh -n')
            result = subprocess.run(['/bin/sh', '-c', 'IPKG_INSTROOT=\n'+code+'echo READY\n'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, '')
            self.assertIn('OEM_FUNCTIONS_SOURCE_RC=1\nOEM_FUNCTIONS_EXPORTS_RC=0\nREADY\n', result.stdout)

    def test_source_syntax_missing_file_or_missing_export_fail_closed(self):
        with tempfile.TemporaryDirectory(prefix='oem-source-bad-') as td:
            root = Path(td)
            for name, text in [('syntax', 'append() {\n'), ('missing-export', 'other() { :; }\nfalse\n'), ('missing-file', None)]:
                with self.subTest(name=name):
                    library = root/name
                    if text is not None: library.write_text(text)
                    code = oem.checked_shell_source(str(library), 'OEM_FUNCTIONS', ('append',))
                    code = code.replace('/qemu-arm /bin/busybox sh -n', '/bin/sh -n')
                    result = subprocess.run(['/bin/sh', '-c', code+'echo READY\n'], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 96)
                    self.assertNotIn('READY', result.stdout)
                    self.assertNotIn('OEM_FUNCTIONS_EXPORTS_RC=0', result.stdout)
                    self.assertTrue(result.stderr)

    def test_actual_sandbox_copies_are_pinned_before_execution(self):
        with tempfile.TemporaryDirectory(prefix='oem-source-pins-') as td:
            root=Path(td); expected={}
            for name in oem.PLATFORM_FILES:
                data=('synthetic pinned '+name).encode(); target=root/name
                target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
                expected[name]=oem.sha256(data)
            with patch.dict(oem.OEM_INPUT_SHA256, expected):
                oem.verify_platform_inputs(root)
                (root/'lib/functions.sh').write_text('append() { :; }\nfalse\n')
                with self.assertRaisesRegex(ValueError, 'copied OEM input hash mismatch: lib/functions.sh'):
                    oem.verify_platform_inputs(root)

    def test_main_returns_nonzero_for_check_rejection_even_if_guest_exits_zero(self):
        """Reproduces the observed rc0/PLATFORM_CHECK_RC=1 false success."""
        with tempfile.TemporaryDirectory(prefix='oem-harness-main-') as td:
            root = Path(td); build = root/'build'; source = root/'oem'; output = root/'out'
            build.mkdir(); source.mkdir(); official = root/'official.bin'; official.write_bytes(b'official')
            def outer(*args, **kwargs):
                name = args[2]; rc = -2 if 'candidate-' in name else 0
                return {'fixture': name, 'host_rc': 0, 'stdout': f'MW_APP_CheckFirmware rc={rc}\n', 'stderr': '', 'timed_out': False}
            broken = {'host_rc': 0, 'stdout': 'PLATFORM_CHECK_RC=1\nROOTFS_PRIMARY=\nLAYOUT=\n',
                      'stderr': 'touch: not found\ncat: not found\n', 'timed_out': False,
                      'actions': '', 'mock_mtd_writes': {}, 'mock_ubiformat_target': None}
            argv = ['checker', '--build', str(build), '--oem-root', str(source), '--output', str(output), '--cc', 'synthetic-cc', '--official', str(official)]
            with patch.object(sys, 'argv', argv), patch.object(oem, 'verify_oem_inputs', return_value=[]), \
                    patch.object(oem.shutil, 'which', side_effect=lambda name: '/synthetic/'+name), \
                    patch.object(oem, 'choose_candidate', return_value=(build/'fit', FIT, build/'wrapped', b'wrapped')), \
                    patch.object(oem, 'compile_checker', return_value={'host_rc': 0}), \
                    patch.object(oem, 'setup_outer_root'), patch.object(oem, 'run_outer_case', side_effect=outer), \
                    patch.object(oem, 'run_platform_case', side_effect=lambda *a, **kw: dict(broken, mock_ubiformat_rc=kw['ubiformat_rc'])), \
                    patch.object(oem, 'corrupt_checksum', side_effect=lambda data:data), \
                    patch.object(oem, 'wrong_product', side_effect=lambda data:data), \
                    patch.object(oem, 'fdt_nodes', return_value={'/images/ubi-bridge-fixture': {'data': UBI}}), redirect_stdout(io.StringIO()):
                rc = oem.main()
            self.assertNotEqual(rc, 0)
            report = json.loads((output/'RESULT.json').read_text())
            self.assertEqual(report['status'], 'FAIL_OFFLINE_OEM_CHECKS')
            self.assertTrue(report['validation_errors'])

    def test_success_and_destructive_fail_open_are_distinct_verified_cases(self):
        record = summary(); self.assertTrue(evaluate(record))
        self.assertEqual(record['status'], 'PASS_OFFLINE_OEM_CHECKS')
        self.assertEqual(record['platform_cases'][1]['verification_status'], 'PASS_EXPECTED_OEM_FAIL_OPEN_REPRODUCED')

    def test_missing_wrong_or_duplicate_markers_and_dependencies_fail(self):
        for changes in [dict(stdout='PLATFORM_CHECK_RC=1\n'), dict(stdout=''),
                        dict(stdout=platform_row()['stdout']+'PLATFORM_DO_RC=0\n'),
                        dict(host_rc=127), dict(timed_out=True), dict(stderr='cat: not found\n')]:
            record = summary(); record['platform_cases'][0].update(changes)
            self.assertFalse(evaluate(record), changes)
        for marker in ('OEM_FUNCTIONS_SOURCE_RC=1', 'OEM_FUNCTIONS_EXPORTS_RC=0',
                       'OEM_PLATFORM_SOURCE_RC=0', 'OEM_PLATFORM_EXPORTS_RC=0'):
            for replacement in ('', marker+'\n'+marker):
                record=summary()
                record['platform_cases'][0]['stdout']=record['platform_cases'][0]['stdout'].replace(marker, replacement)
                self.assertFalse(evaluate(record), marker)

    def test_target_bytes_bc_writes_and_selector_are_verified(self):
        for key, value in [('actions', ''), ('mock_ubiformat_target', metadata(b'wrong')),
                           ('copied_oem_input_sha256', {}),
                           ('mock_mtd_writes', {}), ('stdout', platform_row()['stdout'].replace('ROOTFS_PRIMARY=1','ROOTFS_PRIMARY=0'))]:
            record = summary(); record['platform_cases'][0][key] = value
            self.assertFalse(evaluate(record), key)

    def test_negative_requires_actual_corruption_and_finalization(self):
        for key, value in [('mock_ubiformat_target', metadata(UBI)),
                           ('actions', platform_row(True)['actions'].replace('INJECTED_UBIFORMAT_FAILURE_AFTER_TARGET_CORRUPTION','')),
                           ('stdout', platform_row(True)['stdout'].replace('PLATFORM_DO_RC=0','PLATFORM_DO_RC=7'))]:
            record = summary(); record['platform_cases'][1][key] = value
            self.assertFalse(evaluate(record), key)

    def test_all_outer_cases_are_required_and_function_codes_checked(self):
        for key,value in [('function_rc', 0), ('host_rc', 1), ('timed_out', True)]:
            record = summary(); record['outer_checker'][2][key] = value
            self.assertFalse(evaluate(record), key)
        record = summary(); record['outer_checker'].pop(); self.assertFalse(evaluate(record))
        record = summary(); record['outer_checker'][0]['stdout'] += 'MW_APP_CheckFirmware rc=0\n'; self.assertFalse(evaluate(record))
        record = summary(); record['outer_checker'].append(copy.deepcopy(record['outer_checker'][0])); self.assertFalse(evaluate(record))

    def test_shell_dispatch_uses_explicit_qemu_not_host_binfmt(self):
        with tempfile.TemporaryDirectory(prefix='oem-shell-dispatch-') as td:
            root=Path(td); oem.install_qemu_dispatch(root)
            script=(root/'tmp/qemu-command-functions.sh').read_text()
            for applet in oem.APPLET_LINKS:
                self.assertIn(f'{applet}() {{ /qemu-arm /bin/busybox {applet} "$@"; }}', script)
            self.assertIn('dumpimage() { /qemu-arm /usr/bin/dumpimage "$@"; }', script)
            for tool in ('mtd','ubiformat','ubidetach','fw_printenv','reboot'):
                self.assertIn(f'{tool}() {{ /qemu-arm /bin/busybox sh /sbin/{tool} "$@"; }}', script)
            oem.mock_tools(root)
            for tool in ('mtd','ubiformat','ubidetach','fw_printenv','reboot'):
                self.assertIn('. /tmp/qemu-command-functions.sh', (root/'sbin'/tool).read_text())

if __name__ == '__main__':
    unittest.main()
