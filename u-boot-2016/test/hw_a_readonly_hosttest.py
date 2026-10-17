#!/usr/bin/env python3
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
env = (root / "failsafe/modules/env.c").read_text()
http = (root / "net/httpd.c").read_text()
mainjs = (root / "failsafe/fsdata/main.js").read_text()
identity_test = (root / "test/sysinfo_board_identity_ui_test.js").read_text()
backup = (root / "failsafe/modules/backup.c").read_text()
board = (root / "board/qca/arm/common/board_init.c").read_text()
mac = (root / "board/qca/arm/common/ethaddr.c").read_text()
api = (root / "lib/ipq_api.c").read_text()
main = (root / "common/main.c").read_text()
gmac = (root / "drivers/net/ipq5018/ipq5018_gmac.c").read_text()
sysinfo = (root / "failsafe/modules/sysinfo.c").read_text()
qpic = (root / "drivers/mtd/nand/qpic_nand.c").read_text()
dts = (root / "arch/arm/dts/ipq5018-h3c-hw-a.dts").read_text()
config = (root / "configs/ipq5018_h3c_hw_a_defconfig").read_text()
assert "failsafe_env_export_ram_text(&out_len)" in env
assert "hwalk_r(&env_htab, failsafe_env_append_entry)" in env
assert "CONFIG_TARGET_IPQ5018_H3C_HW_A" in env
assert "read_only" in env and "persistent_write_supported" in env
assert "CONFIG_TARGET_IPQ5018_H3C_HW_A" in sysinfo[:sysinfo.index("/* SMEM info */")]
assert 'board_family' in sysinfo and 'identity_source' in sysinfo
assert 'const familyOnly = board.family === "HW-A" || board.family === "HW-B";' in mainjs
assert "family: 'HW-A'" in identity_test and "legacy-model" in identity_test
assert "gpio = <20>;" in dts and "gpio = <20>;\n\t\t\t\tfunc = <1>;\n\t\t\t\tpull = <GPIO_PULL_UP>;" in dts
assert "saveenv = NULL" in board
assert "# CONFIG_CMD_SAVEENV is not set" in config
mac_start = mac.index("#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)")
mac_end = mac.index("#else", mac_start)
mac_a = mac[mac_start:mac_end]
assert "eth_getenv_enetaddr" in mac_a and "smem_getpart" not in mac_a and "eth_setenv_enetaddr" not in mac_a
setter = mac[mac.index("void set_ethmac_addr(void)"):]
assert "#ifdef CONFIG_TARGET_IPQ5018_H3C_HW_A" in setter and "return;" in setter
assert api.count("defined(CONFIG_H3C_HW_A_RAM_READONLY)") >= 2
assert "!defined(CONFIG_TARGET_IPQ5018_H3C_HW_A)" in main
assert "gephy_led" not in dts
for callback in ("qpic_nand_write_page", "qpic_nand_mark_badblock", "qpic_nand_write_oob", "qpic_nand_blk_erase", "qpic_nand_erase"):
    start = qpic.index(callback + "(")
    end = qpic.find("\n}", start)
    body = qpic[start:end]
    guard = body.find("h3c_hw_a_nand_write_rejected()")
    assert guard >= 0 and "CONFIG_H3C_HW_A_RAM_READONLY" in body[:guard]
    for side_effect in ("qpic_add_wr_page_cws_data_desc", "qpic_nand_add_wr_page_cws_cmd_desc", "NAND_CMD_ERASE", "qpic_reg_write_bam"):
        if side_effect in body:
            assert guard < body.index(side_effect), (callback, side_effect)
assert "CONFIG_TARGET_IPQ5018_H3C_HW_A" in qpic and "mtd->flags &= ~MTD_WRITEABLE" in qpic
assert "CONFIG_TARGET_IPQ5018_H3C_HW_A" in qpic[qpic.index("/* start serial training here */"):]
assert "skip serial training; keep 50 MHz default" in qpic
assert "CONFIG_TARGET_IPQ5018_H3C_HW_A" in (root / "include/qpic_nand_model.h").read_text()
assert "CONFIG_TARGET_IPQ5018_H3C_HW_A" in sysinfo and "qpic_nand_get_model_info(&model)" in sysinfo
assert 'qpic_nand_get_model_info(&model)' in sysinfo and 'model_id' in sysinfo and 'ecc_strength' in sysinfo
# NOR device/model disclosure remains disabled on HW-A.
spi_model = sysinfo.index("spi_nor")
assert "CONFIG_TARGET_IPQ5018_H3C_RT3000_B" in sysinfo[max(0, spi_model - 400):spi_model]
assert "CONFIG_TARGET_IPQ5018_H3C_HW_A" in gmac and "if (ret < 0)" in gmac
assert "Network defaults updated in RAM only" in api
assert "Failsafe setting cleared in RAM only" in api
assert "h3c_hw_a_request_rejected(int is_get, int is_post" in (root / "include/h3c_hw_a_readonly.h").read_text()
assert 'req->method == HTTP_POST' in http
assert "smem_read_data" in backup and "block_read" in backup and "spi_flash_read" in backup
for write_api in ("block_write", "mtd_write", "nand_write", "spi_flash_write", "erase_info", "saveenv"):
    assert write_api not in backup, write_api
assert "httpd_register_uri_handler(inst, \"/backup\", &backup_handler" in backup
policy = http.index("h3c_hw_a_request_rejected(req->method == HTTP_GET,")
dispatch = http.index("req->urih = httpd_find_uri_handler(inst, pdata->uri)")
assert policy < dispatch and "req->urih = NULL" in http[policy:dispatch]
assert "req->method == HTTP_POST" in http[policy:dispatch]
assert "pdata->response.info.code = 403" in http[policy:dispatch]
with tempfile.TemporaryDirectory(prefix="hw-a-readonly-") as tmp:
    exe = pathlib.Path(tmp) / "readonly-test"
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-DCONFIG_TARGET_IPQ5018_H3C_HW_A", "-DCONFIG_H3C_HW_A_RAM_READONLY", "-iquote", str(root / "include"), str(root / "test/hw_a_readonly_hosttest.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("HW-A read-only gate, POST /backup allowlist, NAND identity, and HW-A family integration: PASS")
