"""Compiled C-runtime reconstruction tests for source-bound candidate patches."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
APPSBL = 0x140000


def c_bytes(data):
    return "{" + (",".join(str(value) for value in data) if data else "0") + "}"


def patch_header(base, target, ranges, patch_data=None, base_sha=None):
    if patch_data is None:
        patch_data = b"".join(target[start:start + size] for start, size in ranges)
    lines = [
        "#include <stdint.h>",
        "struct bi_payload_patch { uint32_t offset, bytes, data_offset; };",
        f'#define BI_PAYLOAD_BASE_SHA256 "{base_sha or hashlib.sha256(base).hexdigest()}"',
        f"#define BI_PAYLOAD_BASE_BYTES {len(base)}u",
        f'#define BI_PAYLOAD_SHA256 "{hashlib.sha256(target).hexdigest()}"',
        f"#define BI_PAYLOAD_BYTES {len(target)}u",
        "static const struct bi_payload_patch bi_payload_patches[] = {",
    ]
    data_offset = 0
    for start, size in ranges:
        lines.append(f"  {{{start}u,{size}u,{data_offset}u}},")
        data_offset += size
    if not ranges:
        lines.append("  {0u,0u,0u},")
    lines += [
        "};",
        f"#define BI_PAYLOAD_PATCHES bi_payload_patches",
        f"#define BI_PAYLOAD_PATCH_COUNT {len(ranges)}u",
        f"#define BI_PAYLOAD_PATCH_DATA_BYTES {len(patch_data)}u",
        f"static const uint8_t bi_payload_patch_data[] = {c_bytes(patch_data)};",
        "#define BI_PAYLOAD_PATCH_DATA bi_payload_patch_data",
    ]
    return "\n".join(lines) + "\n"


class PayloadPatchRuntimeTests(unittest.TestCase):
    def test_compiled_patch_reconstructs_exact_image_and_rejects_bad_inputs(self):
        cc = shutil.which("cc")
        catalog = Path(os.environ.get("H3C_NAND_CATALOG_HEADER", ""))
        if not cc or not catalog.is_file():
            self.skipTest("host C compiler or authoritative NAND catalog header unavailable")
        base = bytes((i * 29 + 17) & 255 for i in range(APPSBL))
        target = bytearray(base)
        target[37:42] = b"PATCH"
        target[0x3000:0x3004] = b"OEM!"
        target = bytes(target)
        ranges = [(37, 5), (0x3000, 4)]

        with tempfile.TemporaryDirectory(prefix="bridge-payload-patch-") as td:
            root = Path(td)
            (root / "catalog").mkdir()
            shutil.copyfile(catalog, root / "catalog/h3c_nand_catalog.h")
            (root / "base.bin").write_bytes(base)

            def compile_case(name, header_text):
                inc = root / name
                inc.mkdir()
                (inc / "bridge_payload_patch.h").write_text(header_text)
                exe = root / (name + ".bin")
                sources = [PROJECT / "src/bridge_install.c", PROJECT / "src/bridge_version.c",
                           PROJECT / "src/bridge_oem_slot.c", PROJECT / "src/bridge_backup.c",
                           PROJECT / "src/bridge_root_binding.c", PROJECT / "src/bridge_first_install.c",
                           PROJECT / "src/bridge_install_identity.c"]
                command = [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                           "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_PAYLOAD_PATCH_TEST",
                           "-DBI_PAYLOAD_PATCH_HEADER=\"bridge_payload_patch.h\"",
                           "-I", str(inc), "-I", str(root / "catalog"), "-I", str(PROJECT / "src"),
                           str(PROJECT / "tests/payload_patch_harness.c"), *map(str, sources), "-o", str(exe)]
                subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
                return exe

            good = compile_case("good", patch_header(base, target, ranges))
            result = subprocess.run([str(good), str(root / "base.bin")], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, target)

            bad_base = bytearray(base)
            bad_base[10] ^= 1
            (root / "bad-base.bin").write_bytes(bad_base)
            result = subprocess.run([str(good), str(root / "bad-base.bin")], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 5)

            bad_data = b"WRONG" + target[0x3000:0x3004]
            wrong_final = compile_case("wrong-final", patch_header(base, target, ranges, bad_data))
            result = subprocess.run([str(wrong_final), str(root / "base.bin")], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 5)

            bad_bounds = compile_case("bad-bounds", patch_header(base, target,
                                          [(APPSBL - 1, 2)]))
            result = subprocess.run([str(bad_bounds), str(root / "base.bin")], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 5)

            zero_patch = compile_case("zero-patch", patch_header(base, base, []))
            result = subprocess.run([str(zero_patch), str(root / "base.bin")], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, base)

            bad_sentinel_header = patch_header(base, base, []).replace(
                "{0u,0u,0u}", "{1u,0u,0u}")
            bad_sentinel = compile_case("bad-zero-sentinel", bad_sentinel_header)
            result = subprocess.run([str(bad_sentinel), str(root / "base.bin")], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 5)

            stray_zero_data = compile_case("zero-count-nonzero-data",
                                           patch_header(base, base, [], patch_data=b"\x00"))
            result = subprocess.run([str(stray_zero_data), str(root / "base.bin")],
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 5)

            # Exercise the production read_payload caller as well: its runtime
            # file remains the common base, while the compiled registry yields
            # the exact candidate hash consumed by normal install qualification.
            sys.path.insert(0, str(PROJECT / "tests"))
            from test_install import make_fixture, write_test_oem_manifest_header
            read_inc = root / "read-payload"
            read_inc.mkdir()
            read_header = patch_header(base, target, ranges)
            (read_inc / "bridge_payload_patch.h").write_text(read_header)
            write_test_oem_manifest_header(read_inc, hashlib.sha256(target).hexdigest())
            read_exe = root / "read-payload.bin"
            sources = [PROJECT / "src/bridge_install.c", PROJECT / "src/bridge_version.c",
                       PROJECT / "src/bridge_oem_slot.c", PROJECT / "src/bridge_backup.c",
                       PROJECT / "src/bridge_root_binding.c", PROJECT / "src/bridge_first_install.c",
                       PROJECT / "src/bridge_install_identity.c"]
            command = [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                       "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_PAYLOAD_PATCH_TEST",
                       "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
                       "-DBI_PAYLOAD_PATCH_HEADER=\"bridge_payload_patch.h\"",
                       "-I", str(read_inc), "-I", str(root / "catalog"), "-I", str(PROJECT / "src"),
                       str(PROJECT / "tests/install_harness.c"), *map(str, sources), "-o", str(read_exe)]
            subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
            fixture = root / "fixture"
            make_fixture(fixture)
            (fixture / "etc/bridge/uboot.bin").write_bytes(base)
            state_run = subprocess.run([str(read_exe), str(fixture), "status"],
                                       check=True, capture_output=True, text=True, timeout=45)
            state = next(__import__("json").loads(line) for line in state_run.stdout.splitlines()
                         if line.startswith("{"))
            self.assertTrue(any(check["name"] == "candidate_payload" and check["status"] == "pass"
                                for check in state["checks"]), state.get("checks"))


if __name__ == "__main__":
    unittest.main()
