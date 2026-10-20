#!/usr/bin/env python3
"""Host-only checks for the bridge PID 1 and runtime shell plan."""
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "runtime"
BOOT = RUNTIME / "boot.sh"
INIT_SOURCE = RUNTIME / "bridge_init.c"


class BridgeBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="nx30-bridge-init-")
        cls.init_binary = pathlib.Path(cls.tmp.name) / "bridge-init"
        subprocess.run(
            ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", str(INIT_SOURCE), "-o", str(cls.init_binary)],
            check=True,
        )
        helper = pathlib.Path(cls.tmp.name) / "guard-helper.c"
        helper.write_text('#define main bridge_original_main\n#include ' + repr(str(INIT_SOURCE)).replace("'", '"') + r'''
#undef main
int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "marker") == 0)
        return marker_is(argv[2], argv[3]) ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "compatible") == 0)
        return compatible_path_is_reviewed(argv[2]) ? 0 : 1;
    return 2;
}
''')
        cls.guard_binary = pathlib.Path(cls.tmp.name) / "guard-helper"
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                        str(helper), "-o", str(cls.guard_binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_shell_syntax(self):
        for script in (RUNTIME / "preinit", RUNTIME / "init", BOOT):
            subprocess.run(["sh", "-n", str(script)], check=True)

    def test_pid1_plan_only_prints_intended_operations(self):
        result = subprocess.run([str(self.init_binary), "--plan"], check=True, text=True, capture_output=True)
        plan = result.stdout
        self.assertIn("getpid()==1", plan)
        self.assertIn("mount -t proc", plan)
        self.assertIn("mount -t sysfs", plan)
        self.assertIn("/proc/device-tree or /sys/firmware/devicetree/base", plan)
        self.assertIn("tmpfs /dev", plan)
        self.assertIn("tmpfs /tmp", plan)
        self.assertIn("/etc/bridge/boot.sh once", plan)
        self.assertIn("/usr/sbin/bridge-dhcp -i eth1 -s 192.168.10.1 if present", plan)
        self.assertIn("/usr/sbin/bridge-check -b 192.168.10.1 -p 80 -w /www concurrently", plan)
        self.assertIn("waitpid(-1)", plan)

    def test_non_pid1_execution_refuses_before_mounting(self):
        result = subprocess.run([str(self.init_binary)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 125)
        self.assertIn("not PID 1", result.stderr)

    def test_boot_shell_plan_is_safe_and_module_order_is_explicit(self):
        result = subprocess.run(["sh", str(BOOT), "--plan"], check=True, text=True, capture_output=True)
        lines = result.stdout.splitlines()
        self.assertIn("require BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1", lines)
        self.assertIn("require BRIDGE_RUNTIME_MODE=RUNTIME_GATED", lines)
        self.assertLess(lines.index("insmod /lib/modules/4.4.60/qca-ssdk.ko"), lines.index("insmod /lib/modules/4.4.60/qca-nss-dp.ko"))
        self.assertLess(lines.index("insmod /lib/modules/4.4.60/qca-nss-dp.ko"), lines.index("insmod /lib/modules/4.4.60/qca-nss-drv.ko"))
        self.assertIn("require readable /lib/firmware/qca-nss0.bin (builder link to qca-nss0-retail.bin)", lines)
        self.assertIn("ifconfig eth0 down", lines)
        self.assertIn("ifconfig eth1 192.168.10.1 netmask 255.255.255.0 up", lines)
        self.assertNotIn("mount", result.stdout)
        self.assertNotIn("qca_ol", result.stdout)
        self.assertNotIn("bootconfig", result.stdout)
        self.assertNotIn("ath11k", result.stdout)

    def test_init_chain_has_no_oem_shell_or_serial_login(self):
        preinit = (RUNTIME / "preinit").read_text()
        init = (RUNTIME / "init").read_text()
        c_source = INIT_SOURCE.read_text()
        self.assertIn("exec /sbin/init", preinit)
        self.assertIn("exec /sbin/init", init)
        self.assertIn("getpid() != 1", c_source)
        self.assertIn("/proc/device-tree/h3c,bridge-profile", c_source)
        self.assertIn("/bin/sh", c_source)
        self.assertIn("/etc/bridge/boot.sh", c_source)
        self.assertIn("/usr/sbin/bridge-check", c_source)
        for forbidden in ("ttyS0", "rcS", "mount_root", "h3c_boot_early", "kmodloader", "mwcli"):
            self.assertNotIn(forbidden, preinit + init + c_source)

    def test_dt_marker_is_checked_after_both_ram_metadata_mounts(self):
        source = INIT_SOURCE.read_text()
        proc_mount = source.index('mount_ram("proc", "/proc"')
        sys_mount = source.index('mount_ram("sysfs", "/sys"')
        marker_check = source.index('marker_is("/proc/device-tree/h3c,bridge-profile"')
        dev_mount = source.index('mount_ram("tmpfs", "/dev"')
        self.assertLess(proc_mount, sys_mount)
        self.assertLess(sys_mount, marker_check)
        self.assertLess(marker_check, dev_mount)
        self.assertIn('marker_is("/sys/firmware/devicetree/base/h3c,bridge-profile"', source)

    def test_dhcp_and_web_start_only_after_boot_setup_returns(self):
        source = INIT_SOURCE.read_text()
        boot_start = source.index("pid_t boot = spawn(boot_argv)")
        boot_wait = source.index("waitpid(boot, &status, 0)")
        jobs = source.index("child_job jobs[]")
        supervisor = source.index("supervise(jobs,")
        self.assertLess(boot_start, boot_wait)
        self.assertLess(boot_wait, jobs)
        self.assertIn('{"bridge-dhcp", dhcp_argv,', source[jobs:supervisor])
        self.assertIn('{"bridge-check", web_argv,', source[jobs:supervisor])
        self.assertLess(jobs, supervisor)
        self.assertIn("waitpid(-1, &status, 0)", source)
        self.assertIn("if (getpid() == 1) stay_alive();", source)

    def test_boot_shell_requires_verified_pid1_and_image_markers(self):
        boot = BOOT.read_text()
        self.assertIn("BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1", boot)
        self.assertIn("H3C-BOOTBRIDGE-RUNTIME-v1", boot)
        self.assertIn("H3C-BOOTBRIDGE-ROOTFS-v1", boot)
        self.assertIn("for module in qca-ssdk qca-nss-dp qca-nss-drv", boot)
        self.assertNotIn("machid=", boot)

    def test_optional_dhcp_uses_standalone_lan_only_service(self):
        boot = BOOT.read_text()
        c_source = INIT_SOURCE.read_text()
        self.assertIn('"/usr/sbin/bridge-dhcp", "-i", "eth1", "-s", "192.168.10.1"', c_source)
        self.assertIn('access(dhcp_argv[0], X_OK) == 0', c_source)
        self.assertIn("DHCP child exited; static web LAN remains available", c_source)
        self.assertNotIn("bridge-dhcp", boot)
        self.assertNotIn("dnsmasq", boot)

    def test_diagnostic_runtime_nodes_and_ram_capacity_are_explicit(self):
        source = INIT_SOURCE.read_text()
        self.assertIn('"H3C-BOOTBRIDGE-RUNTIME-v1"', source)
        self.assertIn('"H3C-BOOTBRIDGE-ROOTFS-v1"', source)
        self.assertIn('"H3C-BOOTBRIDGE-MULTICONFIG-V1"', source)
        self.assertIn('"/sys/class/mtd/mtd%u/dev"', source)
        self.assertIn('maj == 0', source)
        self.assertIn("/dev/mtd%u", source)
        self.assertIn('size=64m', source)

    def test_actual_pid1_marker_reader_rejects_embedded_or_wrong_markers(self):
        expected = "H3C-BOOTBRIDGE-MULTICONFIG-V1"
        cases = [(expected.encode(), True), (expected.encode() + b"\0", True),
                 (expected.encode() + b"\n", True), (b"NX30-D-BRIDGE-INSTALL1\0", False),
                 (b"", False), (expected.encode() + b"\0extra", False),
                 (expected.encode() + b"\0\0", False), (expected.encode() + b"\0\n", False),
                 (b" " + expected.encode(), False), (expected.encode() + b"x" * 200, False),
                 (expected.encode() + b" " * 200 + b"evil", False)]
        path = pathlib.Path(self.tmp.name) / "marker"
        for data, accepted in cases:
            with self.subTest(data=data):
                path.write_bytes(data)
                p = subprocess.run([str(self.guard_binary), "marker", str(path), expected])
                self.assertEqual(p.returncode, 0 if accepted else 1)

    def test_actual_pid1_route_reader_does_not_classify_b_d_or_allow_other_21(self):
        path = pathlib.Path(self.tmp.name) / "compatible"
        for route, accepted in [(b"mp02.1", True), (b"mp03.1", True), (b"mp03.1-c2", True),
                                (b"db-mp03.3", False), (b"mp03.1-c3", False), (b"unknown", False)]:
            path.write_bytes(b"qcom,ipq5018-" + route + b"\0qcom,ipq5018\0")
            p = subprocess.run([str(self.guard_binary), "compatible", str(path)])
            self.assertEqual(p.returncode, 0 if accepted else 1)
        for data in [b"qcom,ipq5018-mp02.1", b"qcom,ipq5018-mp02.1\0",
                     b"qcom,ipq5018-mp02.1\0qcom,ipq5018\0hidden"]:
            path.write_bytes(data)
            p = subprocess.run([str(self.guard_binary), "compatible", str(path)])
            self.assertEqual(p.returncode, 1)


if __name__ == "__main__":
    unittest.main()
