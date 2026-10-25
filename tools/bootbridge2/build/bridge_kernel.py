"""Pinned official FIT -> multi-config bridge DT transformation, offline only.

No kernel instruction patch, hardware classification, installation permission or
OEM package generation. Only reviewed reference routes receive bridge DT edits.
"""
if not __debug__:
    raise RuntimeError("bridge kernel audit requires assertions; Python -O is unsupported")

import ctypes
import ctypes.util
import hashlib
import struct
import zlib

import vendor_fit

PROFILE = "H3C-BOOTBRIDGE-MULTICONFIG-V1"
BOOTARGS = b"console=ttyMSM0,115200,n8 ro init=/sbin/init\0"
MAX_FIT_BYTES = 16 * 1024 * 1024
SOURCES = {
    "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922":
        ("NX30V100R012", "5878e39213f37fb3d26b51a5197e09b05e7de18ee275fd7f430ea6eac5e7dc85"),
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44":
        ("RC3000V100R013L01", "49cf5230bf616278f16092cb229f8f50ba39cde98b0fa1fb0f8ae655c2341f17"),
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b":
        ("RT3000V100R015", "e3aedc22bd099accffb3654c136df5111b3210ef2b9b638ebcff31b73f13ac61"),
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5":
        ("RW3000V100R012", "f0d33b7c5f94fe5f9afad60a80fa9cf8221d5030fa579f7993ce5c1292d67f06"),
}
ROUTES = {
    "config@mp02.1": ("fdt@mp02.1", "qcom,ipq5018-mp02.1",
        "1249f5dab08f413e11410935a919218ac73a8daaf2c611abd5fe91b33d8ec9bc"),
    "config@mp03.1": ("fdt@mp03.1", "qcom,ipq5018-mp03.1",
        "3e25cc4f30d539930d3274b66967576024999ad660972151a89ba8008924a930"),
    "config@mp03.1-c2": ("fdt@mp03.1-c2", "qcom,ipq5018-mp03.1-c2",
        "eb652c979c3e2026a49b6791ad6e10c28d70c70f4151d744ec5f380ff5dedcad"),
}
CONFIG_NAMES = frozenset((
    "mp03.5-c2", "mp03.6-c2", "emulation-c3", "mp03.1", "db-mp02.1", "mp03.3-c2",
    "mp03.3", "mp03.5-c1", "mp03.6-c1", "sod", "db-mp03.1-c2", "tb-mp04",
    "mp03.3-c3", "db-mp03.1", "emulation-c2", "mp03.4-c2", "db-mp03.3-c2",
    "db-mp03.3-c3", "mp02.1", "mp03.1-c2", "mp03.1-c3", "mp03.4-c1",
    "emulation-c1", "db-mp03.3"))
DISABLED = {
    "/soc/spi@78b5000/m25p80@0": b"n25q128a11\0",
    "/soc/wifi@c000000": b"qcom,cnss-qca5018\0qcom,ipq5018-wifi\0",
    "/soc/wifi1@c000000": b"qcom,cnss-qcn6122\0qcom,cnss-qcn9100\0",
    "/soc/wifi2@c000000": b"qcom,cnss-qcn6122\0qcom,cnss-qcn9100\0",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def _string(data):
    if not isinstance(data, bytes) or not data.endswith(b"\0") or b"\0" in data[:-1]:
        raise ValueError("expected one terminated FIT string")
    return data[:-1].decode("ascii")


def _digest(data, algo):
    if algo == "crc32":
        return struct.pack(">I", zlib.crc32(data) & 0xffffffff)
    if algo in ("sha1", "sha256"):
        return hashlib.new(algo, data).digest()
    raise ValueError("unreviewed FIT digest algorithm")


def _parse(data):
    if not isinstance(data, bytes) or not 40 <= len(data) <= MAX_FIT_BYTES:
        raise ValueError("invalid bounded FIT length")
    if struct.unpack_from(">II", data) != (0xd00dfeed, len(data)):
        raise ValueError("FIT header or trailing bytes differ from exact image")
    return vendor_fit.fdt_props(data)


def _verify(data):
    nodes = _parse(data)
    for path, props in nodes.items():
        if not (path.startswith("/images/") and path.count("/") == 2):
            continue
        if "data" not in props:
            raise ValueError("FIT image lacks internal data")
        hashes = [p for n, p in nodes.items() if n.startswith(path + "/") and "algo" in p]
        if not hashes:
            raise ValueError("FIT image lacks declared digest")
        for item in hashes:
            if item.get("value") != _digest(props["data"], _string(item["algo"])):
                raise ValueError("FIT image digest mismatch")
    return nodes


class _FDT:
    def __init__(self, data, extra):
        self.lib = ctypes.CDLL(ctypes.util.find_library("fdt") or "libfdt.so.1")
        for name, args in {
            "fdt_open_into": [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int],
            "fdt_path_offset": [ctypes.c_void_p, ctypes.c_char_p],
            "fdt_setprop": [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p,
                            ctypes.c_void_p, ctypes.c_int],
            "fdt_pack": [ctypes.c_void_p],
        }.items():
            getattr(self.lib, name).argtypes = args
            getattr(self.lib, name).restype = ctypes.c_int
        self.buffer = ctypes.create_string_buffer(len(data) + extra)
        source = ctypes.create_string_buffer(data)
        self._require(self.lib.fdt_open_into(source, self.buffer, len(self.buffer)))

    @staticmethod
    def _require(result):
        if result < 0:
            raise ValueError("libfdt rejected structure: %d" % result)
        return result

    def set(self, path, name, value):
        offset = self._require(self.lib.fdt_path_offset(self.buffer, path.encode("ascii")))
        data = ctypes.create_string_buffer(value)
        self._require(self.lib.fdt_setprop(self.buffer, offset, name.encode("ascii"), data, len(value)))

    def bytes(self):
        self._require(self.lib.fdt_pack(self.buffer))
        return self.buffer.raw[:struct.unpack_from(">I", self.buffer.raw, 4)[0]]


def _changes(before, after):
    if set(before) != set(after):
        raise ValueError("unexpected DT/FIT node insertion or deletion")
    return {(path, key) for path in before for key in set(before[path]) | set(after[path])
            if before[path].get(key) != after[path].get(key)}


def _transform_dt(data, compatible, expected_sha):
    if sha(data) != expected_sha:
        raise ValueError("unreviewed reference DTB bytes")
    before = _parse(data)
    expected_compatible = compatible.encode("ascii") + b"\0qcom,ipq5018\0"
    if before["/"].get("compatible") != expected_compatible:
        raise ValueError("reference route compatible mismatch")
    if before.get("/chosen", {}).get("bootargs") != b"console=ttyMSM0,115200,n8 rw init=/init\0":
        raise ValueError("unreviewed chosen bootargs")
    if "h3c,bridge-profile" in before["/"]:
        raise ValueError("already transformed or unexpected profile")
    for path, expected in DISABLED.items():
        if before.get(path, {}).get("compatible") != expected:
            raise ValueError("unreviewed bridge child compatible: " + path)
    # Other stock CNSS radios must already be disabled; do not silently enable
    # or guess modifications for a newly active radio on a new reference DTB.
    for path, props in before.items():
        if b"cnss" in props.get("compatible", b"") and path not in DISABLED:
            if props.get("status") != b"disabled\0":
                raise ValueError("unexpected additional active CNSS radio")
    if before.get("/soc/spi@78b5000", {}).get("status") not in (b"ok\0", b"okay\0"):
        raise ValueError("shared SPI parent has unreviewed state")
    dt = _FDT(data, 4096)
    for path in DISABLED:
        dt.set(path, "status", b"disabled\0")
    dt.set("/", "h3c,bridge-profile", PROFILE.encode("ascii") + b"\0")
    dt.set("/chosen", "bootargs", BOOTARGS)
    result = dt.bytes()
    after = _parse(result)
    expected = {(p, "status") for p in DISABLED} | {("/", "h3c,bridge-profile"), ("/chosen", "bootargs")}
    changes = _changes(before, after)
    if changes != expected:
        raise ValueError("DT transformation changed unexpected properties")
    return result, [{"node": p, "property": k,
                     "before_hex": before[p].get(k, b"").hex(),
                     "after_hex": after[p][k].hex()} for p, k in sorted(changes)]


def transform_kernel(stock_fit, source_official_sha256):
    """Return (transformed FIT bytes, audit report), with no filesystem IO.

    source_official_sha256 chooses a reviewed whole-package identity; exact
    kernel FIT SHA is independently bound to it before parsing. Caller must
    verify the original ordinary package SHA and actual extraction. An already
    transformed FIT is deliberately rejected, rather than silently normalized.
    """
    identity = SOURCES.get(source_official_sha256)
    if identity is None:
        raise ValueError("unknown official source identity")
    if not isinstance(stock_fit, bytes) or sha(stock_fit) != identity[1]:
        raise ValueError("kernel FIT is not the pinned official original; repeat transform refused")
    before = _verify(stock_fit)
    configs = {p: v for p, v in before.items() if p.startswith("/configurations/") and p.count("/") == 2}
    names = {"config@" + name for name in CONFIG_NAMES}
    if {p.rsplit("/", 1)[1] for p in configs} != names:
        raise ValueError("unreviewed configuration layout")
    image_names = {p.rsplit("/", 1)[1] for p in before if p.startswith("/images/") and p.count("/") == 2}
    if image_names != {"kernel@1"} | {"fdt@" + name for name in CONFIG_NAMES}:
        raise ValueError("unreviewed image layout")
    for path, props in configs.items():
        suffix = path.rsplit("@", 1)[1]
        if _string(props.get("kernel")) != "kernel@1" or _string(props.get("fdt")) != "fdt@" + suffix:
            raise ValueError("unreviewed config references")
    if _string(before.get("/configurations", {}).get("default")) not in names:
        raise ValueError("unreviewed FIT default")
    fit = _FDT(stock_fit, 65536)
    route_reports = []
    allowed = set()
    for config, (image, compatible, expected_sha) in ROUTES.items():
        path = "/images/" + image
        original_dt = before[path]["data"]
        new_dt, changes = _transform_dt(original_dt, compatible, expected_sha)
        fit.set(path, "data", new_dt)
        allowed.add((path, "data"))
        for hash_path, props in before.items():
            if hash_path.startswith(path + "/") and "algo" in props:
                fit.set(hash_path, "value", _digest(new_dt, _string(props["algo"])))
                allowed.add((hash_path, "value"))
        route_reports.append({"config": config, "fdt": image,
            "original_dtb_sha256": sha(original_dt), "bridge_dtb_sha256": sha(new_dt),
            "changes": changes, "nand_network_properties_preserved": True,
            "hardware_classification": "unknown; reference route is not a hardware provider"})
    output = fit.bytes()
    after = _verify(output)
    if _changes(before, after) != allowed:
        raise ValueError("FIT changed outside the three DTB data/hash properties")
    untouched = sorted(image_names - {x[0] for x in ROUTES.values()})
    for image in untouched:
        if before["/images/" + image] != after["/images/" + image]:
            raise ValueError("untouched FIT image changed")
    report = {"schema": 1, "profile": PROFILE, "source_identity": identity[0],
        "source_official_sha256": source_official_sha256, "stock_fit_sha256": sha(stock_fit),
        "output_fit_sha256": sha(output), "config_count": 24, "image_count": 25,
        "default_preserved": _string(before["/configurations"]["default"]),
        "all_configuration_properties_preserved": True,
        "untouched_dtb_count": 21, "untouched_image_names": untouched,
        "kernel_compressed_sha256": sha(before["/images/kernel@1"]["data"]),
        "kernel_image_byte_identical": True, "ecc_kernel_binary_patch": False,
        "repeat_policy": "reject: only exact pinned original FIT accepted",
        "routes": route_reports, "hardware": "NOT_EXECUTED",
        "installation": "NOT_QUALIFIED", "B_D_same_machid_classification": "UNKNOWN"}
    return output, report
