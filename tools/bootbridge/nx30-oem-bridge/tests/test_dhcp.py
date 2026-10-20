#!/usr/bin/env python3
"""Socket-free fixtures for the RAM-only bridge DHCP service."""
import pathlib
import socket
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "bridge_dhcp.c"


def packet(message_type, requested=None, server=None, *, cookie=b"\x63\x82\x53\x63", extra=()):
    p = bytearray(240)
    p[0:3] = bytes((1, 1, 6))
    p[4:8] = b"\x12\x34\x56\x78"
    p[28:34] = b"\x02\x10\x20\x30\x40\x50"
    p[236:240] = cookie
    opts = bytearray((53, 1, message_type))
    if requested is not None:
        opts += bytes((50, 4)) + socket.inet_aton(requested)
    if server is not None:
        opts += bytes((54, 4)) + socket.inet_aton(server)
    for option in extra:
        opts += option
    opts.append(255)
    return bytes(p + opts)


def options(reply):
    found = {}
    i = 240
    while i < len(reply):
        code = reply[i]
        i += 1
        if code == 0:
            continue
        if code == 255:
            return found
        length = reply[i]
        i += 1
        found[code] = reply[i:i + length]
        i += length
    return found


class BridgeDhcpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="bridge-dhcp-build-")
        cls.binary = pathlib.Path(cls.tmp.name) / "bridge-dhcp"
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", str(SOURCE), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def decode(self, request):
        with tempfile.TemporaryDirectory(prefix="bridge-dhcp-fixture-") as d:
            root = pathlib.Path(d)
            src, dst = root / "request.bin", root / "reply.bin"
            src.write_bytes(request)
            result = subprocess.run([str(self.binary), "--decode-test", str(src), str(dst)], capture_output=True)
            return result.returncode, dst.read_bytes()

    def test_selftest_runs_without_socket_service(self):
        result = subprocess.run([str(self.binary), "--selftest"], check=True, capture_output=True, text=True)
        self.assertIn("matching and retransmitted ACK", result.stdout)
        self.assertIn("expiry sweep", result.stdout)

    def test_offer_is_deterministic_and_has_no_gateway_or_dns(self):
        code, reply = self.decode(packet(1))
        self.assertEqual(code, 0)
        self.assertEqual(reply[:3], b"\x02\x01\x06")
        self.assertEqual(socket.inet_ntoa(reply[16:20]), "192.168.10.100")
        opts = options(reply)
        self.assertEqual(opts[53], b"\x02")
        self.assertEqual(socket.inet_ntoa(opts[54]), "192.168.10.1")
        self.assertEqual(struct.unpack("!I", opts[51])[0], 1800)
        self.assertEqual(socket.inet_ntoa(opts[1]), "255.255.255.0")
        self.assertNotIn(3, opts)
        self.assertNotIn(6, opts)

    def test_out_of_pool_request_gets_broadcast_nak(self):
        code, reply = self.decode(packet(3, requested="192.168.10.99"))
        self.assertEqual(code, 0)
        self.assertEqual(reply[16:20], bytes(4))
        self.assertEqual(options(reply)[53], b"\x06")

    def test_foreign_server_is_ignored(self):
        code, reply = self.decode(packet(3, requested="192.168.10.100", server="192.168.10.2"))
        self.assertEqual(code, 3)
        self.assertEqual(reply, b"")

    def test_malformed_cookie_and_conflicting_duplicate_type_are_ignored(self):
        code, reply = self.decode(packet(1, cookie=b"bad!"))
        self.assertEqual(code, 3)
        self.assertEqual(reply, b"")
        duplicate = bytes((53, 1, 3))
        code, reply = self.decode(packet(1, extra=(duplicate,)))
        self.assertEqual(code, 3)
        self.assertEqual(reply, b"")

    def test_production_arguments_are_pinned_and_bad_values_fail_before_socket(self):
        for args in (("-i", "eth1;bad"), ("-s", "192.168.10.2"), ("-s", "192.168.124.1")):
            result = subprocess.run([str(self.binary), *args], capture_output=True)
            self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
