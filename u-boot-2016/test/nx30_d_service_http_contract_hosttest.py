#!/usr/bin/env python3
"""Contract checks for public slot UI, HW-D service routes, and safe reads."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
httpd = (root / "net/httpd.c").read_text()
handler = (root / "failsafe/modules/service_hwd.c").read_text()
limits = (root / "include/net/httpd_upload_limits.h").read_text()
main_js = (root / "failsafe/fsdata/main.js").read_text()
misc = (root / "failsafe/modules/misc.c").read_text()
slots_module = (root / "failsafe/modules/slots_rt3000_b.c").read_text()
default_module = (root / "failsafe/modules/boot_default_rt3000_b.c").read_text()
policy = (root / "lib/nx30_d_service.c").read_text()
oem_catalog = (root / "failsafe/modules/slots_oem_catalog.h").read_text()
d_oem_catalog = (root / "failsafe/modules/slots_nx30_d_oem.h").read_text()

registered = dict(re.findall(
    r'httpd_register_uri_handler\(inst,\s*"([^"]+)"\s*,\s*(\w+)', handler))
get_routes = {
    "/service/status", "/service/buttons", "/service/slots", "/service/env",
    "/service/network/info", "/service/boot/default/failure",
}
post_routes = {
    "/service/boot/slot", "/service/backup", "/service/led",
    "/service/env/prepare", "/service/env/commit", "/service/env/abort",
    "/service/network/ram", "/service/boot/default/prepare",
    "/service/boot/default/commit", "/service/boot/default/abort",
    "/service/firmware/validate", "/service/firmware/write",
    "/service/uboot/validate", "/service/uboot/write", "/service/reboot",
    "/service/console",
}
assert set(registered) == get_routes | post_routes, set(registered)

def callback_body(name):
    start = handler.index("static void " + name + "(")
    end = handler.find("\nstatic ", start + 1)
    return handler[start:end if end >= 0 else len(handler)]

for route in get_routes:
    body = callback_body(registered[route])
    assert re.search(r"!request\s*\|\|\s*request->method\s*!=\s*HTTP_GET", body), route
for route in post_routes - {"/service/firmware/validate", "/service/firmware/write",
                             "/service/uboot/validate", "/service/uboot/write"}:
    body = callback_body(registered[route])
    assert re.search(r"!request\s*\|\|\s*request->method\s*!=\s*HTTP_POST", body), route
firmware = callback_body("d_firmware_handler")
assert "d_upload_begin(response)" in firmware
assert re.search(r"!request\s*\|\|\s*request->method\s*!=\s*HTTP_POST", firmware)
uboot = callback_body("d_uboot_handler")
assert "d_upload_begin(response)" in uboot
assert re.search(r"!request\s*\|\|\s*request->method\s*!=\s*HTTP_POST", uboot)
assert "d_uboot_handler(status, request, response, false)" in callback_body("d_uboot_validate_handler")
assert "d_uboot_handler(status, request, response, true)" in callback_body("d_uboot_write_handler")

assert 'httpd_register_uri_handler(inst, "/main.js", &js_handler' in misc
assert 'httpd_register_uri_handler(inst, "/index.html", &index_handler' in misc
assert 'httpd_register_uri_handler(inst, "/boot/slots", boot_slots_handler' in slots_module
assert 'httpd_register_uri_handler(inst, "/boot/default", default_handler' in default_module
assert "NX30_D_SERVICE_FIRMWARE_UPLOAD_LIMIT" in limits
assert "NX30_D_SERVICE_UBOOT_UPLOAD_LIMIT" in limits
for route in ("firmware/validate", "firmware/write", "uboot/validate", "uboot/write"):
    assert f'!strcmp(uri, "/service/{route}")' in limits
assert 'strncmp(uri, "/service/", 9)' not in limits
for route in ("env/prepare", "env/commit", "env/abort", "network/ram", "backup",
              "console", "led", "boot/slot", "boot/default/prepare",
              "boot/default/commit", "boot/default/abort", "reboot"):
    assert re.search(rf'if\s*\(!strcmp\(uri, "/service/{re.escape(route)}"\)\)\s*return length <= 2048U', limits)
upload_buffer = httpd.split("static void *httpd_get_upload_buffer_for_uri", 1)[1]
upload_buffer = upload_buffer.split("#elif defined(CONFIG_H3C_HW_D_NOR_RESCUE)", 1)[0]
for route in ("firmware/validate", "firmware/write", "uboot/validate", "uboot/write"):
    assert f'!strcmp(uri, "/service/{route}")' in upload_buffer
request_parser = httpd.split("static int httpd_handle_request", 1)[1]
parse_later = request_parser.split('if (!strcmp(pdata->uri, "/upload")', 1)[1]
parse_later = parse_later.split("if (!parse_post_data_later)", 1)[0]
assert '"/service/firmware/validate"' in parse_later
assert '"/service/uboot/validate"' in parse_later
assert '"/service/firmware/write"' not in parse_later
assert '"/service/uboot/write"' not in parse_later
assert 'response->info.content_type = "application/json";' in handler
assert 'httpd_register_uri_handler(inst, "/service/backup", d_backup_handler' in handler
assert 'nx30_d_read_object(object, offset, window, want, &got)' in handler
assert 'nx30_d_object_extent(object, &extent)' in handler
assert "d_backup_extent_current(object, generation, extent)" in handler
assert "d_backup_extent_current(session->object" in handler
assert "layout.generation != generation" in handler
assert "session->generation = layout.generation" in handler
assert 'sha256_update(&ctx, window, (u32)got)' in handler
assert 'X-NX30-SHA256:' in handler and 'ECC-decoded-no-OOB' in handler
assert 'appsbl-high' in handler and 'appsbl-low' in handler
for unsafe in ("saveenv", "nand_write", "nand_erase", "/dev/mtd"):
    assert unsafe not in handler, unsafe
assert 'run_command("reset", 0)' in handler
for command in ('"version"', '"bdinfo"', '"nand info"', '"mtd list"'):
    assert command in handler, command
assert 'static char d_service_json' not in handler
assert 'response->session_data = session;' in handler
assert 'httpd_register_uri_handler(inst, "/service/buttons", d_buttons_handler' in handler
assert "D_UPDATE_TOKEN_TTL_MS" in handler
assert "d_pending_update_get(0, token, file->data, file->size)" in handler
assert "d_pending_update_get(1, token, file->data, file->size)" in handler
assert "d_upload_finish(response, session)" in handler
assert 'httpd_register_uri_handler(inst, "/service/led", d_led_handler' in handler
backup_names = set(re.findall(r'!strcmp\(name, "([^"]+)"\)',
                              handler[handler.index("static int d_backup_object("):handler.index("static int d_backup_extent_current(")]))
assert backup_names == {
    "env", "bc0", "bc1", "bc-original0", "bc-original1", "appsbl-high",
    "appsbl-low", "art", "training", "slot-a", "slot-b", "sbl1", "mibib",
    "qsee", "qsee-1", "devcfg", "devcfg-1", "cdt", "cdt-1", "pdt-data",
    "pdt-data-1", "exp", "plugin",
}
assert "NX30_D_OBJ_PLUGIN" in handler and "NX30_D_OBJ_SBL1" in handler
assert "d_mutation_unavailable_handler" not in handler
assert "HTTP_CB_CLOSED" in handler and "D_AFTER_BOOT_SLOT" in handler
assert "slots_rt3000_b_invalidate_cache();" in handler
assert 'httpd_register_uri_handler(inst, "/service/boot/default/abort", d_default_abort_handler' in handler
assert "nx30_d_default_abort(token)" in handler
assert 'fetch(\'/service/boot/default/abort\'' not in main_js
ram_status = handler.split("#ifdef CONFIG_H3C_HW_D_SERVICE_RAM", 1)[1].split("#else", 1)[0]
normal_status = handler.split("#ifdef CONFIG_H3C_HW_D_SERVICE_RAM", 1)[1].split("#else", 1)[1].split("#endif", 1)[0]
for status_branch in (ram_status, normal_status):
    assert r'\"read_only\":false' in status_branch
    for capability in ("env_transaction", "default_slot", "firmware_write"):
        assert rf'\"{capability}\":true' in status_branch
status_handler = handler[handler.index("static void d_status_handler("):handler.index("static void d_slots_handler(")]
assert "appsbl_active = 0;" in status_handler
assert "nx30_d_uboot_high_copy_active()" not in status_handler
assert r'\"mutations_ready\":%s' in handler
assert re.search(r'(?:layout\.ecc_bits == 4U && slots\.ecc_bits == 4U|slots\.ecc_bits == 4U && layout\.ecc_bits == 4U)', handler)
default_prepare_start = policy.index("int nx30_d_default_prepare(")
default_prepare_end = policy.index("\nint nx30_d_default_commit(", default_prepare_start)
default_prepare = policy[default_prepare_start:default_prepare_end]
assert default_prepare.index("l.ecc_bits!=4U") < default_prepare.index("nx30_d_slots_get(&slots)")
assert default_prepare.index("slots.ecc_bits!=4U") < default_prepare.index("original[0]=memalign")
assert default_prepare.count("ret=-EOPNOTSUPP") >= 2
ui_start = main_js.index("const bootSlotsManager = (() => {")
ui_end = main_js.index("// Physical button status on RT3000-B", ui_start)
slot_ui = main_js[ui_start:ui_end]
assert "fetch('/boot/slots', { cache: 'no-store' })" in slot_ui
assert "fetch('/boot/default', { cache: 'no-store' })" in slot_ui
assert "const form = new FormData();" in slot_ui
assert "form.append('target', slot.name);" in slot_ui
assert "fetch('/boot/default', { method: 'POST', body: form })" in slot_ui
assert "saved.can_change !== true" in slot_ui and "data.read_only === true" in slot_ui
assert "Physical " in slot_ui and "slot.physical_index" in slot_ui
assert "slot.offset" in slot_ui and "slot.size" in slot_ui
assert 'httpd_register_uri_handler(inst, "/boot/slots", boot_slots_handler' in slots_module
assert "known_nx30_d_oem" in slots_module and "known_oem" in slots_module
assert "NX30 R012" in oem_catalog and "NX30 R004" in d_oem_catalog

subprocess.run(["node", "test/boot_default_ui_test.js"], cwd=root, check=True)
subprocess.run([sys.executable, "test/slots_rt3000_b_get_hosttest.py"], cwd=root, check=True)
subprocess.run([sys.executable, "test/boot_default_transaction_hosttest.py"], cwd=root, check=True)
subprocess.run([sys.executable, "test/nx30_d_bc_retained_hosttest.py"], cwd=root, check=True)
print("HW-D public main.js slot UI, service HTTP method/body/read/hash safety, and ECC4 mutation gates: PASS")
