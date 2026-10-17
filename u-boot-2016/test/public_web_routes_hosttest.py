#!/usr/bin/env python3
"""Static contract for every public page reachable in production builds."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
FSDATA = ROOT / "failsafe/fsdata"
MAIN = (FSDATA / "main.js").read_text(encoding="utf-8")
MISC = (ROOT / "failsafe/modules/misc.c").read_text(encoding="utf-8")

ROUTES = {
    "/": "sysinfo",
    "/index.html": "sysinfo",
    "/cgi-bin/luci": "sysinfo",
    "/cgi-bin/luci/": "sysinfo",
    "/syslog.html": "syslog",
    "/firmware.html": "firmware",
    "/firmware-dual.html": "firmware-dual",
    "/firmware-single.html": "firmware-single",
    "/art.html": "art",
    "/cdt.html": "cdt",
    "/ptable.html": "ptable",
    "/simg.html": "simg",
    "/uboot.html": "uboot",
    "/mibib.html": "mibib",
    "/initramfs.html": "initramfs",
    "/settings.html": "settings",
    "/network.html": "network",
    "/mac.html": "mac",
    "/env.html": "env",
    "/backup.html": "backup",
    "/webterm.html": "webterm",
    "/reboot.html": "reboot",
}
NAV_ROUTES = {
    "/", "/syslog.html", "/firmware.html", "/art.html", "/cdt.html",
    "/ptable.html", "/simg.html", "/uboot.html", "/mibib.html",
    "/initramfs.html", "/settings.html", "/network.html", "/mac.html",
    "/env.html", "/backup.html", "/webterm.html", "/reboot.html",
}
DYNAMIC_UPLOAD = {
    "/firmware.html", "/firmware-dual.html", "/firmware-single.html",
    "/art.html", "/cdt.html", "/ptable.html", "/simg.html",
    "/uboot.html", "/mibib.html", "/initramfs.html",
}

registered = set(re.findall(r'httpd_register_uri_handler\(inst,\s*"([^"]+)"', MISC))
missing = sorted(set(ROUTES) - registered)
assert not missing, "public pages missing server handlers: " + ", ".join(missing)
assert {"/main.js", "/style.css", "/version"} <= registered

path_map = re.search(r"const pathMap = \{(.*?)\n\s*\};", MAIN, re.S)
assert path_map, "Sidebar pathMap not found"
mapped = set(re.findall(r"['\"](/[^'\"]*)['\"]\s*:", path_map.group(1)))
assert set(ROUTES) <= mapped, "pathMap misses: " + ", ".join(sorted(set(ROUTES) - mapped))

nav_block = re.search(r"getNavigationConfig\(\) \{(.*?)\n\s*\}\n\s*\}\n", MAIN, re.S)
assert nav_block, "navigation configuration not found"
nav = set(re.findall(r"path:\s*['\"](/[^'\"]*)['\"]", nav_block.group(1)))
reboot = re.search(r"getRebootConfig\(\).*?path:\s*['\"](/[^'\"]+)['\"]", MAIN, re.S)
assert reboot
nav.add(reboot.group(1))
assert nav == NAV_ROUTES, f"navigation contract changed: missing={sorted(NAV_ROUTES-nav)} extra={sorted(nav-NAV_ROUTES)}"

for route in ("/firmware-dual.html", "/firmware-single.html"):
    assert route in MAIN, route + " is not reachable from firmware mode UI"

for obsolete in (
    "支持经识别的 H3C OEM 固件包和完整 UBI 系统镜像（含内核和系统卷）；QSDK sysupgrade tar 不支持。",
    "Accepted inputs are recognized H3C OEM packages and complete UBI system images (kernel and system volumes). QSDK sysupgrade tar files are not supported.",
    "仅接受经识别的 H3C OEM 固件包或完整 UBI 系统镜像（含内核和系统卷）；QSDK sysupgrade tar 不支持。",
    "Only recognized H3C OEM packages and complete UBI system images (kernel and system volumes) are accepted; QSDK sysupgrade tar is unsupported.",
):
    assert obsolete not in MAIN, "firmware mode page still renders the removed format footer"

for warning in (
    "刷写过程中请勿断电。",
    "请选择与设备型号和当前分区布局匹配的固件，并在写入前确认目标分区。",
    "Do not power off the device during update.",
    "Choose firmware matching the device model and active partition layout, then confirm the target partition before writing.",
):
    assert warning in MAIN, "firmware mode warnings missing: " + warning

page_configs = re.search(r"const pageConfigs = \{(.*?)\n\};", MAIN, re.S)
assert page_configs, "pageConfigs not found"
for page in set(ROUTES.values()) - {"reboot"}:
    token = page + ":" if re.match(r"^[A-Za-z_$][\w$]*$", page) else "'" + page + "':"
    assert token in page_configs.group(1), "pageConfigs misses " + page

static_routes = set(ROUTES) - DYNAMIC_UPLOAD
for route in sorted(static_routes):
    name = "index.html" if route in {"/", "/index.html", "/cgi-bin/luci", "/cgi-bin/luci/"} else route[1:]
    path = FSDATA / name
    assert path.is_file(), f"static page missing: {name}"
    text = path.read_text(encoding="utf-8")
    page = ROUTES[route]
    assert re.search(r"appInit\(\s*['\"]" + re.escape(page) + r"['\"]\s*\)", text), f"{name} does not initialize {page}"
    assert not re.search(r"http-equiv\s*=\s*['\"]refresh", text, re.I), f"public page redirects by meta refresh: {name}"

not_found = (FSDATA / "404.html").read_text(encoding="utf-8")
assert re.search(r"http-equiv\s*=\s*['\"]refresh", not_found, re.I)

guard = re.search(
    r"#if\s+(.*?)\n\s*httpd_register_uri_handler\(inst, \"/firmware-dual\.html\".*?\n"
    r"\s*httpd_register_uri_handler\(inst, \"/firmware-single\.html\".*?\n#endif",
    MISC, re.S,
)
assert guard, "firmware mode handler guard not found"
for macro in (
    "CONFIG_TARGET_IPQ5018_H3C_RT3000_B",
    "CONFIG_H3C_HW_A_PRODUCTION_PORT",
    "CONFIG_H3C_HW_C_PRODUCTION_PORT",
    "CONFIG_H3C_HW_D_PRODUCTION_PORT",
):
    assert macro in guard.group(1), "production route guard misses " + macro

print(f"PASS {len(ROUTES)} public page routes, navigation, appInit, handlers and no production meta-refresh")
