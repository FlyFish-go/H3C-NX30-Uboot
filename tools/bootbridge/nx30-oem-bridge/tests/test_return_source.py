"""Host checks for generated source expectations; no qualification is implied."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
TOOLS = PROJECT / "tools"
sys.path.insert(0, str(TOOLS))

import bridge_kernel
import build_return_source as return_source
import vendor_fit
import vendor_ubi


NX30_OFFICIAL_SHA256 = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"
OFFICIAL_ENV = {
    "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922": "BOOTBRIDGE_NX30_R012_OFFICIAL",
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": "BOOTBRIDGE_RC3000_R013L01_OFFICIAL",
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": "BOOTBRIDGE_RT3000_R015_OFFICIAL",
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": "BOOTBRIDGE_RW3000_R012_OFFICIAL",
}
NAND_FIXTURE = PROJECT / "tests/fixtures/return-nand-dt-nodes.json"


def official_kernel_fit(official_sha256=NX30_OFFICIAL_SHA256):
    env_name = OFFICIAL_ENV[official_sha256]
    path_value = os.environ.get(env_name)
    if not path_value:
        raise unittest.SkipTest(f"set {env_name} to the exact ordinary package")
    official_path = Path(path_value).expanduser()
    if not official_path.is_file():
        raise unittest.SkipTest(f"{env_name} does not name an available exact package")
    raw = official_path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != official_sha256:
        raise AssertionError("official input path does not contain the exact reviewed package")
    outer_size = struct.unpack_from(">I", raw, 164)[0]
    outer = vendor_fit.fdt_props(raw[160 : 160 + outer_size])
    ubi_images = [props["data"] for path, props in outer.items()
                  if path.startswith("/images/ubi-") and "data" in props]
    if len(ubi_images) != 1:
        raise AssertionError("official package does not have one reviewed UBI image")
    with tempfile.TemporaryDirectory(prefix="bootbridge-return-source-") as directory:
        ubi_path = Path(directory) / "official.ubi"
        ubi_path.write_bytes(ubi_images[0])
        _, volumes, _, _ = vendor_ubi.parse(ubi_path)
    return volumes["kernel"]


class ReturnSourceTests(unittest.TestCase):
    def test_return_rule_uses_linux_raw_spare_not_qpic_normalized_oob(self):
        official, product = next(iter(return_source.PRODUCTS.items()))
        row = dict(schema=1, official_sha256=official,
                   stock_kernel_sha256=bridge_kernel.SOURCES[official][1],
                   bridge_kernel_sha256='a' * 64, compressed_kernel_sha256='b' * 64,
                   nand_dt_sha256=return_source.NAND_SHA256, product=product,
                   route=return_source.ROUTE, compatible=return_source.COMPATIBLE)
        row['revision_sha256'] = return_source.sha(return_source.canonical(row))
        components = {official: dict(sbl_sha256='c' * 64, sbl_valid_bytes=1024,
                                     appsbl_padded_sha256='d' * 64, appsbl_valid_bytes=100000)}
        profile = dict(profile_id='fixture-128-spare', appsbl_source_mask=0x11,
                       appsbl_ecc4_source_mask=0x11, appsbl_ecc8_source_mask=0x11,
                       kernel_ecc_mode_mask=0x03, kernel_oem_oob_field_mask=0x03,
                       raw_spare_bytes=128, mtd_oob_ecc4_bytes=64,
                       mtd_oob_ecc8_bytes=80)
        contract = return_source.c_return_contract_header([row], components, [profile])
        self.assertIn('"fixture-128-spare",4u,64u', contract)
        self.assertIn('"fixture-128-spare",4u,128u', contract)
        self.assertIn('"fixture-128-spare",8u,64u', contract)
        self.assertIn('"fixture-128-spare",8u,128u', contract)

    def test_source_bound_registry_pins_each_source_and_all_three_routes(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('host C compiler is unavailable')
        profiles = json.loads((TOOLS / 'nand_profiles.generated.json').read_text())['profiles']
        for official, product in return_source.PRODUCTS.items():
            rows = []
            for route, (_, compatible, _) in bridge_kernel.ROUTES.items():
                row = dict(schema=1, official_sha256=official,
                           stock_kernel_sha256=bridge_kernel.SOURCES[official][1],
                           bridge_kernel_sha256='a' * 64,
                           compressed_kernel_sha256='b' * 64,
                           nand_dt_sha256=return_source.NAND_SHA256,
                           product=product, route=route, compatible=compatible)
                row['revision_sha256'] = return_source.sha(return_source.canonical(row))
                rows.append(row)
            components = {official: dict(sbl_sha256='c' * 64, sbl_valid_bytes=1024,
                                         appsbl_padded_sha256='d' * 64, appsbl_valid_bytes=100000)}
            header = return_source.c_registry_header(rows, components)
            self.assertIn('BI_RETURN_SOURCE_COUNT', header)
            self.assertEqual(header.count('config@mp02.1'), 1)
            self.assertEqual(header.count('qcom,ipq5018-mp02.1'), 1)
            with self.assertRaises(ValueError):
                return_source.c_registry_header(rows + [rows[0]], components)
            contract = return_source.c_return_contract_header(rows, components, profiles)
            self.assertIn('BR_RETURN_EXIT_SOURCE_BOOTIPQ_V1', contract)
            self.assertNotIn('HARDWARE_PASS', contract)
            provider_contract = return_source.c_return_contract_header(
                rows[:1], components, profiles, exit_policy=2)
            self.assertIn('BR_RETURN_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V2', provider_contract)
            self.assertNotIn('BR_RETURN_EXIT_SOURCE_BOOTIPQ_V1', provider_contract)
            with self.assertRaises(ValueError):
                return_source.c_return_contract_header(rows[:1], components, profiles, exit_policy=3)
            with tempfile.TemporaryDirectory(prefix='bootbridge-return-contract-') as directory:
                temp = Path(directory)
                shutil.copyfile(PROJECT / 'src/bridge_oem_return.h', temp / 'bridge_oem_return.h')
                (temp / 'bridge_oem_return_contract.h').write_text(contract)
                fixture = temp / 'contract.c'
                fixture.write_text('#include "bridge_oem_return_contract.h"\n'
                                   'int main(void){return BI_RETURN_SOURCE_COUNT==3 && BI_RETURN_RULE_COUNT>4 ? 0 : 1;}\n')
                exe = temp / 'contract'
                subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror',
                                '-I', str(temp), str(fixture), '-o', str(exe)],
                               check=True, capture_output=True, text=True, timeout=30)
                subprocess.run([str(exe)], check=True, timeout=10)

    def test_real_nx30_source_and_generated_header(self):
        stock = official_kernel_fit()
        bridge, _ = bridge_kernel.transform_kernel(stock, NX30_OFFICIAL_SHA256)
        metadata = return_source.metadata(stock, bridge, NX30_OFFICIAL_SHA256)
        self.assertEqual(metadata["official_sha256"], NX30_OFFICIAL_SHA256)
        self.assertEqual(metadata["stock_kernel_sha256"], bridge_kernel.SOURCES[NX30_OFFICIAL_SHA256][1])
        self.assertEqual(metadata["bridge_kernel_sha256"], hashlib.sha256(bridge).hexdigest())
        self.assertEqual(metadata["nand_dt_sha256"], return_source.NAND_SHA256)
        self.assertEqual(metadata["route"], "config@mp02.1")
        self.assertEqual(metadata["compatible"], "qcom,ipq5018-mp02.1")
        self.assertEqual(metadata["revision_sha256"], return_source.sha(
            return_source.canonical({k: v for k, v in metadata.items() if k != "revision_sha256"})))

        header = return_source.c_header(metadata)
        expected_macros = {
            "OFFICIAL_SHA256", "STOCK_KERNEL_SHA256", "BRIDGE_KERNEL_SHA256",
            "COMPRESSED_KERNEL_SHA256", "NAND_DT_SHA256", "REVISION_SHA256",
            "PRODUCT", "ROUTE", "COMPATIBLE",
        }
        actual_macros = {line.split("BI_RETURN_SOURCE_", 1)[1].split()[0]
                         for line in header.splitlines() if "#define BI_RETURN_SOURCE_" in line}
        self.assertEqual(actual_macros, expected_macros)
        self.assertIn("#define BI_RETURN_SOURCE_PRODUCT 1", header)

        cc = shutil.which("cc")
        if not cc:
            self.skipTest("host C compiler is unavailable")
        with tempfile.TemporaryDirectory(prefix="bootbridge-return-header-") as directory:
            temp = Path(directory)
            (temp / "bridge_oem_return_source.h").write_text(header)
            fixture = temp / "source_header_fixture.c"
            fixture.write_text(
                '#include BI_RETURN_SOURCE_HEADER\n'
                'static const char marker[] = "BB_RETURN_SOURCE_REVISION:" BI_RETURN_SOURCE_REVISION_SHA256;\n'
                'int main(void) { return BI_RETURN_SOURCE_PRODUCT == 1 && '
                'sizeof(marker) == sizeof("BB_RETURN_SOURCE_REVISION:") + 64 ? 0 : 1; }\n'
            )
            exe = temp / "source_header_fixture"
            subprocess.run(
                [cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                 '-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"',
                 "-I", str(temp), str(fixture), "-o", str(exe)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_explicit_routes_bind_actual_pinned_official_fit_routes(self):
        available = []
        for official_sha256, env_name in OFFICIAL_ENV.items():
            if os.environ.get(env_name):
                available.append((official_sha256, env_name))
        if not available:
            raise unittest.SkipTest("set one or more BOOTBRIDGE_*_OFFICIAL exact package paths")

        for official_sha256, env_name in available:
            with self.subTest(official=env_name):
                stock = official_kernel_fit(official_sha256)
                bridge, _ = bridge_kernel.transform_kernel(stock, official_sha256)
                for route, (fdt_name, compatible, _) in bridge_kernel.ROUTES.items():
                    with self.subTest(route=route):
                        metadata = return_source.metadata(stock, bridge, official_sha256, route=route)
                        self.assertEqual(metadata["route"], route)
                        self.assertEqual(metadata["compatible"], compatible)
                        self.assertEqual(metadata["nand_dt_sha256"], return_source.NAND_SHA256)
                        self.assertEqual(metadata["revision_sha256"], return_source.sha(
                            return_source.canonical({k: v for k, v in metadata.items()
                                                     if k != "revision_sha256"})))
                        stock_nodes = bridge_kernel._verify(stock)
                        config = stock_nodes["/configurations/" + route]
                        self.assertEqual(config["kernel"], b"kernel@1\0")
                        self.assertEqual(config["fdt"], fdt_name.encode("ascii") + b"\0")
                        stock_dt = vendor_fit.fdt_props(stock_nodes["/images/" + fdt_name]["data"])
                        self.assertEqual(stock_dt["/"]["compatible"],
                                         compatible.encode("ascii") + b"\0qcom,ipq5018\0")

        stock = official_kernel_fit(available[0][0])
        bridge, _ = bridge_kernel.transform_kernel(stock, available[0][0])
        with self.assertRaises(ValueError):
            return_source.metadata(stock, bridge, available[0][0], route="config@mp03.5-c2")
        wrong_nodes = bridge_kernel._verify(stock)
        wrong_nodes["/configurations/config@mp03.1"]["fdt"] = b"fdt@mp03.1-c2\0"
        with self.assertRaises(ValueError):
            return_source.route_data(wrong_nodes, "config@mp03.1")

    def test_source_metadata_rejects_unpinned_or_nontransformed_fit(self):
        stock = official_kernel_fit()
        bridge, _ = bridge_kernel.transform_kernel(stock, NX30_OFFICIAL_SHA256)
        with self.assertRaises(ValueError):
            return_source.metadata(stock, bridge, "0" * 64)
        with self.assertRaises(ValueError):
            return_source.metadata(stock, stock, NX30_OFFICIAL_SHA256)
        with self.assertRaises(ValueError):
            return_source.metadata(stock + b"x", bridge, NX30_OFFICIAL_SHA256)
        with self.assertRaises(ValueError):
            return_source.metadata(stock, bridge, NX30_OFFICIAL_SHA256, route="config@mp03.5-c2")

    def test_metadata_validation_rejects_tampering(self):
        stock = official_kernel_fit()
        bridge, _ = bridge_kernel.transform_kernel(stock, NX30_OFFICIAL_SHA256)
        metadata = return_source.metadata(stock, bridge, NX30_OFFICIAL_SHA256)
        for field, value in (("product", 4), ("compatible", "qcom,wrong"),
                             ("nand_dt_sha256", "0" * 64),
                             ("revision_sha256", "1" * 64)):
            changed = dict(metadata)
            changed[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                return_source.validate_metadata(changed)
        wrong_route = dict(metadata, route="config@mp03.5-c2")
        wrong_route["revision_sha256"] = return_source.sha(return_source.canonical(
            {k: v for k, v in wrong_route.items() if k != "revision_sha256"}))
        with self.assertRaises(ValueError):
            return_source.validate_metadata(wrong_route)

    def test_actual_nand_closure_fixture_and_missing_node_rejection(self):
        raw = json.loads(NAND_FIXTURE.read_text())
        nodes = {path: {name: bytes.fromhex(value) for name, value in props.items()}
                 for path, props in raw.items()}
        closure = return_source.nand_closure(nodes)
        self.assertEqual(hashlib.sha256(closure).hexdigest(), return_source.NAND_SHA256)
        del nodes["/soc/qpic-nand@79b0000/nandcs@0"]
        with self.assertRaises(ValueError):
            return_source.nand_closure(nodes)


if __name__ == "__main__":
    unittest.main()
