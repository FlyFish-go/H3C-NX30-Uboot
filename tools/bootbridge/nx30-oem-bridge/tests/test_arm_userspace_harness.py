"""Fail-closed output contracts for the bounded ARM userspace verifier."""
import importlib.util
from pathlib import Path
import shlex
import subprocess
import unittest

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('arm_userspace_harness', PROJECT/'tools/test_arm_userspace.py')
arm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(arm)


class ArmUserspaceHarnessTests(unittest.TestCase):
    def test_observed_rc_zero_empty_plan_and_missing_cat_is_rejected(self):
        observed = {'rc': 0, 'stdout': '', 'stderr': '/etc/bridge/boot.sh: line 9: cat: not found\n'}
        with self.assertRaisesRegex(ValueError, 'boot_plan'):
            arm.validate_cli_run('boot_plan', observed)

    def test_every_cli_checks_status_and_both_output_channels(self):
        expected = {
            'pid1_plan': (0, arm.EXPECTED_PID1_PLAN, ''),
            'pid1_host_guard': (125, '', arm.EXPECTED_PID1_GUARD),
            'dhcp_packets': (0, arm.EXPECTED_DHCP_SELFTEST, ''),
            'oem_busybox_shell_syntax': (0, '', ''),
            'boot_plan': (0, arm.EXPECTED_BOOT_PLAN, ''),
        }
        for label, (rc, stdout, stderr) in expected.items():
            valid = dict(rc=rc, stdout=stdout, stderr=stderr)
            arm.validate_cli_run(label, valid)
            for key, bad in [('rc', 99), ('stdout', stdout+'unexpected\n'),
                             ('stderr', stderr+'cat: not found\n')]:
                with self.subTest(label=label, key=key), self.assertRaises(ValueError):
                    arm.validate_cli_run(label, dict(valid, **{key: bad}))
            if stdout:
                with self.assertRaises(ValueError):
                    arm.validate_cli_run(label, dict(valid, stdout=''))
        with self.assertRaises(ValueError):
            arm.validate_cli_run('unknown', dict(rc=0, stdout='', stderr=''))

    def test_sourced_unmodified_boot_script_prints_complete_plan(self):
        # Use host cat solely for this host test; formal ARM execution retains
        # the exact QEMU dispatch in boot_plan_command(). No runtime path runs.
        command = arm.boot_plan_command()
        self.assertIn('/qemu-arm /bin/busybox cat "$@"', command[-1])
        code = command[-1].replace('/qemu-arm /bin/busybox cat', '/bin/cat')
        code = code.replace('/etc/bridge/boot.sh', shlex.quote(str(PROJECT/'runtime/boot.sh')))
        result = subprocess.run(['/bin/sh', '-c', code], capture_output=True, text=True, timeout=5)
        arm.validate_cli_run('boot_plan', dict(rc=result.returncode, stdout=result.stdout, stderr=result.stderr))


if __name__ == '__main__':
    unittest.main()
