#!/usr/bin/env python3
"""Focused source contracts for C production RESET startup behavior."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
api=(root/'lib/ipq_api.c').read_text()
config=(root/'include/configs/ipq5018.h').read_text()
button=api[api.index('static bool is_any_button_pressed('):api.index('static bool is_any_button_pressed_for_enough_time(')]
assert '#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_H3C_HW_C_PRODUCTION_PORT)' in button
assert 'strcmp(name, "reset")' in button
network=api[api.index('void do_network_check(void)'):api.index('// =============================================================================\n// 闪存设备检测')]
cbranch=network[network.index('#elif defined(CONFIG_H3C_HW_C_PRODUCTION_PORT)'):network.index('#endif',network.index('#elif defined(CONFIG_H3C_HW_C_PRODUCTION_PORT)'))]
assert 'getenv("bootcmd")' in cbranch and 'strcmp(bootcmd, "httpd")' in cbranch
assert 'getenv("bootdelay")' in cbranch and 'strcmp(bootdelay, "0")' in cbranch
assert 'setenv("bootcmd", "bootipq")' in cbranch and 'setenv("bootdelay", "3")' in cbranch
assert 'saveenv' not in cbranch
assert '#if defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_A_PRODUCTION_PORT) || defined(CONFIG_H3C_HW_C_RAM_READONLY)' in config
default_branch=config[config.index('#if defined(CONFIG_H3C_HW_A_RAM_READONLY)'):config.index('#endif',config.index('#if defined(CONFIG_H3C_HW_A_RAM_READONLY)'))]
assert 'CONFIG_H3C_HW_C_PRODUCTION_PORT' not in default_branch
assert '#define CONFIG_BOOTCOMMAND\t\t\t"bootipq"' in default_branch
assert 'led_all_off();\n\tled_on("system_led");' in api
for name in ('ipq5018_h3c_rt3000_b_defconfig','ipq5018_h3c_hw_a_defconfig','ipq5018_h3c_hw_a_production_defconfig','ipq5018_h3c_hw_c_defconfig','ipq5018_h3c_hw_c_production_defconfig'):
    text=(root/'configs'/name).read_text()
    assert 'CONFIG_LOCALVERSION="-h3c-common2"' in text, name
print('C production RESET startup focused checks: PASS')

httpd=api[api.index('void do_httpd_check(void)'):api.index('// =============================================================================\n// 网络参数检查')]
assert '#ifdef CONFIG_H3C_HW_C_PRODUCTION_PORT' in httpd
assert httpd.index('setenv("ethact", "eth1")') < httpd.index('run_command("httpd", 0)')
