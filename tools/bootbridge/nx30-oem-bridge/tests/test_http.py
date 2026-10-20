#!/usr/bin/env python3
"""Host-only HTTP and fixture tests for bridge_check."""
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
import hashlib
import io
import zipfile
from test_install import make_backup_fixture, make_oem_fixture, write_test_oem_manifest_header

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "bridge_check.c"
INSTALL = ROOT / "src" / "bridge_install.c"
WEB = ROOT / "web"


def write(root, relative, value):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding="utf-8")


def write_bytes(root, relative, value):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(value)


def fixture(root, *, machid="8040000", ecc=4, include_cmd=True, malicious=False, nor=False, duplicate=False, extra_names=False):
    quote = 'NX30 "quoted" \\ model\nline' if malicious else "Qualcomm IPQ50xx"
    args = f"console=ttyMSM0 machid={machid} nandspare=128 nandeccbits=4 cnss2.skip_radio_bmap=0x6"
    if duplicate:
        args += " machid=8040001 cnss2.skip_radio_bmap=4"
    write(root, "proc/cmdline", (args + "\n" if include_cmd else "console=ttyMSM0\n"))
    write(root, "proc/sys/kernel/osrelease", "4.4.60\n")
    write(root, "proc/meminfo", "MemTotal:        262144 kB\n")
    write(root, "proc/device-tree/model", quote)
    write(root, "proc/device-tree/compatible", "vendor,nx30\x00qcom,ipq5018\x00")
    entries = [
        (2, 0x40000, 0x100000, "0:BOOTCONFIG"),
        (3, 0x40000, 0x140000, "0:BOOTCONFIG1"),
        (8, 0x140000, 0x500000, "0:APPSBL_1"),
        (12, 0x140000, 0x640000, "0:APPSBL"),
        (13, 0x100000, 0x780000, "0:ART"),
        (15, 0x2800000, 0x900000, "rootfs_1"),
        (16, 0x2800000, 0x3100000, "rootfs"),
    ]
    if extra_names:
        entries += [(17, 0x100000, 0x5900000, "0:APPSBLENV"), (18, 0x100000, 0x5a00000, "rootfs_data"), (19, 0x140000, 0x5b00000, "0:APPSBL_PRIMARY")]
    lines = ["dev:    size   erasesize  name"]
    for idx, size, offset, name in entries:
        lines.append(f'mtd{idx}: {size:08x} 00020000 "{name}"')
        base = f"sys/class/mtd/mtd{idx}"
        write(root, f"{base}/name", name + "\n")
        write(root, f"{base}/type", "NOR\n" if nor and idx == 2 else "nand\n")
        write(root, f"{base}/writesize", "2048\n")
        write(root, f"{base}/erasesize", "131072\n")
        write(root, f"{base}/offset", f"{offset}\n")
        write(root, f"{base}/ecc_strength", f"{ecc}\n")
        write(root, f"{base}/ecc_failures", "0\n")
    write(root, "proc/mtd", "\n".join(lines) + "\n")


def complete_install_fixture(root, *, backup_fail=False):
    install_fixture(root, backup_fail=backup_fail)


def install_fixture(root, *, backup_fail=False):
    """Build a disposable 9 MiB synthetic NX30 INSTALL1 NAND root."""
    import hashlib
    import struct
    import zlib

    write(root, "etc/bridge/profile", "NX30-OEM-BRIDGE-INSTALL1-v2\n")
    write(root, "etc/bridge/rootfs-id", "NX30-OEM-BRIDGE-INSTALL1-ROOTFS-v2\n")
    write_bytes(root, "proc/device-tree/h3c,bridge-profile", b"NX30-D-BRIDGE-INSTALL1\0")
    write_bytes(root, "proc/device-tree/compatible", b"qcom,ipq5018-mp02.1\0")
    write(root, "proc/sys/kernel/osrelease", "4.4.60\n")
    write(root, "proc/sys/kernel/random/boot_id", "12345678-1234-1234-1234-123456789abc\n")
    write(root, "proc/cmdline", "console=ttyMSM0 nandspare=128 nandeccbits=4 cnss2.skip_radio_bmap=6\n")
    write(root, "proc/nand-probe-log",
          "nand: device found, Manufacturer ID: 0xef, Chip ID: 0xba\n"
          "nand: Winbond W25N01GWZEIG SPI NAND 1.8V 1G-BIT\n"
          "nand: 128 MiB, SLC, erase size: 128 KiB, page size: 2048, OOB size: 64\n")
    write(root, "sys/devices/system/qfprom/qfprom0/authenticate", "0\n")
    # Match the immutable synthetic OEM/boot oracle shared with test_install.
    payload = bytes((i * 29 + 17) & 0xff for i in range(0x140000))
    active = bytes((i * 11 + 3) & 0xff for i in range(0x140000))
    sbl_prefix = b"INSTALL1-SBL1-VALID\0"
    valid_sbl = sbl_prefix + bytes((i * 7 + 5) & 0xff for i in range(0x10000 - len(sbl_prefix)))
    sbl = valid_sbl + b"\xff" * (0x80000 - len(valid_sbl))
    write_bytes(root, "etc/bridge/uboot.bin", payload)
    write(root, "etc/bridge/test-payload-sha256", hashlib.sha256(payload).hexdigest() + "\n")
    write(root, "etc/bridge/test-allowed-appsbl-sha256", hashlib.sha256(active).hexdigest() + "\n")
    write(root, "etc/bridge/test-sbl-sha256", hashlib.sha256(valid_sbl).hexdigest() + "\n")
    if backup_fail:
        write(root, "fault", "backupfail\n")

    # U-Boot's active env includes the 4-byte CRC, so the CRC-covered data is
    # 0x3fffc bytes and the complete active env is exactly 0x40000 bytes.
    env = bytearray(b"\xff" * 0x3fffc)
    variables = b"productname=NX30\0machid=8040000\0\0"
    env[:len(variables)] = variables
    env_blob = struct.pack("<I", zlib.crc32(env) & 0xffffffff) + bytes(env)

    bc = bytearray(0x40000)
    struct.pack_into("<III", bc, 0, 0xa3a2a1a0, 2, 8)
    names = ("0:QSEE", "0:DEVCFG", "0:CDT", "0:APPSBL", "0:HLOS", "rootfs", "0:WIFIFW", "0:BTFW")
    for i, name in enumerate(names):
        pos = 12 + i * 20
        bc[pos:pos + len(name)] = name.encode("ascii")
        struct.pack_into("<I", bc, pos + 16, 1 if i in (3, 5) else 0)
    struct.pack_into("<I", bc, 332, 0xb3b2b1b0)

    parts = [
        (0x000000, 0x080000, "0:SBL1", sbl, 0x10000),
        (0x080000, 0x080000, "0:MIBIB", b"\xff" * 0x080000, None),
        (0x100000, 0x040000, "0:BOOTCONFIG", bytes(bc), None),
        (0x140000, 0x040000, "0:BOOTCONFIG1", bytes(bc), None),
        (0x180000, 0x300000, "0:TZ", b"\xff" * 0x300000, None),
        (0x480000, 0x080000, "0:APPSBLENV", env_blob + b"\xff" * (0x80000 - len(env_blob)), None),
        (0x500000, 0x140000, "0:APPSBL_1", b"I" * 0x140000, None),
        (0x640000, 0x140000, "0:APPSBL", active, None),
        (0x780000, 0x100000, "0:ART", b"A" * 0x100000, None),
        (0x880000, 0x080000, "0:CDT", b"\xff" * 0x080000, None),
    ]
    lines = ["dev:    size   erasesize  name"]
    for idx, (offset, size, name, data, valid_bytes) in enumerate(parts):
        lines.append(f'mtd{idx}: {size:08x} 00020000 "{name}"')
        base = f"sys/class/mtd/mtd{idx}"
        for attr, value in (("name", name), ("type", "nand"), ("offset", str(offset)),
                            ("size", str(size)), ("writesize", "2048"),
                            ("erasesize", "131072"), ("oobsize", "64"),
                            ("ecc_strength", "4"), ("dev", f"90:{idx}")):
            write(root, f"{base}/{attr}", value + "\n")
        if valid_bytes is not None:
            write(root, f"{base}/sbl_valid_bytes", str(valid_bytes) + "\n")
        write_bytes(root, f"dev/mtd{idx}", data)
    write(root, "proc/mtd", "\n".join(lines) + "\n")
    make_oem_fixture(root)
    make_backup_fixture(root)


class BridgeHTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="nx30-bridge-build-")
        cls.binary = pathlib.Path(cls.temp.name) / "bridge_check"
        catalog = pathlib.Path(os.environ.get("H3C_NAND_CATALOG_HEADER", ""))
        if not catalog.is_file():
            raise RuntimeError("set H3C_NAND_CATALOG_HEADER to the authoritative shared catalog header")
        include = pathlib.Path(cls.temp.name) / "catalog-include"
        include.mkdir()
        (include / "h3c_nand_catalog.h").write_bytes(catalog.read_bytes())
        write_test_oem_manifest_header(include)
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST", "-I", str(include), "-I", str(ROOT / "src"), str(SOURCE), str(INSTALL), str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"), str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"), str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_install_identity.c"), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory(prefix="nx30-fixture-")
        self.root = pathlib.Path(self.tempdir.name)
        self.port = self.free_port()
        self.proc = None

    def tearDown(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(timeout=2)
        if self.proc and self.proc.stderr:
            self.proc.stderr.close()
        self.tempdir.cleanup()

    @staticmethod
    def free_port():
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    def start(self):
        self.proc = subprocess.Popen([str(self.binary), "-r", str(self.root), "-b", "127.0.0.1", "-p", str(self.port), "-w", str(WEB)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        url = f"http://127.0.0.1:{self.port}/"
        for _ in range(750):
            if self.proc.poll() is not None:
                self.fail(f"server exited: {self.proc.stderr.read().decode(errors='replace')}")
            try:
                urllib.request.urlopen(url, timeout=.1).close()
                return url
            except (OSError, urllib.error.URLError):
                time.sleep(.02)
        self.fail("server did not start")

    def get_json(self, path="/api/check"):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}{path}", timeout=2) as r:
            return r.status, json.loads(r.read().decode("utf-8"))

    def test_complete_production_assets_and_head(self):
        fixture(self.root)
        self.start()
        for filename in ("index.html", "main.js", "style.css"):
            expected=(WEB / filename).read_bytes()
            url=f"http://127.0.0.1:{self.port}/{filename}"
            with self.subTest(filename=filename):
                with urllib.request.urlopen(url,timeout=2) as r:
                    self.assertEqual(int(r.headers["Content-Length"]),len(expected))
                    self.assertEqual(r.read(),expected)
                req=urllib.request.Request(url,method="HEAD")
                with urllib.request.urlopen(req,timeout=2) as r:
                    self.assertEqual(int(r.headers["Content-Length"]),len(expected))
                    self.assertEqual(r.read(),b"")

    def test_positive_observation_and_read_only_report(self):
        fixture(self.root, extra_names=True)
        self.start()
        status, report = self.get_json()
        self.assertEqual(status, 200)
        self.assertEqual(report["profile"], "H3C-BOOTBRIDGE-MULTICONFIG-V1")
        self.assertEqual(report["hardware_classification"], "UNKNOWN")
        self.assertFalse(report["install_available"])
        self.assertEqual(report["wizard"]["phase"], "blocked")
        checks = {c["name"]: c["status"] for c in report["checks"]}
        self.assertEqual(checks["machid"], "consistent")
        self.assertEqual(checks["nand_geometry_ecc4"], "consistent")
        self.assertEqual(checks["system"], "consistent")
        self.assertEqual(checks["APPSBL"], "consistent")
        self.assertEqual(checks["BOOTCONFIG"], "consistent")
        self.assertEqual(checks["ART"], "consistent")
        self.assertEqual(len(report["observed"]["partitions"]), 10)
        self.assertEqual(report["observed"]["kernel"], "4.4.60")
        self.assertEqual(report["observed"]["boot_parameters"]["skip_radio_bmap"], "0x6")
        self.assertEqual(report["observed"]["partitions"][0]["offset_bytes"], 0x100000)
        check_items = {c["name"]: c for c in report["checks"]}
        self.assertEqual(check_items["flash_type"]["detail"], "检测到 NAND")
        self.assertIn("仅用于离线核对", report["identity_limit"])
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/report.json") as r:
            self.assertEqual(json.loads(r.read()), report)
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}/api/check", method="HEAD")
        with urllib.request.urlopen(req) as r:
            self.assertEqual(r.status, 200)
            self.assertGreater(int(r.headers["Content-Length"]), 0)

    def test_mismatch_unknown_and_nor_are_never_install_ready(self):
        fixture(self.root, machid="8040001", ecc=8, nor=True)
        self.start()
        _, report = self.get_json()
        checks = {c["name"]: c["status"] for c in report["checks"]}
        self.assertEqual(checks["machid"], "mismatch")
        self.assertEqual(checks["flash_type"], "mismatch")
        self.assertEqual(checks["nand_geometry_ecc4"], "mismatch")
        self.assertFalse(report["install_available"])

    def test_missing_sources_report_unknown(self):
        fixture(self.root, include_cmd=False)
        (self.root / "proc/mtd").unlink()
        self.start()
        _, report = self.get_json()
        checks = {c["name"]: c["status"] for c in report["checks"]}
        self.assertEqual(checks["machid"], "unknown")
        self.assertEqual(checks["cnss2.skip_radio_bmap"], "unknown")
        self.assertEqual(checks["flash_type"], "unknown")
        self.assertEqual(checks["ART"], "unknown")

    def test_conflicting_duplicate_boot_parameters_are_unknown(self):
        fixture(self.root, duplicate=True)
        self.start()
        _, report = self.get_json()
        checks = {c["name"]: c["status"] for c in report["checks"]}
        self.assertEqual(checks["machid"], "unknown")
        self.assertEqual(checks["cnss2.skip_radio_bmap"], "unknown")
        self.assertEqual(report["observed"]["boot_parameters"]["machid"], "")
        self.assertEqual(report["observed"]["boot_parameters"]["skip_radio_bmap"], "")

    def test_non_utf8_observation_stays_valid_json(self):
        fixture(self.root)
        (self.root / "proc/device-tree/model").write_bytes(b"NX30-\xff\x80\n")
        self.start()
        _, report = self.get_json()
        self.assertEqual(report["observed"]["device_tree_model"], "NX30-\u00ff\u0080\n")

    def test_split_header_and_idle_client_timeout(self):
        fixture(self.root)
        self.start()
        with socket.create_connection(("127.0.0.1", self.port), timeout=2) as client:
            client.sendall(f"GET /api/check HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\n".encode())
            time.sleep(.05)
            client.sendall(b"\r\n")
            response = b""
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    break
                response += chunk
        self.assertTrue(response.startswith(b"HTTP/1.1 200 OK"))
        idle = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        start = time.monotonic()
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/api/check", timeout=3) as r:
            self.assertEqual(r.status, 200)
        idle.close()
        self.assertLess(time.monotonic() - start, 2.5)

    def test_malformed_and_overlong_request_lines(self):
        fixture(self.root)
        self.start()
        requests = ((b"GET  / HTTP/1.1\r\n\r\n", b"400 Bad Request"),
                    (b"GET /" + b"a" * 300 + b" HTTP/1.1\r\n\r\n", b"414 URI Too Long"))
        for payload, expected in requests:
            with socket.create_connection(("127.0.0.1", self.port), timeout=2) as client:
                client.sendall(payload)
                response = client.recv(512)
            self.assertIn(expected, response)

    def test_bind_address_is_limited_to_lan_or_loopback(self):
        result = subprocess.run([str(self.binary), "-r", str(self.root), "-b", "0.0.0.0", "-p", str(self.port)], capture_output=True)
        self.assertEqual(result.returncode, 2)

    def test_escaped_observations_and_http_policy(self):
        fixture(self.root, malicious=True)
        self.start()
        _, report = self.get_json()
        self.assertEqual(report["observed"]["device_tree_model"], 'NX30 "quoted" \\ model\nline')
        self.assertFalse(report["install_available"])
        for path in ("/../etc/passwd", "/%2e%2e/etc/passwd", "/dir\\file"):
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(f"http://127.0.0.1:{self.port}{path}")
            self.assertEqual(ctx.exception.code, 400)
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}/api/check", data=b"x", method="POST")
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            urllib.request.urlopen(req)
        self.assertEqual(ctx.exception.code, 405)
        with self.assertRaises(urllib.error.HTTPError) as ctx:
            urllib.request.urlopen(f"http://127.0.0.1:{self.port}/not-whitelisted")
        self.assertEqual(ctx.exception.code, 404)
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/") as r:
            page = r.read().decode()
        self.assertTrue(page)
        aliases = ("/mobile.asp", "/router_password_mobile.asp", "/index.asp", "/login.asp", "/device_firmware_upgrade.asp", "/device_fireware_progress.asp", "/do_cmd.asp")
        for alias in aliases:
            with urllib.request.urlopen(f"http://127.0.0.1:{self.port}{alias}") as r:
                self.assertEqual(r.status, 200)
                self.assertEqual(r.read().decode(), page)
            req = urllib.request.Request(f"http://127.0.0.1:{self.port}{alias}", method="HEAD")
            with urllib.request.urlopen(req) as r:
                self.assertEqual(r.status, 200)

    def test_install_status_endpoints_and_fixture_fail_closed(self):
        fixture(self.root, extra_names=True)
        self.start()
        status_code, status = self.get_json("/api/status")
        self.assertEqual(status_code, 200)
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/api/status", timeout=2) as response:
            self.assertEqual(response.headers.get("Cache-Control"), "no-store")
            self.assertIsNone(response.headers.get("Access-Control-Allow-Origin"))
        self.assertEqual(status["phase"], "blocked")
        self.assertFalse(status["install_ready"])
        self.assertTrue(status["token"])
        self.assertIsNone(status["source"], "source is null until an OEM source-route is uniquely matched")
        self.assertIsNone(status["nand_profile"], "fixture has no uniquely matched runtime NAND profile")
        _, combined = self.get_json("/api/check")
        self.assertEqual(combined["wizard"]["source"], status["source"])
        self.assertEqual(combined["wizard"]["nand_profile"], status["nand_profile"])
        checks = {c["name"]: c["status"] for c in combined["checks"]}
        self.assertEqual(checks["nand_geometry_ecc4"], "consistent")
        before = {p.relative_to(self.root).as_posix(): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        for path in ("/api/install", "/api/reboot", "/api/return-oem", "/api/reboot-oem"):
            req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}?token={status['token']}")
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(req)
            self.assertEqual(ctx.exception.code, 404)
        wrong_token=("0" if status["token"][0]!="0" else "1")+status["token"][1:]
        for path in ("/api/install", "/api/reboot", "/api/return-oem", "/api/reboot-oem"):
            for token in (status["token"],wrong_token):
                post = urllib.request.Request(
                    f"http://127.0.0.1:{self.port}{path}",
                    data=("token=" + token + ("&confirm=return-oem" if path == "/api/return-oem" else "")).encode(), method="POST",
                    headers={"Content-Type":"application/x-www-form-urlencoded", "Origin":f"http://127.0.0.1:{self.port}"})
                with self.assertRaises(urllib.error.HTTPError) as ctx:
                    urllib.request.urlopen(post)
                self.assertEqual(ctx.exception.code, 409)
        after = {p.relative_to(self.root).as_posix(): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_install_end_to_end_without_backup_replay_and_fixture_reboot(self):
        complete_install_fixture(self.root, backup_fail=True)
        self.start()
        status_code, status = self.get_json("/api/status")
        self.assertEqual(status_code, 200)
        self.assertEqual(status["phase"], "ready", status)
        self.assertTrue(status["install_ready"])
        self.assertFalse(status["backup"]["available"])
        token = status["token"]

        def post(path):
            req = urllib.request.Request(
                f"http://127.0.0.1:{self.port}{path}", data=("token=" + token).encode(), method="POST",
                headers={"Content-Type":"application/x-www-form-urlencoded",
                         "Origin":f"http://127.0.0.1:{self.port}"})
            return urllib.request.urlopen(req, timeout=5)

        with post("/api/install") as response:
            self.assertEqual(response.status, 202)
            self.assertTrue(json.loads(response.read())["accepted"])
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            _, status = self.get_json("/api/status")
            if status["phase"] in ("complete", "failed", "blocked"):
                break
            time.sleep(.05)
        self.assertEqual(status["phase"], "complete", status)
        self.assertTrue(status["may_have_modified"])
        self.assertTrue(status["can_reboot"])

        with self.assertRaises(urllib.error.HTTPError) as ctx:
            post("/api/install")
        self.assertEqual(ctx.exception.code, 409)
        with post("/api/reboot-oem") as response:
            self.assertEqual(response.status, 202)
            self.assertTrue(json.loads(response.read())["accepted"])
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            _, status = self.get_json("/api/status")
            if status["stage"] == "reboot_simulated":
                break
            time.sleep(.05)
        self.assertEqual(status["stage"], "reboot_simulated", status)

    def test_candidate_failure_refuses_install_but_allows_verified_oem_return(self):
        for failure in ("missing", "corrupt"):
            with self.subTest(failure=failure):
                if self.proc and self.proc.poll() is None:
                    self.proc.terminate()
                    self.proc.wait(timeout=2)
                    self.proc.stderr.close()
                self.proc = None
                self.tempdir.cleanup()
                self.tempdir = tempfile.TemporaryDirectory(prefix="nx30-fixture-")
                self.root = pathlib.Path(self.tempdir.name)
                self.port = self.free_port()
                complete_install_fixture(self.root)
                candidate = self.root / "etc/bridge/uboot.bin"
                if failure == "missing":
                    candidate.unlink()
                else:
                    data = bytearray(candidate.read_bytes())
                    data[0] ^= 0x80
                    candidate.write_bytes(data)
                self.start()
                _, status = self.get_json("/api/status")
                self.assertEqual(status["phase"], "ready", status)
                self.assertFalse(status["install_ready"], status)
                self.assertTrue(status["actions"]["return_oem"], status)
                self.assertFalse(status["actions"]["reboot_oem"], status)

                token = status["token"]
                protected = {name: hashlib.sha256((self.root / "dev" / name).read_bytes()).hexdigest()
                             for name in ("mtd0", "mtd5", "mtd6", "mtd7", "mtd8", "mtd15", "mtd16")}

                def post(path):
                    body = "token=" + token + ("&confirm=return-oem" if path == "/api/return-oem" else "")
                    req = urllib.request.Request(
                        f"http://127.0.0.1:{self.port}{path}",
                        data=body.encode(), method="POST",
                        headers={"Content-Type":"application/x-www-form-urlencoded",
                                 "Origin":f"http://127.0.0.1:{self.port}"})
                    return urllib.request.urlopen(req, timeout=5)

                with self.assertRaises(urllib.error.HTTPError) as refused:
                    post("/api/install")
                self.assertEqual(refused.exception.code, 409)
                with post("/api/return-oem") as response:
                    self.assertEqual(response.status, 202)
                    self.assertTrue(json.loads(response.read())["accepted"])

                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    _, final = self.get_json("/api/status")
                    if final["stage"] in ("oem_return_reboot_simulated", "oem_return_reboot_failed", "failed"):
                        break
                    time.sleep(.05)
                self.assertEqual(final["phase"], "complete", final)
                self.assertEqual(final["operation"], "return_oem")
                self.assertEqual(final["stage"], "oem_return_reboot_simulated", final)
                self.assertTrue(final["may_have_modified"])
                self.assertFalse(final["can_reboot"])
                self.assertFalse(final["actions"]["return_oem"])
                self.assertEqual(protected,
                                 {name: hashlib.sha256((self.root / "dev" / name).read_bytes()).hexdigest()
                                  for name in protected},
                                 "OEM return must change only the two BOOTCONFIG records")

    def test_backup_stream_is_limited_to_core_file(self):
        complete_install_fixture(self.root)
        self.start()
        _, status = self.get_json("/api/status")
        self.assertTrue(status["backup"]["available"])
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/backup.zip", timeout=20) as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(response.headers.get("Content-Disposition"), 'attachment; filename="bootbridge-recovery-backup.zip"')
            self.assertEqual(response.headers.get("Content-Type"), "application/zip")
            data = response.read()
        self.assertEqual(len(data), status["backup"]["size_bytes"])
        self.assertEqual(hashlib.sha256(data).hexdigest(), status["backup"]["sha256"])
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertIsNone(z.testzip())
            manifest = json.loads(z.read("manifest.json"))
            self.assertEqual(manifest["data_bytes"], 45 * 1024 * 1024)
            self.assertEqual(z.read("startup_000000.bin")[:16], b"INSTALL1-SBL1-VA")
        request=urllib.request.Request(f"http://127.0.0.1:{self.port}/backup.zip",method="HEAD")
        with urllib.request.urlopen(request,timeout=5) as response:
            self.assertEqual(int(response.headers["Content-Length"]),len(data))
            self.assertEqual(response.read(),b"")
        # Changes to a source after initial capture must never regenerate or
        # truncate the published ZIP while another client downloads it.
        (self.root / "dev/mtd17").write_bytes(b"X" * 0x600000)
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/backup.zip",timeout=20) as response:
            self.assertEqual(response.read(),data)
        install=urllib.request.Request(f"http://127.0.0.1:{self.port}/api/install",
            data=("token="+status["token"]).encode(),method="POST",
            headers={"Content-Type":"application/x-www-form-urlencoded","Origin":f"http://127.0.0.1:{self.port}"})
        with urllib.request.urlopen(install,timeout=5) as response:self.assertEqual(response.status,202)
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            _,after=self.get_json("/api/status")
            if after["phase"] in ("complete","failed","blocked"):break
            time.sleep(.05)
        self.assertEqual(after["phase"],"complete",after)
        self.assertEqual(after["backup"]["sha256"],status["backup"]["sha256"])
        # The installation worker must keep the initial pre-write recovery ZIP.
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/backup.zip",timeout=20) as response:
            self.assertEqual(response.read(),data)
        for path in ("/backup.bin", "/backup.zip/anything"):
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(f"http://127.0.0.1:{self.port}{path}",timeout=5)
            self.assertEqual(error.exception.code,404)

    def test_post_rejects_unknown_csrf_duplicate_or_bad_length_and_duplicate_tokens(self):
        fixture(self.root)
        self.start()
        host=f"127.0.0.1:{self.port}"
        cases = [
            (f"POST /unknown HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 8\r\n\r\ntoken=aa".encode(), 404),
            (f"POST /api/install HTTP/1.1\r\nHost: {host}\r\nOrigin: http://elsewhere\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 8\r\n\r\ntoken=aa".encode(), 403),
            (b"POST /api/install HTTP/1.1\r\nHost: attacker.example\r\nOrigin: http://attacker.example\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 8\r\n\r\ntoken=aa", 403),
            (b"POST /api/install HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://127.0.0.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 8\r\n\r\ntoken=aa", 403),
            (f"POST /api/install HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 8\r\nContent-Length: 8\r\n\r\ntoken=aa".encode(), 400),
            (f"POST /api/install HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\nContent-Type: application/x-www-form-urlencoded\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n".encode(), 400),
            (f"POST /api/install HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 11\r\n\r\ntoken=aa&x=1".encode(), 400),
            (f"POST /api/install HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 17\r\n\r\ntoken=aa&token=bb".encode(), 400),
            (f"POST /api/install HTTP/1.1\r\nHost: {host}\r\nOrigin: http://{host}\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 3\r\n\r\nx=1".encode(), 400),
        ]
        # Every mutation endpoint shares the same request gate; changing the
        # action must never bypass Origin/Host/form/duplicate-token validation.
        action_cases = []
        for raw, expected in cases:
            if b"POST /api/install " in raw:
                for action in (b"/api/reboot", b"/api/return-oem", b"/api/reboot-oem"):
                    action_cases.append((raw.replace(b"POST /api/install ", b"POST " + action + b" ", 1), expected))
        cases += action_cases
        for raw, expected in cases:
            with socket.create_connection(("127.0.0.1", self.port), timeout=2) as s:
                s.sendall(raw)
                response=b""
                while True:
                    part=s.recv(1024)
                    if not part:break
                    response+=part
            self.assertIn(f" {expected} ".encode(), response.split(b"\r\n",1)[0], response)
if __name__ == "__main__":
    unittest.main()
