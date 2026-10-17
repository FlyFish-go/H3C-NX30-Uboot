from pathlib import Path

root = Path(__file__).resolve().parents[1]
board = (root / "board/qca/arm/common/failsafe.c").read_text()
upgrade = (root / "failsafe/modules/upgrade.c").read_text()
httpd = (root / "net/httpd.c").read_text()

validate = board[board.index("int failsafe_validate_image("):
                 board.index("int failsafe_write_image(")]
assert "case WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS:" in validate
assert "ret=failsafe_validate_initramfs(data_addr, data_size);" in validate
assert "read_only_stage0" in validate
assert "rt3_metadata_validate(md_kind" in validate
switch = validate[validate.index("switch (upgrade_type)"):
                  validate.index("if (md_kind)")]
assert "case WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS:" in switch
assert "default:" in switch and "ret=-EROFS;" in switch

validator = board[board.index("static int __maybe_unused failsafe_validate_initramfs("):
                  board.index("// =====", board.index("static int __maybe_unused failsafe_validate_initramfs("))]
assert 'fw_type != FW_TYPE_FIT' in validator
assert "rt3_hw_b_initramfs_range_valid" in validator
assert "rt3_hw_b_fit_extent_valid" in validator
assert "fit_check_format(data_addr, data_size)" in validator

boot = board[board.index("int boot_from_mem("):
             board.index("int failsafe_validate_image(")]
b_branch = boot[boot.index("#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B"):
                boot.index("#else")]
assert "rt3_hw_b_stage_initramfs" in b_branch
assert 'snprintf(rcmd, sizeof(rcmd), "bootm 0x%lx"' in b_branch
assert "config_select(" not in b_branch
assert b_branch.index("rt3_hw_b_ram_bootargs") < b_branch.index("rt3_hw_b_stage_initramfs")
assert b_branch.index("rt3_hw_b_stage_initramfs") < b_branch.index("setenv(\"bootargs\", ram_bootargs)")
assert b_branch.index("setenv(\"bootargs\", ram_bootargs)") < b_branch.index("run_command(rcmd, 0)")
assert "setenv(\"bootargs\", original_bootargs)" in b_branch
assert "saveenv" not in b_branch

write = board[board.index("int failsafe_write_image("):
              board.index("if (ret)", board.index("int failsafe_write_image("))]
read_only_pos = write.index("read_only_stage0")
readonly_at = write.rfind("strlcpy(info", 0, read_only_pos)
write_b = write[readonly_at:write.index("#else", readonly_at)]
assert "read_only_stage0" in write_b
assert "ret = -EROFS;" in write_b
assert "failsafe_write_firmware(" not in write_b
assert "failsafe_write_uboot(" not in write_b

result = upgrade[upgrade.index("if (upload_data_id == fs_upload_id)"):
                 upgrade.index("#else", upgrade.index("if (upload_data_id == fs_upload_id)"))]
assert "failsafe_initramfs_is_validated((ulong)upload_data" in result
assert "st->auto_reboot = true;" in result
assert "failsafe_initramfs_invalidate();" in result
initramfs_result = result[result.index("else if (upgrade_type == WEBFAILSAFE_UPGRADE_TYPE_INITRAMFS)"):]
initramfs_result = initramfs_result[:initramfs_result.index("} else {")]
assert "failsafe_write_image(upgrade_type" not in initramfs_result

mismatch = upgrade[upgrade.index("upload_id_mismatch"):
                   upgrade.index("Headers were already streamed")]
assert "failsafe_initramfs_invalidate();" in mismatch
assert "rt3_metadata_invalidate();" in mismatch
assert "metadata_kind_for_upgrade(upgrade_type,&kind)" in result
assert "metadata_commit_token(request,token)" in result
assert "rt3_metadata_write(kind,token,response)" in result
upload_cb = upgrade[upgrade.index("static void upload_handler"):
                    upgrade.index("static void result_handler")]
assert "failsafe_initramfs_invalidate();" in upload_cb
assert "request->form.count != 1" in upload_cb

assert "rt3_hw_b_http_content_length_allowed" in httpd
assert '!strcmp(pdata->uri, "/upload")' in httpd
request_upload = httpd[httpd.index("pdata->uri = uri_ptr;"):
                        httpd.index("/* find required fields", httpd.index("pdata->uri = uri_ptr;"))]
assert "failsafe_initramfs_invalidate();" in request_upload
assert "rt3_metadata_invalidate();" in request_upload
print("HW-B web initramfs production-path checks passed")
