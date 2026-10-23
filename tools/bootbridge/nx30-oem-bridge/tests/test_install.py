import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import subprocess
import tempfile
import unittest
import zlib
import zipfile
import io

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "bridge_install.c"
HARNESS = Path(__file__).with_name("install_harness.c")
NAND_LOG = Path(__file__).with_name("fixtures") / "nand-dmesg.raw"
BACKUP_DATA_BYTES = 45 * 1024 * 1024
BACKUP_CONFIG = [(17, 0x5900000, 0x600000, "pdt_data"),
                 (18, 0x5f00000, 0x600000, "pdt_data_1"),
                 (19, 0x6500000, 0x100000, "exp"),
                 (20, 0x6600000, 0x1700000, "plugin")]
PAGE = 2048
APPSBL = 0x140000
STARTUP = 0x900000
LAYOUT = [
    (0x000000, 0x080000, "0:SBL1", "nand"),
    (0x080000, 0x080000, "misc0", "nand"),
    (0x100000, 0x040000, "0:BOOTCONFIG", "nand"),
    (0x140000, 0x040000, "0:BOOTCONFIG1", "nand"),
    (0x180000, 0x300000, "misc1", "nand"),
    (0x480000, 0x080000, "0:APPSBLENV", "nand"),
    (0x500000, 0x140000, "APPSBL_1", "nand"),
    (0x640000, 0x140000, "APPSBL", "nand"),
    (0x780000, 0x100000, "0:ART", "nand"),
    (0x880000, 0x080000, "misc2", "nand"),
]

def digest(data):
    return hashlib.sha256(data).hexdigest()

def write_proc_mounts(root, contents):
    """Mirror Linux procfs: mounts is a symlink, while self/mounts is readable."""
    root = Path(root)
    target = root / "proc/self/mounts"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(contents)
    alias = root / "proc/mounts"
    if alias.exists() or alias.is_symlink():
        alias.unlink()
    alias.symlink_to("self/mounts")

def write_env_entries(root, entries, bad_crc=False):
    raw = bytearray((Path(root) / "dev/mtd5").read_bytes())
    env = bytearray(b"\xff" * 0x40000)
    serialized = b"\0".join(k.encode() + b"=" + v.encode() for k, v in entries) + b"\0\0"
    env[4:4 + len(serialized)] = serialized
    crc = zlib.crc32(env[4:]) & 0xffffffff
    if bad_crc:
        crc ^= 1
    struct.pack_into("<I", env, 0, crc)
    raw[:0x40000] = env
    (Path(root) / "dev/mtd5").write_bytes(raw)

def bc_image(age=2, appsbl=1, rootfs=1, invalid=False):
    b = bytearray(b"\xff" * 0x40000)
    struct.pack_into("<III", b, 0, 0xA3A2A1A0, age, 8)
    names = ["0:QSEE", "0:DEVCFG", "0:CDT", "0:APPSBL", "0:HLOS", "rootfs", "0:WIFIFW", "0:BTFW"]
    for i, name in enumerate(names):
        value = appsbl if i == 3 else rootfs if i == 5 else 0
        struct.pack_into("<16sI", b, 12 + i * 20, name.encode(), value)
    struct.pack_into("<I", b, 332, 0xB3B2B1B0)
    if invalid:
        b[12:16] = b"BAD!"
    return bytes(b)

def make_fixture(root, selector=1, fault=None, active_payload=False, include_ubi=True):
    root = Path(root)
    for sub in ("proc/sys/kernel/random", "proc/device-tree", "etc/bridge", "dev", "sys/class/mtd", "sys/devices/system/qfprom/qfprom0", "tmp"):
        (root / sub).mkdir(parents=True, exist_ok=True)
    (root / "etc/bridge/profile").write_text("H3C-BOOTBRIDGE-RUNTIME-v1\n")
    (root / "etc/bridge/rootfs-id").write_text("H3C-BOOTBRIDGE-ROOTFS-v1\n")
    (root / "proc/sys/kernel/osrelease").write_text("4.4.60\n")
    (root / "proc/cmdline").write_text("console=ttyMSM0\n")
    (root / "proc/nand-probe-log").write_text(
        "nand: device found, Manufacturer ID: 0xef, Chip ID: 0xba\n"
        "nand: Winbond W25N01GWZEIG SPI NAND 1.8V 1G-BIT\n"
        "nand: 128 MiB, SLC, erase size: 128 KiB, page size: 2048, OOB size: 64\n"
    )
    (root / "proc/sys/kernel/random/boot_id").write_text("01234567-89ab-cdef-0123-456789abcdef\n")
    (root / "proc/device-tree/compatible").write_bytes(b"qcom,ipq5018-mp02.1\0qcom,ipq5018\0")
    (root / "proc/device-tree/h3c,bridge-profile").write_bytes(b"H3C-BOOTBRIDGE-MULTICONFIG-V1\0")
    (root / "sys/devices/system/qfprom/qfprom0/authenticate").write_text("0\n")
    payload = bytes((i * 29 + 17) & 255 for i in range(APPSBL))
    (root / "etc/bridge/uboot.bin").write_bytes(payload)
    (root / "etc/bridge/test-payload-sha256").write_text(digest(payload) + "\n")
    sbl = bytes((i * 13 + 7) & 255 for i in range(178176)) + b"\xff" * (0x80000 - 178176)
    (root / "etc/bridge/test-sbl-sha256").write_text(digest(sbl[:178176]) + "\n")
    active = payload if active_payload else bytes((i * 11 + 3) & 255 for i in range(APPSBL))
    (root / "etc/bridge/test-allowed-appsbl-sha256").write_text(digest(active) + "\n" + digest(payload) + "\n")
    proc = ['dev:    size   erasesize  name']
    original = {}
    for idx, (off, size, name, typ) in enumerate(LAYOUT):
        if idx in (6, 7):
            is_active = (idx == 6 and selector == 0) or (idx == 7 and selector == 1)
            name = ("0:APPSBL" if is_active else "0:APPSBL_1")
        if idx == 0:
            data = sbl
        elif idx == 2 or idx == 3:
            data = bc_image(appsbl=selector)
        elif idx == 5:
            data = bytearray(b"\xff" * size)
            envdata = bytearray(b"\xff" * 0x40000)
            vars_ = b"productname=NX30\0machid=8040000\0bootcmd=bootipq\0bootargs=console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3\0bootdelay=3\0\0"
            envdata[4:4 + len(vars_)] = vars_
            struct.pack_into("<I", envdata, 0, zlib.crc32(envdata[4:]) & 0xffffffff)
            data[:0x40000] = envdata
            data = bytes(data)
        elif idx == 6 or idx == 7:
            physical_active = (idx == 6 and selector == 0) or (idx == 7 and selector == 1)
            data = active if physical_active else bytes((i * (idx + 3) + 5) & 255 for i in range(size))
        else:
            data = bytes((i * (idx + 1) + 19) & 255 for i in range(size))
        original[off] = data
        (root / f"dev/mtd{idx}").write_bytes(data)
        d = root / f"sys/class/mtd/mtd{idx}"
        d.mkdir(parents=True, exist_ok=True)
        fields = {"name": name, "offset": hex(off), "size": hex(size), "erasesize": "0x20000", "writesize": "0x800", "oobsize": "0x40", "ecc_strength": "4", "dev": f"90:{idx}", "type": typ}
        for k, v in fields.items():
            (d / k).write_text(v + "\n")
        if idx == 0:
            (d / "sbl_valid_bytes").write_text("178176\n")
        proc.append(f"mtd{idx}: {size:08x} 00020000 \"{name}\"")
    if include_ubi:
        idx = len(LAYOUT)
        (root / "sys/class/mtd" / f"mtd{idx}").mkdir(exist_ok=True)
        d = root / "sys/class/mtd" / f"mtd{idx}"
        (d / "type").write_text("ubi\n")
        (d / "offset").write_text(hex(STARTUP) + "\n")
        proc.append(f"mtd{idx}: 00010000 00020000 \"rootfs_data\"")
    (root / "proc/mtd").write_text("\n".join(proc) + "\n")
    if fault:
        (root / "fault").write_text(fault + "\n")
    make_oem_fixture(root)
    return original, payload

def write_test_oem_manifest_header(include_dir, candidate_sha=None, product="BOS_PRODUCT_NX30", include_return_rules=True,
                                  exit_policy="BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0"):
    """Exact compiled synthetic oracle; never reads packages or device exports."""
    import test_oem_slot as oslot
    _, manifest, _ = oslot.fixture()
    candidate = candidate_sha or digest(bytes((i * 29 + 17) & 255 for i in range(APPSBL)))
    original = digest(bytes((i * 11 + 3) & 255 for i in range(APPSBL)))
    def array(b): return '{' + ','.join(str(x) for x in b) + '}'
    rows=[]
    for offset in (0x900000, 0x3100000):
        volumes=','.join('{%du,%s,%du}' % (v.size_bytes,array(v.sha256),v.type) for v in manifest.immutable)
        rows.append('{{%du,41943040u,131072u,2048u,64u,4u},%s,%s,{%s}}' % (offset,array(bytes.fromhex(candidate)),array(manifest.volume_table_sha256),volumes))
    text='#include "bridge_oem_slot.h"\nstatic const bos_pool_rule TEST_OEM_POOL[] = {\n'+',\n'.join('{'+r+',1u,'+exit_policy+','+product+',1u,BOS_TABLE_RAW_EXACT}' for r in rows)+'\n};\n'
    revision=array(bytes.fromhex(digest(text.encode()+b'explicit-synthetic-test-provenance')))
    text+='static const uint8_t TEST_OEM_REVISION[32] = '+revision+';\n'
    text+='#define BI_OEM_POOL TEST_OEM_POOL\n#define BI_OEM_POOL_COUNT 2u\n#define BI_OEM_POOL_REVISION TEST_OEM_REVISION\n'
    text+='#define TEST_BOOT_CANDIDATE_SHA256 "%s"\n#define TEST_BOOT_OEM_SHA256 "%s"\n' % (candidate,original)
    text+='#define TEST_BOOT_OLD_SHA256 "%s"\n' % digest(b'O'*APPSBL)
    text+='#define TEST_BUILD_CANDIDATE_SHA256 "%s"\n' % candidate
    if include_return_rules:
        # Synthetic contents exercise the actual finite NX source policy; the
        # explicit synthetic revision and BI_TEST binary do not grant hardware
        # qualification or claim that these bytes came from an OEM image.
        source='61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922';kernel=bytes(manifest.immutable[0].sha256).hex();source_rev=digest(('explicit-synthetic-source-v1'+kernel).encode())
        text+='#include "bridge_oem_return.h"\n'
        for key,value in {'OFFICIAL_SHA256':source,'STOCK_KERNEL_SHA256':kernel,'BRIDGE_KERNEL_SHA256':kernel,'REVISION_SHA256':source_rev,'ROUTE':'config@mp02.1','COMPATIBLE':'qcom,ipq5018-mp02.1','COMPRESSED_KERNEL_SHA256':'c'*64,'NAND_DT_SHA256':'d'*64}.items():
            text+='#define BI_RETURN_SOURCE_%s "%s"\n' % (key,value)
        text+='#define BI_RETURN_SOURCE_PRODUCT BOS_PRODUCT_NX30\n'
        text+='static const br_return_rule TEST_RETURN_RULES[] = {{1u,BOS_PRODUCT_NX30,BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0,BI_RETURN_SOURCE_OFFICIAL_SHA256,BI_RETURN_SOURCE_STOCK_KERNEL_SHA256,BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256,BI_RETURN_SOURCE_REVISION_SHA256,BI_RETURN_SOURCE_ROUTE,BI_RETURN_SOURCE_COMPATIBLE,"h3c-oem-efba-128m-2k-sp64",4u,64u,BR_RETURN_BC_WRITE_REVIEWED_LINUX_MTD_V1,BR_RETURN_ENV_EXPLICIT_BOOTIPQ_V1,BI_RETURN_SOURCE_COMPRESSED_KERNEL_SHA256,BI_RETURN_SOURCE_NAND_DT_SHA256,"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"}};\n'
        text+='#define BI_RETURN_RULES TEST_RETURN_RULES\n#define BI_RETURN_RULE_COUNT 1u\n'
    (Path(include_dir)/'test_oem_manifest.h').write_text(text)


def write_return_registry_header(include_dir, official_sha, product, product_name):
    """Build a test-only source registry for the real BRIDGE_RETURN_ONLY path."""
    import test_oem_slot as oslot
    import sys
    sys.path.insert(0, str(ROOT / "tools"))
    import bridge_kernel
    _, manifest, _ = oslot.fixture()
    kernel = bytes(manifest.immutable[0].sha256).hex()
    sbl = bytes((i * 13 + 7) & 255 for i in range(178176))
    appsbl = bytes((i * 11 + 3) & 255 for i in range(APPSBL))
    exit_policy = "BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0"
    rows=[]; rules=[]
    for idx, (route, (_, compatible, route_hash)) in enumerate(bridge_kernel.ROUTES.items(), 1):
        revision = digest((official_sha + route + kernel).encode())
        compressed = digest(("compressed" + route).encode())
        nand_dt = digest(("nand-dt" + route).encode())
        rows.append('{"%s","%s","%s","%s","%s","%s","%s","%s","%s","%s",178176u,%du,%s}' % (
            official_sha, kernel, kernel, revision, compressed, nand_dt, route, compatible,
            digest(sbl), digest(appsbl), APPSBL, product))
        rules.append('{%du,%s,%s,"%s","%s","%s","%s","%s","%s","h3c-oem-efba-128m-2k-sp64",4u,64u,BR_RETURN_BC_WRITE_REVIEWED_LINUX_MTD_V1,BR_RETURN_ENV_EXPLICIT_BOOTIPQ_V1,"%s","%s","%s"}' % (
            idx, product, exit_policy, official_sha, kernel, kernel, revision, route, compatible,
            compressed, nand_dt, 'e' * 64))
    header = '#include "bridge_oem_return.h"\n#include "bridge_oem_slot.h"\n'
    header += 'static const br_return_source_record TEST_RETURN_SOURCES[] = {\n' + ',\n'.join(rows) + '\n};\n'
    header += '#define BI_RETURN_SOURCES TEST_RETURN_SOURCES\n#define BI_RETURN_SOURCE_COUNT 3u\n'
    header += 'static const br_return_rule TEST_RETURN_RULES[] = {\n' + ',\n'.join(rules) + '\n};\n'
    header += '#define BI_RETURN_RULES TEST_RETURN_RULES\n#define BI_RETURN_RULE_COUNT 3u\n'
    (Path(include_dir) / 'bridge_oem_return_registry.h').write_text(header)
    write_test_oem_manifest_header(include_dir, official_sha, product, include_return_rules=False)
    return kernel


def write_provider_return_registry_header(include_dir, official_sha, product, route="config@mp02.1"):
    """One exact policy-2 source/route registry for a regular provider test."""
    import test_oem_slot as oslot
    import sys
    sys.path.insert(0, str(ROOT / "tools"))
    import bridge_kernel
    _, manifest, _ = oslot.fixture()
    kernel = bytes(manifest.immutable[0].sha256).hex()
    compatible = bridge_kernel.ROUTES[route][1]
    sbl = bytes((i * 13 + 7) & 255 for i in range(178176))
    appsbl = bytes((i * 11 + 3) & 255 for i in range(APPSBL))
    revision = digest((official_sha + route + kernel).encode())
    compressed = digest(("compressed" + route).encode())
    nand_dt = digest(("nand-dt" + route).encode())
    row = '{"%s","%s","%s","%s","%s","%s","%s","%s","%s","%s",178176u,%du,%s}' % (
        official_sha, kernel, kernel, revision, compressed, nand_dt, route, compatible,
        digest(sbl), digest(appsbl), APPSBL, product)
    header = '#include "bridge_oem_return.h"\n#include "bridge_oem_slot.h"\n'
    header += 'static const br_return_source_record TEST_RETURN_SOURCES[] = {' + row + '};\n'
    header += '#define BI_RETURN_SOURCES TEST_RETURN_SOURCES\n#define BI_RETURN_SOURCE_COUNT 1u\n'
    header += ('static const br_return_rule TEST_RETURN_RULES[] = {{1u,%s,BR_RETURN_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V2,'
               '"%s","%s","%s","%s","%s","%s","h3c-oem-efba-128m-2k-sp64",4u,64u,'
               'BR_RETURN_BC_WRITE_REVIEWED_LINUX_MTD_V1,BR_RETURN_ENV_EXPLICIT_BOOTIPQ_V1,"%s","%s","%s"}};\n' %
               (product, official_sha, kernel, kernel, revision, route, compatible, compressed, nand_dt, 'e' * 64))
    header += '#define BI_RETURN_RULES TEST_RETURN_RULES\n#define BI_RETURN_RULE_COUNT 1u\n'
    (Path(include_dir) / 'bridge_oem_return_registry.h').write_text(header)
    source_values = {
        'OFFICIAL_SHA256': official_sha, 'STOCK_KERNEL_SHA256': kernel,
        'BRIDGE_KERNEL_SHA256': kernel, 'REVISION_SHA256': revision,
        'COMPRESSED_KERNEL_SHA256': compressed, 'NAND_DT_SHA256': nand_dt,
        'ROUTE': route, 'COMPATIBLE': compatible,
    }
    source_header = ''.join('#define BI_RETURN_SOURCE_%s "%s"\n' % (key, value)
                            for key, value in source_values.items())
    source_header += '#define BI_RETURN_SOURCE_PRODUCT %s\n' % product
    (Path(include_dir) / 'bridge_oem_return_source.h').write_text(source_header)
    write_test_oem_manifest_header(include_dir, official_sha, product, include_return_rules=False,
                                   exit_policy='BOS_EXIT_INSTALLED_CANDIDATE_BOOTIPQ_V1')
    return kernel


def make_oem_fixture(root, root_selector=1, bridge_offset=None, oem_offset=None):
    """Two ordinary synthetic 40MiB slot files plus independent live-root witnesses."""
    import test_oem_slot as oslot
    root=Path(root)
    (root/'etc/bridge/profile').write_text('H3C-BOOTBRIDGE-RUNTIME-v1\n')
    (root/'etc/bridge/rootfs-id').write_text('H3C-BOOTBRIDGE-ROOTFS-v1\n')
    (root/'proc/device-tree/h3c,bridge-profile').write_bytes(b'H3C-BOOTBRIDGE-MULTICONFIG-V1\0')
    envpath=root/'dev/mtd5'
    if envpath.exists():
        raw=bytearray(envpath.read_bytes());end=raw.find(b'\0\0',4,0x40000)
        if end>4 and b'bootcmd=' not in raw[4:end]:
            extra=b'bootcmd=bootipq\0bootargs=console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3\0bootdelay=3\0\0'
            start=end+1
            raw[start:start+len(extra)]=extra
            struct.pack_into('<I',raw,0,zlib.crc32(raw[4:0x40000])&0xffffffff);envpath.write_bytes(raw)
    bridge_offset=(0x900000 if root_selector==0 else 0x3100000) if bridge_offset is None else bridge_offset
    _, _, blocks=oslot.fixture()
    del blocks[6]  # empty official writable volume is legal
    lines=[line for line in (root/'proc/mtd').read_text().splitlines() if not line.startswith(('mtd15:','mtd16:'))]
    for idx,offset in ((15,0x900000),(16,0x3100000)):
        name='rootfs' if offset==(0x900000 if root_selector==0 else 0x3100000) else 'rootfs_1'
        node=root/f'sys/class/mtd/mtd{idx}';node.mkdir(parents=True,exist_ok=True)
        for k,v in {'name':name,'offset':hex(offset),'size':'0x2800000','erasesize':'0x20000','writesize':'0x800','oobsize':'0x40','ecc_strength':'4','dev':f'90:{idx}','type':'nand'}.items():(node/k).write_text(v+'\n')
        lines.append(f'mtd{idx}: 02800000 00020000 "{name}"')
        erased=b'\xff'*oslot.PEB
        with (root/f'dev/mtd{idx}').open('wb') as stream:
            for p in range(oslot.N):
                block=blocks.get(p,erased)
                if offset==bridge_offset and p==5:
                    block=bytearray(block);block[4096:4102]=b'BRIDGE'
                stream.write(block)
    (root/'proc/mtd').write_text('\n'.join(lines)+'\n')
    cmd=(root/'proc/cmdline').read_text().strip()
    if 'ubi.mtd=' not in cmd:cmd+=' ubi.mtd=rootfs root=/dev/ubiblock0_3'
    (root/'proc/cmdline').write_text(cmd+'\n')
    for rel,value in [('sys/class/ubi/ubi0/mtd_num','15' if bridge_offset==0x900000 else '16'),('sys/class/ubi/ubi0_3/name','ubi_rootfs')]:
        p=root/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(value+'\n')
    write_proc_mounts(root, '/dev/ubiblock0_3 / squashfs ro,relatime 0 0\nproc /proc proc rw 0 0\n')
    return bridge_offset, (0x3100000 if bridge_offset==0x900000 else 0x900000) if oem_offset is None else oem_offset

def make_backup_fixture(root):
    """Four disposable ordinary configuration files; no archive/device input."""
    root = Path(root)
    lines = (root / "proc/mtd").read_text().splitlines()
    for index, offset, size, name in BACKUP_CONFIG:
        node = root / f"sys/class/mtd/mtd{index}"; node.mkdir(parents=True, exist_ok=True)
        fields = {"name": name, "type": "nand", "offset": hex(offset), "size": hex(size),
                  "erasesize": "0x20000", "writesize": "0x800", "oobsize": "0x40",
                  "ecc_strength": "4", "dev": f"90:{index}"}
        for key, value in fields.items():
            (node / key).write_text(value + "\n")
        (root / f"dev/mtd{index}").write_bytes(bytes([index + 160]) * size)
        lines = [line for line in lines if not line.startswith(f"mtd{index}:")]
        lines.append(f'mtd{index}: {size:08x} 00020000 "{name}"')
    (root / "proc/mtd").write_text("\n".join(lines) + "\n")

def status_result(exe, fixture, mode="status"):
    p = subprocess.run([str(exe), str(fixture), mode], capture_output=True, text=True, timeout=45)
    if p.returncode:
        raise AssertionError(f"harness rc={p.returncode}, stderr={p.stderr}, stdout={p.stdout[-500:]}")
    lines = [line for line in p.stdout.splitlines() if line.startswith("{")]
    return p, [json.loads(x) for x in lines]

class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="nx30-install-tests-")
        cls.exe = Path(cls.tmp.name) / "install_harness"
        cls.prod_guard = Path(cls.tmp.name) / "production_fixture_guard"
        catalog = Path(os.environ.get("H3C_NAND_CATALOG_HEADER", ""))
        if not catalog.is_file():
            raise RuntimeError("set H3C_NAND_CATALOG_HEADER to the authoritative shared catalog header")
        catalog_include = Path(cls.tmp.name) / "catalog-include"
        catalog_include.mkdir()
        (catalog_include / "h3c_nand_catalog.h").write_bytes(catalog.read_bytes())
        # This host fixture has explicit synthetic NX geometry. Production builds
        # use the contract generated from their exact official source UBI.
        (catalog_include / "bridge_root_binding_contract.generated.h").write_text(
            "/* explicit synthetic test-only root binding contract */\n"
            "#include <stdint.h>\n"
            "#define BI_ROOT_BINDING_CONTRACT { UINT64_C(17395712), UINT64_C(126976), UINT64_C(137), UINT64_C(2048) }\n"
            '#define BI_ROOT_BINDING_CONTRACT_SHA256 "' + "0" * 64 + '"\n')
        write_test_oem_manifest_header(catalog_include)
        include = ["-DBI_ROOT_BINDING_CONTRACT_HEADER=\"bridge_root_binding_contract.generated.h\"",
                   "-I", str(catalog_include), "-I", str(ROOT / "src")]
        cls.return_registry_bins = {}
        sys.path.insert(0, str(ROOT / "tools"))
        import build_return_source as return_source
        products = {"RC3000": ("BOS_PRODUCT_RC3000", 2), "RT3000": ("BOS_PRODUCT_RT3000", 3),
                    "RW3000": ("BOS_PRODUCT_RW3000", 4), "NX30": ("BOS_PRODUCT_NX30", 1)}
        for name, (product_name, product_number) in products.items():
            official_sha = next(sha for sha, product in return_source.PRODUCTS.items()
                                if product == product_number)
            variant = Path(cls.tmp.name) / ("return-registry-" + name)
            variant.mkdir()
            write_return_registry_header(variant, official_sha, product_name, name)
            exe = Path(cls.tmp.name) / ("return-registry-" + name + ".bin")
            compile_result = subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_RETURN_ONLY", "-DBRIDGE_RETURN_REGISTRY_TEST",
                            "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
                            '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
                            "-I", str(variant), *include, str(HARNESS), str(SRC),
                            str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"),
                            str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"),
                            str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"), "-o", str(exe)],
                           capture_output=True, text=True)
            if compile_result.returncode:
                raise RuntimeError(compile_result.stderr)
            cls.return_registry_bins[name] = (exe, official_sha, product_name)
        provider_sha = next(sha for sha, product in return_source.PRODUCTS.items() if product == 1)
        provider_include = Path(cls.tmp.name) / "provider-return-registry"
        provider_include.mkdir()
        write_provider_return_registry_header(provider_include, provider_sha, "BOS_PRODUCT_NX30")
        (provider_include / "bridge_payload.h").write_text(
            '#define BI_PAYLOAD_VERSION "synthetic-provider-guard"\n'
            '#define BI_PAYLOAD_SHA256 "' + provider_sha + '"\n'
            '#define BI_PAYLOAD_BYTES 0u\n#define BI_PAYLOAD_PATH "/tmp/unavailable-payload"\n'
            '#define BI_VERSION_SCOPE "synthetic-provider-guard"\n'
            'static const bv_record BI_VERSION_RELEASES[] = {{"' + provider_sha + '",'
            '"synthetic-provider-guard",1310720u,1u,BV_CUSTOM_RELEASE}};\n'
            '#define BI_VERSION_REGISTRY BI_VERSION_RELEASES\n#define BI_VERSION_REGISTRY_COUNT 1u\n')
        cls.provider_return_exe = Path(cls.tmp.name) / "provider-return-v2.bin"
        provider_compile = subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_RETURN_REGISTRY_TEST",
                        "-DBI_RETURN_SOURCE_REGISTRY_V2=1", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
                        '-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"',
                        '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
                        "-I", str(provider_include), *include, str(HARNESS), str(SRC),
                        str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"),
                        str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"),
                        str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"),
                        "-o", str(cls.provider_return_exe)], capture_output=True, text=True)
        if provider_compile.returncode:
            raise RuntimeError(provider_compile.stderr)
        provider_rx_include = Path(cls.tmp.name) / "provider-return-rx-registry"
        provider_rx_include.mkdir()
        write_provider_return_registry_header(provider_rx_include, provider_sha, "BOS_PRODUCT_NX30",
                                              route="config@mp03.1")
        cls.provider_return_rx_exe = Path(cls.tmp.name) / "provider-return-rx-v2.bin"
        provider_rx_compile = subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_RETURN_REGISTRY_TEST",
                        "-DBI_RETURN_SOURCE_REGISTRY_V2=1", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
                        '-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"',
                        '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
                        "-I", str(provider_rx_include), *include, str(HARNESS), str(SRC),
                        str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"),
                        str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"),
                        str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"),
                        "-o", str(cls.provider_return_rx_exe)], capture_output=True, text=True)
        if provider_rx_compile.returncode:
            raise RuntimeError(provider_rx_compile.stderr)
        subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST", *include, str(HARNESS), str(SRC), str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"), str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"), str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"), "-o", str(cls.exe)], check=True)
        # This immutable, compiled synthetic registry exercises the same real
        # bv_evaluate path as production, not the compatibility fixture adapter.
        cls.version_exe = Path(cls.tmp.name) / "version_install_harness"
        candidate = bytes((i * 29 + 17) & 255 for i in range(APPSBL))
        oem = bytes((i * 11 + 3) & 255 for i in range(APPSBL))
        cls.version_bytes = {"old": b"O" * APPSBL, "new": b"N" * APPSBL,
                             "wrong_scope": b"W" * APPSBL, "unknown": b"?" * APPSBL}
        rows = [(digest(oem), "synthetic-d-ecc4", 0, "BV_OEM_BASELINE"),
                (digest(cls.version_bytes["old"]), "synthetic-d-ecc4", 1, "BV_CUSTOM_RELEASE"),
                (digest(candidate), "synthetic-d-ecc4", 2, "BV_CUSTOM_RELEASE"),
                (digest(cls.version_bytes["new"]), "synthetic-d-ecc4", 3, "BV_CUSTOM_RELEASE"),
                (digest(cls.version_bytes["wrong_scope"]), "other-scope", 4, "BV_CUSTOM_RELEASE")]
        header = '#define BI_VERSION_SCOPE "synthetic-d-ecc4"\nstatic const bv_record TEST_RELEASES[] = {\n'
        for fingerprint, scope, sequence, kind in rows:
            header += '{"%s","%s",%d,%d,%s},\n' % (fingerprint, scope, APPSBL, sequence, kind)
        header += '};\n#define BI_VERSION_REGISTRY TEST_RELEASES\n#define BI_VERSION_REGISTRY_COUNT (sizeof(TEST_RELEASES)/sizeof(TEST_RELEASES[0]))\n'
        (catalog_include / "test_version_registry.h").write_text(header)
        subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
                        "-DBRIDGE_VERSION_COMPILED_TEST_REGISTRY", *include, str(HARNESS), str(SRC),
                        str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"), str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"), str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"), "-o", str(cls.version_exe)], check=True)
        guard = Path(__file__).with_name("production_fixture_guard.c")
        subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", *include, "-I", str(ROOT / "src"), str(guard), str(SRC), str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"), str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"), str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"), "-o", str(cls.prod_guard)], check=True)
        cls.prod_return_guard = Path(cls.tmp.name) / "production_return_fixture_guard"
        subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DBRIDGE_RETURN_ONLY", "-DBI_HAVE_PAYLOAD=0", *include, "-I", str(ROOT / "src"), str(guard), str(SRC), str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"), str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"), str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"), "-o", str(cls.prod_return_guard)], check=True)
        cls.prod_v2_guard = Path(cls.tmp.name) / "production_provider_return_fixture_guard"
        subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-DBI_RETURN_SOURCE_REGISTRY_V2=1",
                        '-DBI_OEM_MANIFEST_HEADER="test_oem_manifest.h"',
                        '-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"',
                        '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
                        "-I", str(provider_include), *include, "-I", str(ROOT / "src"), str(guard), str(SRC),
                        str(ROOT / "src/bridge_version.c"), str(ROOT / "src/bridge_oem_slot.c"),
                        str(ROOT / "src/bridge_backup.c"), str(ROOT / "src/bridge_root_binding.c"),
                        str(ROOT / "src/bridge_first_install.c"), str(ROOT / "src/bridge_mtd_observer.c"), str(ROOT / "src/bridge_install_identity.c"),
                        "-o", str(cls.prod_v2_guard)], check=True)

        cls.no_manifest_exe=Path(cls.tmp.name)/"no_oem_manifest"
        cls.no_boot_exe=Path(cls.tmp.name)/"no_candidate_boot_contract"
        extra=[str(ROOT/"src"/name) for name in ("bridge_version.c","bridge_oem_slot.c","bridge_backup.c","bridge_root_binding.c","bridge_first_install.c","bridge_install_identity.c")]
        subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST",*include,str(HARNESS),str(SRC),*extra,"-o",str(cls.no_manifest_exe)],check=True)
        no_boot_include=Path(cls.tmp.name)/"no-boot-contract";no_boot_include.mkdir()
        base_header=(catalog_include/"test_oem_manifest.h").read_text()
        header=base_header.replace('#define TEST_BOOT_CANDIDATE_SHA256 "'+digest(candidate)+'"','#define TEST_BOOT_CANDIDATE_SHA256 "'+'0'*64+'"')
        (no_boot_include/"test_oem_manifest.h").write_text(header)
        subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST","-DBRIDGE_OEM_COMPILED_TEST_MANIFEST","-I",str(no_boot_include),*include,str(HARNESS),str(SRC),*extra,"-o",str(cls.no_boot_exe)],check=True)

        cls.pool_binding_exe=Path(cls.tmp.name)/"pool_binding"
        subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST","-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",*include,str(ROOT/"tests/oem_pool_binding_harness.c"),*extra,"-o",str(cls.pool_binding_exe)],check=True)
        cls.unknown_exit_exe=Path(cls.tmp.name)/"unknown_exit_policy"
        unknown_include=Path(cls.tmp.name)/"unknown-exit-policy";unknown_include.mkdir()
        unknown_header=base_header.replace('BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0','BOS_EXIT_UNAVAILABLE')
        (unknown_include/"test_oem_manifest.h").write_text(unknown_header)
        subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST","-DBRIDGE_OEM_COMPILED_TEST_MANIFEST","-I",str(unknown_include),*include,str(HARNESS),str(SRC),*extra,"-o",str(cls.unknown_exit_exe)],check=True)

        cls.route_mismatch_exe=Path(cls.tmp.name)/"route_mismatch"
        route_include=Path(cls.tmp.name)/"route-mismatch";route_include.mkdir()
        route_header=base_header.replace('BI_RETURN_SOURCE_ROUTE,BI_RETURN_SOURCE_COMPATIBLE','"config@other",BI_RETURN_SOURCE_COMPATIBLE')
        (route_include/"test_oem_manifest.h").write_text(route_header)
        subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST","-DBRIDGE_OEM_COMPILED_TEST_MANIFEST","-I",str(route_include),*include,str(HARNESS),str(SRC),*extra,"-o",str(cls.route_mismatch_exe)],check=True)

        cls.source_mismatch_exe=Path(cls.tmp.name)/"source_mismatch"
        source_include=Path(cls.tmp.name)/"source-mismatch";source_include.mkdir()
        source_header=base_header.replace('BI_RETURN_SOURCE_OFFICIAL_SHA256,BI_RETURN_SOURCE_STOCK_KERNEL_SHA256','"'+'f'*64+'",BI_RETURN_SOURCE_STOCK_KERNEL_SHA256')
        (source_include/"test_oem_manifest.h").write_text(source_header)
        subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST","-DBRIDGE_OEM_COMPILED_TEST_MANIFEST","-I",str(source_include),*include,str(HARNESS),str(SRC),*extra,"-o",str(cls.source_mismatch_exe)],check=True)

        cls.bridge_kernel_mismatch_exe=Path(cls.tmp.name)/"bridge_kernel_mismatch"
        kernel_include=Path(cls.tmp.name)/"bridge-kernel-mismatch";kernel_include.mkdir()
        bridge_line=next(line for line in base_header.splitlines() if line.startswith("#define BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256 "))
        bridge_header=base_header.replace(bridge_line,'#define BI_RETURN_SOURCE_BRIDGE_KERNEL_SHA256 "'+'f'*64+'"')
        (kernel_include/"test_oem_manifest.h").write_text(bridge_header)
        subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST","-DBRIDGE_OEM_COMPILED_TEST_MANIFEST","-I",str(kernel_include),*include,str(HARNESS),str(SRC),*extra,"-o",str(cls.bridge_kernel_mismatch_exe)],check=True)

        cls.other_product_exes=[]
        for product in ("BOS_PRODUCT_RC3000","BOS_PRODUCT_RT3000","BOS_PRODUCT_RW3000"):
            variant=Path(cls.tmp.name)/("other-"+product);variant.mkdir()
            variant_header=base_header.replace('1u,BOS_PRODUCT_NX30,BOS_EXIT_NX30_R012_REGISTERED_STOCK_BASELINE0',f'1u,{product},BOS_EXIT_UNAVAILABLE')
            (variant/"test_oem_manifest.h").write_text(variant_header)
            exe=Path(cls.tmp.name)/("provider-"+product)
            subprocess.run(["cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-DBRIDGE_INSTALL_TEST","-DBRIDGE_OEM_COMPILED_TEST_MANIFEST","-I",str(variant),*include,str(HARNESS),str(SRC),*extra,"-o",str(exe)],check=True)
            cls.other_product_exes.append(exe)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def fixture(self, **kw):
        d = Path(tempfile.mkdtemp(prefix="case-", dir=self.tmp.name))
        make_fixture(d, **kw)
        self.addCleanup(shutil.rmtree, d)
        return d

    def release_fixture(self, d):
        self._cleanups = [entry for entry in self._cleanups if not (entry[0] is shutil.rmtree and entry[1] == (d,))]
        shutil.rmtree(d)

    def test_regular_provider_v2_return_survives_candidate_mismatch(self):
        import build_return_source as return_source
        official_sha = next(sha for sha, product in return_source.PRODUCTS.items() if product == 1)
        d = self.fixture()
        _, (state,) = status_result(self.provider_return_exe, d)
        self.assertFalse(state["install_ready"])
        self.assertTrue(state["actions"]["return_oem"], json.dumps(state.get("checks", []), ensure_ascii=False))
        self.assertEqual(state["oem_return"]["source_sha256"], official_sha)
        self.assertEqual(state["oem_return"]["product"], "NX30")
        self.assertTrue(any(c["name"] == "candidate_payload" and c["status"] == "unavailable"
                            for c in state["checks"]))

    def test_regular_provider_v2_accepts_source_bound_rx_identity_on_its_route(self):
        d = self.fixture()
        (d / "proc/device-tree/compatible").write_bytes(b"qcom,ipq5018-mp03.1\0qcom,ipq5018\0")
        write_env_entries(d, [("productname", "RX3000"), ("machid", "8040000"),
                              ("bootcmd", "bootipq"),
                              ("bootargs", "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),
                              ("bootdelay", "3")])
        _, (state,) = status_result(self.provider_return_rx_exe, d)
        self.assertTrue(state["actions"]["return_oem"], json.dumps(state.get("checks", []), ensure_ascii=False))
        self.assertEqual(state["oem_return"]["source_sha256"], next(
            sha for sha, product in __import__("build_return_source").PRODUCTS.items() if product == 1))
        self.assertEqual(state["oem_return"]["product"], "NX30")

    def test_return_only_registry_all_sources_routes_and_transaction_guards(self):
        import build_return_source as return_source
        import bridge_kernel
        for name, (exe, official_sha, product_macro) in self.return_registry_bins.items():
            product_name = name
            d = self.fixture()
            write_env_entries(d, [("productname", product_name), ("machid", "8040000"),
                                  ("bootcmd", "bootipq"),
                                  ("bootargs", "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),
                                  ("bootdelay", "3")])
            for route, (_, compatible, _) in bridge_kernel.ROUTES.items():
                with self.subTest(source=name, route=route):
                    (d / "proc/device-tree/compatible").write_bytes(
                        compatible.encode() + b"\0qcom,ipq5018\0")
                    route_product = "RX3000" if name == "NX30" and route != "config@mp02.1" else product_name
                    write_env_entries(d, [("productname", route_product), ("machid", "8040000"),
                                          ("bootcmd", "bootipq"),
                                          ("bootargs", "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),
                                          ("bootdelay", "3")])
                    _, (state,) = status_result(exe, d)
                    self.assertFalse(state["install_ready"])
                    self.assertTrue(state["actions"]["return_oem"])
                    self.assertEqual(state["oem_return"]["source_sha256"], official_sha)
            write_env_entries(d, [("productname", "NX30" if name != "NX30" else "RC3000"),
                                  ("bootcmd", "bootipq"), ("bootargs", "console=ttyMSM0"),
                                  ("bootdelay", "3")])
            _, (wrong_product,) = status_result(exe, d)
            self.assertFalse(wrong_product["actions"]["return_oem"])
            d = self.fixture()
            write_env_entries(d, [("productname", product_name), ("bootcmd", "bootipq"),
                                  ("bootargs", "console=ttyMSM0 root=/dev/ubiblock0_3"),
                                  ("bootdelay", "3")])
            _, (root_override,) = status_result(exe, d)
            self.assertFalse(root_override["actions"]["return_oem"])
            self.release_fixture(d)
        # The registry-selected production branch preserves per-copy BOOTCONFIG
        # bytes and never changes either APP selector.
        exe = self.return_registry_bins["NX30"][0]
        d = self.fixture()
        bad_oem = d / "dev/mtd15"
        raw = bytearray(bad_oem.read_bytes()); raw[5 * 131072 + 4096] ^= 1; bad_oem.write_bytes(raw)
        _, (bad_hash,) = status_result(exe, d)
        self.assertFalse(bad_hash["actions"]["return_oem"])
        self.release_fixture(d)
        d = self.fixture()
        for idx, marker in ((2, 0x31), (3, 0x72)):
            p = d / f"dev/mtd{idx}"
            raw = bytearray(p.read_bytes()); raw[0x300] = marker; p.write_bytes(raw)
        _, (returned,) = status_result(exe, d, "return_oem")
        self.assertEqual(returned["phase"], "complete", returned["blocked_reason"])
        for idx, marker in ((2, 0x31), (3, 0x72)):
            raw = (d / f"dev/mtd{idx}").read_bytes()
            self.assertEqual(raw[0x300], marker)
            self.assertEqual(struct.unpack_from("<I", raw, 0x58)[0], 1)
        self.release_fixture(d)
        for fault in ("bc1_partial", "bc0_before_erase"):
            d = self.fixture(fault=fault)
            _, (failed,) = status_result(exe, d, "return_oem")
            self.assertEqual(failed["phase"], "failed")
            self.assertTrue(failed["may_have_modified"])
            self.assertFalse(failed["actions"]["return_oem"])
            self.release_fixture(d)

    def test_ready_with_ubi_and_optional_backup(self):
        d = self.fixture()
        self.assertTrue((d / "proc/mounts").is_symlink())
        self.assertEqual(os.readlink(d / "proc/mounts"), "self/mounts")
        self.assertTrue((d / "proc/self/mounts").is_file())
        make_backup_fixture(d)
        p, (s,) = status_result(self.exe, d)
        self.assertEqual(s["phase"], "ready")
        self.assertEqual(s["token"].__len__(), 64)
        self.assertTrue(s["backup"]["available"])
        self.assertGreater(s["backup"]["size_bytes"], BACKUP_DATA_BYTES)
        self.assertEqual(s["backup"]["data_bytes"], BACKUP_DATA_BYTES)
        self.assertEqual(s["backup"]["format"], "zip")
        self.assertTrue(s["backup"]["complete"])
        self.assertEqual(Path(d / "tmp" / "nx30-bridge-install-boot.lock").exists(), False)
        self.assertTrue(any(c["status"] == "pass" for c in s["checks"]))

    def test_production_binary_rejects_fixture_root(self):
        d=self.fixture()
        for binary in (self.prod_guard, self.prod_return_guard, self.prod_v2_guard):
            p=subprocess.run([str(binary),str(d)],capture_output=True,text=True,timeout=5)
            self.assertEqual(p.returncode,0,p.stderr)
            s=json.loads(p.stdout)
            self.assertEqual(s["phase"],"blocked")
            self.assertIn("拒绝 fixture_root",s["blocked_reason"])

    def test_recovery_zip_complete_exact_bytes_hashes_and_no_source_writes(self):
        d = self.fixture(); make_backup_fixture(d)
        before = {p.name: digest(p.read_bytes()) for p in (d / "dev").iterdir()}
        _, (state,) = status_result(self.exe, d)
        self.assertEqual(state["phase"], "ready", state["blocked_reason"])
        archive = list((d / "tmp").glob("nx30-bridge-install-*/backup.zip"))
        self.assertEqual(len(archive), 1)
        data = archive[0].read_bytes()
        self.assertEqual(state["backup"]["sha256"], digest(data))
        self.assertEqual(state["backup"]["size_bytes"], len(data))
        self.assertEqual(archive[0].stat().st_mode & 0o777, 0o400)
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertIsNone(z.testzip())
            manifest = json.loads(z.read("manifest.json"))
            self.assertEqual(manifest["data_bytes"], BACKUP_DATA_BYTES)
            self.assertFalse(manifest["contains_oob"])
            self.assertFalse(manifest["contains_ecc_bytes"])
            self.assertFalse(manifest["restore_api"])
            self.assertFalse(manifest["restore_format_qualified"])
            expected = {off: (idx, size) for idx, (off, size, _, _) in enumerate(LAYOUT)}
            expected.update({off: (idx, size) for idx, off, size, _ in BACKUP_CONFIG})
            self.assertEqual(len(manifest["entries"]), len(expected))
            self.assertEqual(set(z.namelist()), {e["name"] for e in manifest["entries"]} | {"manifest.json"})
            for entry in manifest["entries"]:
                idx, size = expected[entry["offset"]]
                content = z.read(entry["name"])
                self.assertEqual(len(content), size)
                self.assertEqual(content, (d / f"dev/mtd{idx}").read_bytes())
                self.assertEqual(entry["sha256"], digest(content))
                self.assertEqual(entry["crc32"], f"{zlib.crc32(content):08x}")
        self.assertEqual({p.name: digest(p.read_bytes()) for p in (d / "dev").iterdir()}, before)
        type(self).backup_evidence={"zip_bytes":len(data),"zip_sha256":digest(data),
                                   "data_bytes":manifest["data_bytes"],"data_entries":len(manifest["entries"]),
                                   "zip_crc_all_passed":True,"entry_bytes_crc_sha_all_passed":True,
                                   "source_hashes_unchanged":True,"synthetic_only":True}

    def test_recovery_zip_failures_publish_nothing_and_leave_install_optional(self):
        for fault in ("backupnomem", "backupspacefail", "backupreadfail", "backupchange", "backupverifyfail"):
            with self.subTest(fault=fault):
                d = self.fixture(fault=fault); make_backup_fixture(d)
                before = {p.name: digest(p.read_bytes()) for p in (d / "dev").iterdir()}
                _, (state,) = status_result(self.exe, d)
                self.assertEqual(state["phase"], "ready", state["blocked_reason"])
                self.assertFalse(state["backup"]["available"])
                self.assertFalse(state["backup"]["complete"])
                self.assertEqual(state["backup"]["data_bytes"], 0)
                self.assertEqual(state["backup"]["sha256"], "")
                self.assertEqual(list((d / "tmp").glob("nx30-bridge-install-*/backup*")), [])
                self.assertEqual({p.name: digest(p.read_bytes()) for p in (d / "dev").iterdir()}, before)

    def test_recovery_zip_requires_all_config_windows_without_blocking_install(self):
        d = self.fixture()
        _, (state,) = status_result(self.exe, d)
        self.assertEqual(state["phase"], "ready", state["blocked_reason"])
        self.assertFalse(state["backup"]["available"])

    def test_recovery_zip_rejects_config_name_geometry_and_overlapping_alias(self):
        for mutation in ("name", "geometry", "alias"):
            with self.subTest(mutation=mutation):
                d = self.fixture(); make_backup_fixture(d)
                if mutation == "name":
                    (d / "sys/class/mtd/mtd17/name").write_text("unexpected_config\n")
                elif mutation == "geometry":
                    (d / "sys/class/mtd/mtd17/writesize").write_text("4096\n")
                else:
                    node=d / "sys/class/mtd/mtd21";node.mkdir()
                    fields={"name":"config_alias","type":"nand","offset":"0x5902000","size":"0x20000",
                            "erasesize":"0x20000","writesize":"0x800","oobsize":"0x40","ecc_strength":"4","dev":"90:21"}
                    for key,value in fields.items():(node/key).write_text(value+"\n")
                    (d / "dev/mtd21").write_bytes(b"Z" * 0x20000)
                    with (d / "proc/mtd").open("a") as stream:stream.write('mtd21: 00020000 00020000 "config_alias"\n')
                before={p.name:digest(p.read_bytes()) for p in (d / "dev").iterdir()}
                _, (state,) = status_result(self.exe,d)
                self.assertEqual(state["phase"],"ready",state["blocked_reason"])
                self.assertFalse(state["backup"]["available"])
                self.assertEqual(list((d / "tmp").glob("nx30-bridge-install-*/backup*")),[])
                self.assertEqual({p.name:digest(p.read_bytes()) for p in (d / "dev").iterdir()},before)

    def test_success_preserves_protected_data_and_replay_is_rejected(self):
        d = self.fixture(fault="backupfail")
        original, payload = make_fixture(d, fault="backupfail")
        p, states = status_result(self.exe, d, "replay")
        final = states[0]
        self.assertEqual(final["phase"], "complete")
        self.assertTrue(final["may_have_modified"])
        self.assertFalse(final["backup"]["available"])
        self.assertIn("replay_rc=-1", p.stdout)
        self.assertIn("reboot_rc=0", p.stdout)
        self.assertIn("reboot_simulated", states[-1]["stage"])
        self.assertEqual((d / "dev/mtd6").read_bytes(), payload)
        self.assertEqual((d / "dev/mtd7").read_bytes(), original[0x640000])
        self.assertEqual((d / "dev/mtd5").read_bytes(), original[0x480000])
        b0=(d / "dev/mtd2").read_bytes();b1=(d / "dev/mtd3").read_bytes()
        self.assertEqual(b0,b1)
        self.assertEqual(struct.unpack_from("<I", b0, 4)[0],3)
        self.assertEqual(struct.unpack_from("<I", b0, 0x58)[0],0)
        self.assertEqual(struct.unpack_from("<I", b0, 0x80)[0],0)

    def test_invalid_bc_and_badblock_block(self):
        for kind in ("badblock", "invalid_bc"):
            d = self.fixture(fault="badblock" if kind == "badblock" else None)
            if kind == "invalid_bc":
                b=bytearray((d / "dev/mtd2").read_bytes());b[12:16]=b"BAD!";(d / "dev/mtd2").write_bytes(b)
                (d / "dev/mtd3").write_bytes(b)
            _, (s,) = status_result(self.exe, d)
            if kind == "badblock":
                self.assertEqual(s["phase"],"blocked")
                self.assertFalse(s["may_have_modified"])
            else:
                self.assertEqual(s["phase"],"blocked")

    def test_nand_probe_identity_missing_or_conflicting_blocks(self):
        for change in ("missing", "id_conflict", "model_conflict", "geometry_conflict"):
            d=self.fixture()
            log=d/"proc/nand-probe-log"
            baseline=log.read_text()
            if change == "missing":
                log.unlink()
            elif change == "id_conflict":
                log.write_text(baseline + "nand: device found, Manufacturer ID: 0xc8, Chip ID: 0x41\n")
            elif change == "model_conflict":
                log.write_text(baseline + "nand: GigaDevice GD5F1GQ5REYIG SPI NAND 1.8V\n")
            else:
                log.write_text(baseline + "nand: 256 MiB, SLC, erase size: 128 KiB, page size: 2048, OOB size: 64\n")
            _,(s,)=status_result(self.exe,d)
            self.assertEqual(s["phase"],"blocked",change)
            self.assertFalse(s["may_have_modified"],change)
            self.assertFalse(s["install_ready"],change)

    def test_sanitized_nand_probe_excerpt_is_accepted(self):
        raw = NAND_LOG.read_bytes()
        self.assertEqual(digest(raw), "8a43a2080aa3cdabf824419d87357373a453c1923456be752dc218e52fddf9d3")
        d=self.fixture()
        (d/"proc/nand-probe-log").write_bytes(raw)
        _,(s,)=status_result(self.exe,d)
        self.assertEqual(s["phase"],"ready",s["blocked_reason"])
        self.assertTrue(any(c["name"] == "nand_identity" and c["status"] == "pass" for c in s["checks"]))

    def test_worker_rechecks_canonical_nand_identity(self):
        d=self.fixture()
        _,(s,)=status_result(self.exe,d,"nand_noise_recheck")
        self.assertEqual(s["phase"],"complete",s["blocked_reason"])
        d=self.fixture()
        original=(d/"dev/mtd6").read_bytes()
        _,(s,)=status_result(self.exe,d,"nand_identity_recheck")
        self.assertEqual(s["phase"],"failed")
        self.assertFalse(s["may_have_modified"])
        self.assertFalse(s["can_reboot"])
        self.assertEqual((d/"dev/mtd6").read_bytes(),original)

    def test_stale_snapshot_and_partial_write_fail_closed(self):
        for mode in ("stale","stale_map"):
            d=self.fixture()
            _,(s,)=status_result(self.exe,d,mode)
            self.assertEqual(s["phase"],"failed")
            self.assertFalse(s["may_have_modified"])
        d=self.fixture(fault="partial_write")
        _,(s,)=status_result(self.exe,d,"install")
        self.assertEqual(s["phase"],"failed")
        self.assertTrue(s["may_have_modified"])
        self.assertFalse(s["can_reboot"])

    def test_bc_partial_commit_and_abnormal_worker_exit(self):
        for fault in ("bc1_partial", "bc0_before_erase"):
            d=self.fixture(fault=fault);original,payload=make_fixture(d,fault=fault)
            _,(s,)=status_result(self.exe,d,"install")
            self.assertEqual(s["phase"],"failed")
            self.assertTrue(s["may_have_modified"])
            self.assertFalse(s["can_reboot"])
            self.assertEqual((d/"dev/mtd6").read_bytes(),payload)
            if fault == "bc1_partial":
                self.assertEqual((d/"dev/mtd2").read_bytes(),original[0x100000])
                self.assertNotEqual((d/"dev/mtd3").read_bytes(),original[0x140000])
            else:
                self.assertEqual((d/"dev/mtd2").read_bytes(),original[0x100000])
                self.assertEqual(struct.unpack_from("<I",(d/"dev/mtd3").read_bytes(),4)[0],3)
        d=self.fixture(fault="worker_exit");original,_=make_fixture(d,fault="worker_exit")
        _,(s,)=status_result(self.exe,d,"install")
        self.assertEqual(s["phase"],"failed")
        self.assertTrue(s["may_have_modified"])
        self.assertFalse(s["can_reboot"])
        self.assertEqual((d/"dev/mtd6").read_bytes(),original[0x500000])
        self.assertEqual((d/"dev/mtd2").read_bytes(),original[0x100000])
        self.assertEqual((d/"dev/mtd3").read_bytes(),original[0x140000])

    def test_reboot_before_complete_is_rejected(self):
        d=self.fixture()
        p,_=status_result(self.exe,d,"reboot_before")
        self.assertIn("reboot_rc=-1",p.stdout)

    @unittest.skip("Historical private partition inputs are outside this synthetic-only suite")
    def test_real_archived_nx30_bc_and_env_bytes(self):
        evidence = ROOT.parents[1] / "work" / "nx30-env-identity-restore-20261011" / "readback"
        paths = [evidence / "mtd2-before.bin", evidence / "mtd3-before.bin", evidence / "mtd10-after.bin"]
        if not all(p.is_file() for p in paths):
            self.skipTest("private live evidence files are not present in this checkout")
        d=self.fixture(selector=0)
        (d/"dev/mtd2").write_bytes(paths[0].read_bytes())
        (d/"dev/mtd3").write_bytes(paths[1].read_bytes())
        (d/"dev/mtd5").write_bytes(paths[2].read_bytes())
        _,(s,)=status_result(self.exe,d)
        self.assertEqual(s["phase"],"ready",s["blocked_reason"])

    def test_env_exact_nx30_2i5x_alias_with_oem_config_is_accepted(self):
        d = self.fixture()
        write_env_entries(d, [("productname", "NX30_2i5x"),
                              ("productconfig", "/etc/config/NX30/product_NX30.ini"),
                              ("machid", "8040000"),("bootcmd","bootipq"),
                              ("bootargs","console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),("bootdelay","3")])
        _, (s,) = status_result(self.exe, d)
        self.assertEqual(s["phase"], "ready", s["blocked_reason"])
        self.assertTrue(any(c["name"] == "appsblenv" and c["status"] == "pass" for c in s["checks"]))

    def test_env_exact_product_names_preserve_success_and_diagnostics(self):
        for name in ("NX30", "NX30_2i5x"):
            d = self.fixture()
            write_env_entries(d, [("productname", name), ("machid", "8040000"),("bootcmd","bootipq"),
                                  ("bootargs","console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),("bootdelay","3")])
            _, (s,) = status_result(self.exe, d)
            self.assertEqual(s["phase"], "ready", s["blocked_reason"])
            checks = {c["name"]: c for c in s["checks"]}
            self.assertEqual(checks["mtd_map"]["status"], "pass")
            self.assertIn(name, checks["appsblenv"]["detail"])

    def test_env_nx30_2i5x_near_aliases_are_rejected(self):
        for name in ("NX30_2i5x-extra", "NX30_2i5y", "nx30_2i5x"):
            d = self.fixture()
            write_env_entries(d, [("productname", name),
                                  ("productconfig", "/etc/config/NX30/product_NX30.ini"),
                                  ("machid", "8040000"),("bootcmd","bootipq"),
                                  ("bootargs","console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),("bootdelay","3")])
            _, (s,) = status_result(self.exe, d)
            self.assertEqual(s["phase"], "blocked", name)
            self.assertFalse(s["install_ready"], name)
            self.assertFalse(s["actions"]["return_oem"], name)
            checks={c["name"]:c for c in s["checks"]}
            self.assertEqual(checks["appsblenv"]["status"], "unavailable", name)
            self.assertIn("唯一产品名", checks["appsblenv"]["detail"], name)
            self.assertNotIn("CRC", checks["appsblenv"]["detail"], name)

    def test_env_nx30_2i5x_duplicate_and_bad_crc_are_rejected(self):
        entries = [("productname", "NX30_2i5x"),
                   ("productconfig", "/etc/config/NX30/product_NX30.ini"),
                   ("machid", "8040000"),("bootcmd","bootipq"),
                   ("bootargs","console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),("bootdelay","3")]
        d = self.fixture()
        write_env_entries(d, entries + [("productname", "NX30_2i5x")])
        _, (s,) = status_result(self.exe, d)
        self.assertEqual(s["phase"], "blocked")
        self.assertIn("唯一产品名", s["blocked_reason"])
        self.assertNotIn("CRC", s["blocked_reason"])
        d = self.fixture()
        write_env_entries(d, entries, bad_crc=True)
        _, (s,) = status_result(self.exe, d)
        self.assertEqual(s["phase"], "blocked")
        self.assertEqual(s["blocked_reason"], "APPSBLENV CRC 校验失败")

    def test_env_duplicate_product_and_bad_256k_crc_block(self):
        for damage in ("duplicate_product", "bad_crc"):
            d=self.fixture()
            p=d/"dev/mtd5"; raw=bytearray(p.read_bytes())
            if damage == "duplicate_product":
                env=bytearray(raw[:0x40000]);tail=env[4:].find(b"\0\0")
                pos=5+tail
                env[pos:pos+len(b"productname=NX30\0\0")]=b"productname=NX30\0\0"
                struct.pack_into("<I",env,0,zlib.crc32(env[4:])&0xffffffff);raw[:0x40000]=env
            else:
                raw[0]^=1
            p.write_bytes(raw)
            _,(s,)=status_result(self.exe,d)
            self.assertEqual(s["phase"],"blocked")
            if damage=="duplicate_product":
                self.assertFalse(s["actions"]["return_oem"])
                self.assertIn("APPSBLENV",s["blocked_reason"])
            else:
                self.assertIn("APPSBLENV",s["blocked_reason"])

    def test_qfprom_authenticate_must_be_explicit_zero(self):
        for value in ("1\n", None):
            d=self.fixture()
            p=d/"sys/devices/system/qfprom/qfprom0/authenticate"
            if value is None:p.unlink()
            else:p.write_text(value)
            _,(s,)=status_result(self.exe,d)
            self.assertEqual(s["phase"],"ready")
            self.assertFalse(s["install_ready"])
            self.assertTrue(s["actions"]["return_oem"])
            checks={c["name"]:c for c in s["checks"]}
            self.assertEqual(checks["qfprom_authenticate"]["status"],"unavailable")

    def test_qualification_reports_every_blocker_in_one_pass(self):
        # One preflight must list independent blockers together, so a tester
        # can report them all from a single boot instead of one per release.
        d=self.fixture()
        (d/"sys/devices/system/qfprom/qfprom0/authenticate").write_text("1\n")
        (d/"etc/bridge/uboot.bin").write_bytes(b"X"*APPSBL)
        _,(s,)=status_result(self.exe,d)
        self.assertFalse(s["install_ready"])
        unavailable=[c["name"] for c in s["checks"] if c["status"]=="unavailable"]
        self.assertIn("qfprom_authenticate",unavailable)
        self.assertIn("candidate_payload",unavailable)
        self.assertLess(unavailable.index("qfprom_authenticate"),unavailable.index("candidate_payload"))

    def test_candidate_machid_conflict_does_not_close_independent_return(self):
        d=self.fixture()
        (d/"proc/cmdline").write_text("console=ttyMSM0 machid=08040001 ubi.mtd=rootfs root=/dev/ubiblock0_3\n")
        process,(state,)=status_result(self.exe,d,"return_oem")
        self.assertEqual(state["stage"],"oem_return_reboot_simulated",state["blocked_reason"])
        self.assertFalse(state["install_ready"])
        self.assertIn("return_replay_rc=-1",process.stdout)

    def test_missing_or_corrupt_candidate_does_not_close_bc_only_return(self):
        for mode in ("missing","corrupt"):
            with self.subTest(mode=mode):
                d=self.fixture();payload=d/"etc/bridge/uboot.bin"
                if mode=="missing":payload.unlink()
                else:payload.write_bytes(b"X"*APPSBL)
                before={idx:(d/f"dev/mtd{idx}").read_bytes() for idx in (0,5,6,7,8,15,16)}
                process,(state,)=status_result(self.exe,d,"return_oem")
                self.assertEqual(state["stage"],"oem_return_reboot_simulated",state["blocked_reason"])
                self.assertFalse(state["install_ready"])
                self.assertEqual((d/"dev/mtd2").read_bytes(),(d/"dev/mtd3").read_bytes())
                self.assertEqual(struct.unpack_from("<I",(d/"dev/mtd2").read_bytes(),4)[0],3)
                for idx,data in before.items():self.assertEqual((d/f"dev/mtd{idx}").read_bytes(),data)
                self.assertIn("return_replay_rc=-1",process.stdout)

    def test_return_contract_rejects_env_source_route_and_non_nx_providers(self):
        mutations=(("bootcmd",[("productname","NX30"),("bootcmd","other")]),
                   ("short_bootargs",[("productname","NX30"),("bootcmd","bootipq"),
                                      ("bootargs","console=ttyMSM0,115200n8 loglevel=3"),("bootdelay","3")]),
                   ("bootargs",[("productname","NX30"),("bootcmd","bootipq"),("bootargs","console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=6 loglevel=3"),("bootdelay","3")]),
                   ("bootdelay",[("productname","NX30"),("bootcmd","bootipq"),("bootargs","console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),("bootdelay","99")]))
        for field,entries in mutations:
            with self.subTest(field=field):
                d=self.fixture();write_env_entries(d,entries)
                _,(state,)=status_result(self.exe,d)
                self.assertFalse(state["actions"]["return_oem"])
                self.assertFalse(state["actions"]["reboot_oem"])
        for exe in self.other_product_exes:
            with self.subTest(provider=exe.name):
                d=self.fixture();_,(state,)=status_result(exe,d)
                self.assertFalse(state["actions"]["return_oem"])
                self.assertFalse(state["actions"]["reboot_oem"])
        for exe in (self.route_mismatch_exe,self.source_mismatch_exe,self.bridge_kernel_mismatch_exe,self.unknown_exit_exe):
            with self.subTest(provider=exe.name):
                d=self.fixture();_,(state,)=status_result(exe,d)
                self.assertFalse(state["actions"]["return_oem"])
                self.assertFalse(state["actions"]["reboot_oem"])

    def test_return_accepts_exact_live_hw_d_bdf_bootargs_variant(self):
        d=self.fixture()
        live_args="console=ttyMSM0,115200n8 cnss2.bdf_integrated=0x10 cnss2.bdf_pci0=0x60 cnss2.bdf_pci1=0x60 cnss2.skip_radio_bmap=4 loglevel=3"
        write_env_entries(d,[("productname","NX30"),("machid","8040000"),
                             ("bootcmd","bootipq"),("bootargs",live_args),("bootdelay","1")])
        _,(ready,)=status_result(self.exe,d)
        self.assertEqual(ready["phase"],"ready",ready["blocked_reason"])
        self.assertTrue(ready["actions"]["return_oem"])
        process,(returned,)=status_result(self.exe,d,"return_oem")
        self.assertEqual(returned["phase"],"complete",returned["blocked_reason"])
        self.assertEqual(returned["operation"],"return_oem")
        self.assertEqual(returned["stage"],"oem_return_reboot_simulated")
        self.assertIn("return_replay_rc=-1",process.stdout)

    def test_return_rejects_unreviewed_bdf_bootargs_variants(self):
        base="console=ttyMSM0,115200n8"
        bdf=("cnss2.bdf_integrated=0x10", "cnss2.bdf_pci0=0x60", "cnss2.bdf_pci1=0x60")
        skip="cnss2.skip_radio_bmap=4 loglevel=3"
        variants=(
            f"{base} {bdf[0]} {bdf[1]} {skip}", # missing pci1
            f"{base} cnss2.bdf_integrated=0x11 {bdf[1]} {bdf[2]} {skip}", # changed value
            f"{base} {bdf[1]} {bdf[0]} {bdf[2]} {skip}", # reordered
            f"{base} {bdf[0]} {bdf[1]} {bdf[2]} {bdf[0]} {skip}", # duplicate
            f"{base} {bdf[0]} {bdf[1]} {bdf[2]} {skip} extra=1", # extra token
            f"{base} {bdf[0]} {bdf[1]} {bdf[2]} cnss2.skip_radio_bmap=6 loglevel=3", # changed skip
        )
        for args in variants:
            with self.subTest(args=args):
                d=self.fixture()
                write_env_entries(d,[("productname","NX30"),("machid","8040000"),
                                     ("bootcmd","bootipq"),("bootargs",args),("bootdelay","1")])
                before={d/f"dev/mtd{x}":(d/f"dev/mtd{x}").read_bytes() for x in (2,3,6,7)}
                _,(state,)=status_result(self.exe,d)
                self.assertFalse(state["actions"]["return_oem"])
                self.assertFalse(state["actions"]["reboot_oem"])
                rejected=subprocess.run([str(self.exe),str(d),"return_oem"],capture_output=True,text=True,timeout=45)
                self.assertNotEqual(rejected.returncode,0)
                for path,data in before.items():self.assertEqual(path.read_bytes(),data)

    def test_exact_bdf_args_do_not_allow_return_from_active_custom_appsbl(self):
        d=self.fixture(active_payload=True)
        live_args="console=ttyMSM0,115200n8 cnss2.bdf_integrated=0x10 cnss2.bdf_pci0=0x60 cnss2.bdf_pci1=0x60 cnss2.skip_radio_bmap=4 loglevel=3"
        write_env_entries(d,[("productname","NX30"),("machid","8040000"),
                             ("bootcmd","bootipq"),("bootargs",live_args),("bootdelay","1")])
        before={d/f"dev/mtd{x}":(d/f"dev/mtd{x}").read_bytes() for x in (2,3,6,7)}
        _,(state,)=status_result(self.exe,d)
        self.assertFalse(state["actions"]["return_oem"])
        self.assertFalse(state["actions"]["reboot_oem"])
        self.assertFalse(state["oem_return"]["available"])
        rejected=subprocess.run([str(self.exe),str(d),"return_oem"],capture_output=True,text=True,timeout=45)
        self.assertNotEqual(rejected.returncode,0)
        for path,data in before.items():self.assertEqual(path.read_bytes(),data)

    def test_return_operation_ticket_tamper_fails_before_writes(self):
        d=self.fixture(fault="ticket_tamper")
        before={p.name:digest(p.read_bytes()) for p in (d/"dev").iterdir()}
        _,(state,)=status_result(self.exe,d,"return_oem")
        self.assertEqual(state["phase"],"failed")
        self.assertFalse(state["may_have_modified"])
        self.assertEqual(state["stage"],"operation_ticket_failed")
        self.assertEqual({p.name:digest(p.read_bytes()) for p in (d/"dev").iterdir()},before)

    def test_cmdline_machid_may_be_absent_but_conflicts_block(self):
        d=self.fixture()
        (d/"proc/cmdline").write_text("console=ttyMSM0 machid=08040001 ubi.mtd=rootfs root=/dev/ubiblock0_3\n")
        _,(s,)=status_result(self.exe,d)
        self.assertEqual(s["phase"],"ready")
        self.assertFalse(s["install_ready"])
        self.assertTrue(s["actions"]["return_oem"])
        checks={c["name"]:c for c in s["checks"]}
        self.assertEqual(checks["machid"]["status"],"unavailable")
        self.assertIn("machid",checks["machid"]["detail"])

    def test_selector_zero_logical_names_and_mismatch(self):
        d=self.fixture(selector=0)
        _,(s,)=status_result(self.exe,d)
        self.assertEqual(s["phase"],"ready")
        d=self.fixture(selector=0)
        b=bytearray((d/"dev/mtd2").read_bytes());struct.pack_into("<I",b,0x58,1);(d/"dev/mtd2").write_bytes(b);(d/"dev/mtd3").write_bytes(b)
        _,(s,)=status_result(self.exe,d)
        self.assertEqual(s["phase"],"blocked")

    def test_compiled_registry_first_install_skip_upgrade_and_rejections(self):
        for variant, expected, reason in [("oem", "ready", "首次安装"),
                                          ("candidate", "ready", "跳过重复"),
                                          ("old", "ready", "允许升级"),
                                          ("new", "blocked", "拒绝降级"),
                                          ("unknown", "ready", "覆盖安装"),
                                          ("wrong_scope", "blocked", "不兼容")]:
            with self.subTest(variant=variant):
                d = self.fixture(active_payload=variant == "candidate")
                if variant in self.version_bytes:
                    (d / "dev/mtd7").write_bytes(self.version_bytes[variant])
                before = {p.name: p.read_bytes() for p in (d / "dev").iterdir()}
                _, (state,) = status_result(self.version_exe, d)
                self.assertEqual(state["phase"], expected, state["blocked_reason"])
                checks = [c for c in state["checks"] if c["name"] == "trusted_version"]
                self.assertTrue(checks)
                self.assertIn(reason, checks[-1]["detail"])
                self.assertFalse(state["may_have_modified"])
                self.assertEqual({p.name: p.read_bytes() for p in (d / "dev").iterdir()}, before)

    def test_compiled_old_release_upgrade_executes_real_transaction(self):
        d = self.fixture(); (d / "dev/mtd7").write_bytes(self.version_bytes["old"])
        protected = (d / "dev/mtd7").read_bytes()
        _, (state,) = status_result(self.version_exe, d, "nand_noise_recheck")
        self.assertEqual(state["phase"], "complete", state["blocked_reason"])
        self.assertEqual((d / "dev/mtd7").read_bytes(), protected)
        self.assertEqual((d / "dev/mtd6").read_bytes(), (d / "etc/bridge/uboot.bin").read_bytes())

    def test_compiled_unknown_active_override_executes_real_transaction(self):
        d = self.fixture(); (d / "dev/mtd7").write_bytes(self.version_bytes["unknown"])
        protected = (d / "dev/mtd7").read_bytes()
        _, (state,) = status_result(self.version_exe, d, "nand_noise_recheck")
        self.assertEqual(state["phase"], "complete", state["blocked_reason"])
        self.assertEqual((d / "dev/mtd7").read_bytes(), protected)
        self.assertEqual((d / "dev/mtd6").read_bytes(), (d / "etc/bridge/uboot.bin").read_bytes())

    def test_version_commit_recheck_rejects_newer_active_and_token_replay(self):
        d = self.fixture(); (d / "dev/mtd7").write_bytes(self.version_bytes["old"])
        (d / "etc/bridge/test-newer-appsbl.bin").write_bytes(self.version_bytes["new"])
        protected = {p.name: p.read_bytes() for p in (d / "dev").iterdir() if p.name != "mtd7"}
        process, (state,) = status_result(self.version_exe, d, "stale_active_version")
        self.assertEqual(state["phase"], "failed", state["blocked_reason"])
        self.assertIn("启动保护区或物理映射变化", state["blocked_reason"])
        self.assertFalse(state["may_have_modified"])
        self.assertFalse(state["can_reboot"])
        self.assertIn("replay_rc=-1", process.stdout)
        checks = [c for c in state["checks"] if c["name"] == "protected_snapshot"]
        self.assertTrue(checks)
        self.assertEqual(checks[-1]["status"],"blocked")
        self.assertEqual({p.name: p.read_bytes() for p in (d / "dev").iterdir() if p.name != "mtd7"}, protected)
        # mtd7 changed only by the explicit harness injection; all installer
        # targets including both BC copies remain identical to their baseline.
        self.assertEqual((d / "dev/mtd7").read_bytes(), self.version_bytes["new"])

    def test_oem_manifest_is_compiled_not_a_runtime_boolean(self):
        d=self.fixture()
        (d/'etc/bridge/oem-manifest.json').write_text('{"verified":true,"bootable":true}')
        _,(state,)=status_result(self.no_manifest_exe,d)
        self.assertEqual(state['phase'],'blocked')
        self.assertFalse(state['install_ready'])
        self.assertFalse(state['actions']['return_oem'])
        self.assertIn('manifest',state['blocked_reason'])

    def test_content_pool_unknown_exit_policy_cannot_unlock_actions(self):
        d=self.fixture();before={p.name:p.read_bytes() for p in (d/'dev').iterdir()}
        _,(state,)=status_result(self.unknown_exit_exe,d)
        self.assertEqual(state['phase'],'blocked')
        self.assertFalse(state['install_ready']);self.assertFalse(state['actions']['return_oem'])
        self.assertFalse(state['actions']['reboot_oem']);self.assertFalse(state['may_have_modified'])
        self.assertEqual({p.name:p.read_bytes() for p in (d/'dev').iterdir()},before)

    def test_candidate_pool_revision_group_and_policy_are_transaction_bound(self):
        for field in ('revision','candidate','group','policy'):
            with self.subTest(field=field):
                d=self.fixture();before={p.name:p.read_bytes() for p in (d/'dev').iterdir()}
                result=subprocess.run([str(self.pool_binding_exe),str(d),field],capture_output=True,text=True,timeout=75)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                state=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{')))
                self.assertEqual(state['phase'],'failed');self.assertFalse(state['may_have_modified'])
                self.assertFalse(state['can_reboot'])
                self.assertEqual({p.name:p.read_bytes() for p in (d/'dev').iterdir()},before)

    def test_oem_linux_geometry_does_not_substitute_uboot_oob(self):
        result=subprocess.run([str(self.pool_binding_exe),"unused","linux-geometry"],
                              capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("PASS OEM Linux raw-spare MTD geometry",result.stdout)

    def test_production_profile_collector_uses_startup_snapshots_not_40m_slot_snapshots(self):
        d=self.fixture()
        result=subprocess.run([str(self.pool_binding_exe),str(d),"collector"],capture_output=True,text=True,timeout=90)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("PASS production profile collector",result.stdout)

    def test_original_return_does_not_require_new_candidate_boot_contract(self):
        d=self.fixture()
        _,(state,)=status_result(self.no_boot_exe,d)
        self.assertEqual(state['phase'],'ready',state['blocked_reason'])
        self.assertFalse(state['install_ready'])
        self.assertTrue(state['actions']['return_oem'])
        process,(returned,)=status_result(self.no_boot_exe,d,'return_oem')
        self.assertEqual(returned['stage'],'oem_return_reboot_simulated',returned['blocked_reason'])
        self.assertEqual(returned['operation'],'return_oem')
        self.assertFalse(returned['boot_policy']['verified'])
        self.assertEqual(returned['boot_policy']['maintenance_entry'],'unknown')
        self.assertIn('return_replay_rc=-1',process.stdout)

    def test_oem_return_is_bc_only_secondary_then_primary_and_reboots_once(self):
        d=self.fixture()
        before={idx:(d/f'dev/mtd{idx}').read_bytes() for idx in (0,2,3,5,6,7,8)}
        slot_hash={idx:digest((d/f'dev/mtd{idx}').read_bytes()) for idx in (15,16)}
        process,(state,)=status_result(self.exe,d,'return_oem')
        self.assertEqual(state['phase'],'complete',state['blocked_reason'])
        self.assertEqual(state['operation'],'return_oem')
        self.assertEqual(state['stage'],'oem_return_reboot_simulated')
        self.assertTrue(state['may_have_modified'])
        self.assertFalse(state['can_reboot'])
        self.assertFalse(state['actions']['return_oem'])
        self.assertIn('return_replay_rc=-1',process.stdout)
        b0=(d/'dev/mtd2').read_bytes();self.assertEqual(b0,(d/'dev/mtd3').read_bytes())
        self.assertEqual(struct.unpack_from('<I',b0,4)[0],3)
        self.assertEqual(struct.unpack_from('<I',b0,0x58)[0],1)
        self.assertEqual(struct.unpack_from('<I',b0,0x80)[0],0)
        allowed=set(range(4,8))|set(range(0x80,0x84))
        self.assertTrue(all(i in allowed for i,(a,b) in enumerate(zip(before[2],b0)) if a!=b))
        for idx in (0,5,6,7,8):self.assertEqual((d/f'dev/mtd{idx}').read_bytes(),before[idx])
        for idx in (15,16):self.assertEqual(digest((d/f'dev/mtd{idx}').read_bytes()),slot_hash[idx])

    def test_oem_return_preserves_asymmetric_non_target_bc_bytes(self):
        d=self.fixture()
        bc0=d/'dev/mtd2';bc1=d/'dev/mtd3'
        first=bytearray(bc0.read_bytes());second=bytearray(bc1.read_bytes())
        first[0x300]=0x31;second[0x300]=0x72
        bc0.write_bytes(first);bc1.write_bytes(second)
        process,(state,)=status_result(self.exe,d,'return_oem')
        self.assertEqual(state['phase'],'complete',state['blocked_reason'])
        self.assertEqual(state['stage'],'oem_return_reboot_simulated')
        out0=bc0.read_bytes();out1=bc1.read_bytes()
        self.assertEqual(out0[0x300],0x31);self.assertEqual(out1[0x300],0x72)
        self.assertNotEqual(out0,out1)
        for data in (out0,out1):
            self.assertEqual(struct.unpack_from('<I',data,4)[0],3)
            self.assertEqual(struct.unpack_from('<I',data,0x58)[0],1)
            self.assertEqual(struct.unpack_from('<I',data,0x80)[0],0)
        self.assertIn('return_replay_rc=-1',process.stdout)

    def test_oem_return_partial_backup_or_primary_failure_freezes_without_retry(self):
        for fault in ('bc1_partial','bc0_before_erase'):
            with self.subTest(fault=fault):
                d=self.fixture(fault=fault)
                appsbl=[(d/f'dev/mtd{idx}').read_bytes() for idx in (6,7)]
                primary=(d/'dev/mtd2').read_bytes()
                process,(state,)=status_result(self.exe,d,'return_oem')
                self.assertEqual(state['phase'],'failed')
                self.assertTrue(state['may_have_modified'])
                self.assertFalse(state['can_reboot'])
                self.assertFalse(state['oem']['boot_records_verified'])
                self.assertIn('return_replay_rc=-1',process.stdout)
                self.assertEqual((d/'dev/mtd2').read_bytes(),primary)
                for idx,data in zip((6,7),appsbl):self.assertEqual((d/f'dev/mtd{idx}').read_bytes(),data)

    def test_unknown_root_witness_and_ambiguous_cmdline_cannot_guess_slot(self):
        for change in ('missing_ubi','wrong_root_mount','contradictory_mount','duplicate_ubi','wrong_alias','proc_sysfs_name_conflict','malformed_proc_mtd'):
            with self.subTest(change=change):
                d=self.fixture()
                if change=='missing_ubi':(d/'sys/class/ubi/ubi0/mtd_num').unlink()
                elif change=='wrong_root_mount':write_proc_mounts(d,'/dev/root / tmpfs rw 0 0\n')
                elif change=='contradictory_mount':write_proc_mounts(d,'/dev/ubiblock0_3 / squashfs ro,rw 0 0\n')
                elif change=='malformed_proc_mtd':
                    p=d/'proc/mtd';p.write_text(p.read_text()+'malformed partition record\n')
                elif change=='proc_sysfs_name_conflict':
                    p=d/'proc/mtd';p.write_text(p.read_text().replace('mtd15: 02800000 00020000 "rootfs_1"','mtd15: 02800000 00020000 "wrong_alias"'))
                elif change=='duplicate_ubi':
                    p=d/'proc/cmdline';p.write_text(p.read_text().strip()+' ubi.mtd=15\n')
                else:(d/'sys/class/mtd/mtd15/name').write_text('rootfs\n')
                _,(state,)=status_result(self.exe,d)
                self.assertEqual(state['phase'],'blocked')
                self.assertFalse(state['install_ready'])
                self.assertFalse(state['actions']['return_oem'])
                self.assertFalse(state['may_have_modified'])

    def test_oem_crc_or_invisible_immutable_digest_corruption_blocks_preflight(self):
        for pos in (2*131072+4096,5*131072+4096):
            d=self.fixture()
            with (d/'dev/mtd15').open('r+b') as f:f.seek(pos);f.write(b'\x13')
            _,(state,)=status_result(self.exe,d)
            self.assertEqual(state['phase'],'blocked')
            self.assertFalse(state['actions']['return_oem'])
            self.assertFalse(state['may_have_modified'])

    def test_sparse_writable_oem_volume_is_valid_without_official_runtime_digest(self):
        import test_oem_slot as oslot
        d=self.fixture()
        with (d/'dev/mtd15').open('r+b') as f:
            f.seek(7*131072);f.write(oslot.block(4,4,b'SYNTHETIC writable configuration'))
        _,(state,)=status_result(self.exe,d)
        self.assertEqual(state['phase'],'ready',state['blocked_reason'])
        self.assertTrue(state['oem']['slot_verified'])

    def test_oem_or_mapping_changes_between_preflight_and_commit_freeze_before_writes(self):
        for mode in ('stale_oem','stale_oem_map','stale_root_witness'):
            d=self.fixture()
            startup={idx:(d/f'dev/mtd{idx}').read_bytes() for idx in (2,3,6,7)}
            _,(state,)=status_result(self.exe,d,mode)
            self.assertEqual(state['phase'],'failed',mode)
            self.assertFalse(state['may_have_modified'])
            self.assertFalse(state['can_reboot'])
            for idx,data in startup.items():self.assertEqual((d/f'dev/mtd{idx}').read_bytes(),data)

    def test_oem_change_after_appsbl_write_stops_before_either_bc(self):
        d=self.fixture(fault='oem_pause_before_bc')
        before=[(d/f'dev/mtd{idx}').read_bytes() for idx in (2,3)]
        _,(state,)=status_result(self.exe,d,'pause_before_oem_change')
        self.assertEqual(state['phase'],'failed')
        self.assertTrue(state['may_have_modified'])
        self.assertFalse(state['can_reboot'])
        self.assertEqual((d/'dev/mtd6').read_bytes(),(d/'etc/bridge/uboot.bin').read_bytes())
        for idx,data in zip((2,3),before):self.assertEqual((d/f'dev/mtd{idx}').read_bytes(),data)

    def test_oem_change_after_backup_bc_prevents_primary_commit_and_rollback(self):
        d=self.fixture(fault='oem_pause_after_bc1')
        primary=(d/'dev/mtd2').read_bytes()
        _,(state,)=status_result(self.exe,d,'pause_after_bc1_oem_change')
        self.assertEqual(state['phase'],'failed')
        self.assertTrue(state['may_have_modified'])
        self.assertFalse(state['can_reboot'])
        self.assertEqual((d/'dev/mtd2').read_bytes(),primary)
        backup=(d/'dev/mtd3').read_bytes()
        self.assertEqual(struct.unpack_from('<I',backup,4)[0],3)
        self.assertEqual(struct.unpack_from('<I',backup,0x80)[0],0)

    def test_changed_oem_or_bc_after_complete_prevents_manual_reboot(self):
        for mode in ('pre_reboot_oem_change','pre_reboot_bc_change'):
            d=self.fixture()
            process,states=status_result(self.exe,d,mode)
            self.assertEqual(states[0]['phase'],'complete')
            self.assertEqual(states[-1]['phase'],'failed')
            self.assertFalse(states[-1]['can_reboot'])
            self.assertIn('oem_reboot_rc=-1',process.stdout)
            self.assertNotIn('reboot_simulated',process.stdout)

    def test_generic_reboot_is_not_verified_oem_return(self):
        d=self.fixture()
        process,_=status_result(self.exe,d,'generic_reboot')
        self.assertIn('generic_reboot_rc=-1',process.stdout)
        self.assertNotIn('reboot_simulated',process.stdout)

    def test_bridge_on_physical_a_selects_independently_verified_oem_b(self):
        d=self.fixture()
        for idx in (2,3):(d/f'dev/mtd{idx}').write_bytes(bc_image(rootfs=0))
        make_oem_fixture(d,root_selector=0)
        process,states=status_result(self.exe,d,'install')
        self.assertEqual(states[0]['phase'],'complete',states[0]['blocked_reason'])
        self.assertEqual(states[0]['oem_return']['physical_offset'],0x3100000)
        self.assertEqual(states[0]['oem_return']['selector'],1)
        self.assertEqual(struct.unpack_from('<I',(d/'dev/mtd2').read_bytes(),0x80)[0],1)
        self.assertEqual(states[-1]['stage'],'reboot_simulated')

    def test_already_installed_skips_appsbl_but_selects_verified_oem(self):
        d=self.fixture(active_payload=True)
        low=(d/"dev/mtd6").read_bytes();high=(d/"dev/mtd7").read_bytes()
        process,states=status_result(self.exe,d,"install")
        self.assertEqual(states[0]["phase"],"complete",states[0]["blocked_reason"])
        self.assertEqual(states[0]["stage"],"identical_oem_selected")
        self.assertTrue(states[0]["may_have_modified"])
        self.assertEqual((d/"dev/mtd6").read_bytes(),low)
        self.assertEqual((d/"dev/mtd7").read_bytes(),high)
        bc=(d/"dev/mtd2").read_bytes()
        self.assertEqual(struct.unpack_from("<I",bc,0x58)[0],1)
        self.assertEqual(struct.unpack_from("<I",bc,0x80)[0],0)
        self.assertEqual(states[-1]["stage"],"reboot_simulated")

if __name__ == "__main__":
    unittest.main()
