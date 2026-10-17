from pathlib import Path
src=(Path(__file__).resolve().parents[1]/"board/qca/arm/common/failsafe.c").read_text()
def body(name):
    start=src.index(name)
    begin=src.index("{",start)
    depth=0
    for i in range(begin,len(src)):
        if src[i]=="{": depth+=1
        elif src[i]=="}":
            depth-=1
            if depth==0:return src[start:i+1]
    raise AssertionError("unterminated "+name)
validate=body("static int recovery_fit_validate_blob")
upload=body("int h3c_hw_a_recovery_fit_validate")
confirm=body("int h3c_hw_a_recovery_fit_confirm")
boot=body("int h3c_hw_a_recovery_fit_boot")
memory = body("static int recovery_fit_memory_matches")
for token in ["fdt_check_full(fit, size)", "fdt_check_full(fdt_copy, fdt_size)", "malloc(fdt_size)", "free(fdt_copy)", "fit_check_format", "!fit_image_verify(fit, kernel)", "!fit_image_verify(fit, ramdisk)", "!fit_image_verify(fit, fdt)", "recovery_fit_sha256_required", "fdt_check_full(fit, size)", "H3C_RECOVERY_FIT_CONFIG", "rdinit=/init"]:
    assert token in validate, token
for token in ["qcom,ipq5018-mp03.1", "gd->ram_size != 0x20000000UL", "0x40000000U", "0x20000000U"]:
    assert token in memory, token
#
hash_body = body("static int recovery_fit_sha256_required")
assert '"ignore"' in hash_body and '"sha256"' in hash_body and "SHA256_SUM_LEN" in hash_body
assert "httpd_upload_buffer_base()" in upload
assert "recovery_upload_range_valid(addr, size, upload_base" in upload
assert "memcpy((void *)H3C_RECOVERY_FIT_STAGE, upload, size)" in upload
assert "recovery_sha256((void *)H3C_RECOVERY_FIT_STAGE" in upload
assert "recovery_upload_addr" in confirm and "recovery_stage_sha256" in confirm
assert "recovery_fit_validate_blob" in confirm
assert "recovery_run_diagnostic_boot(command)" in boot
assert "bootm 0x%lx#%s" in boot
env_boot = body("static int recovery_run_diagnostic_boot")
assert 'setenv("bootargs", "console=ttyMSM0,115200,n8 rdinit=/init")' in env_boot
assert 'setenv("initrd_high", "0x48000000")' in env_boot
assert 'setenv("fdt_high", "0x48000000")' in env_boot
assert "recovery_env_restore" in env_boot and "saveenv" not in env_boot
range_body = body("static int recovery_upload_range_valid")
for token in ["SZ_64K", "H3C_RECOVERY_FIT_MAX_SIZE", "addr > ~0UL - size", "end <= ram_end"]:
    assert token in range_body, token
print("HW-A recovery FIT source guard: PASS")

validate_image = body("int failsafe_validate_image")
assert "H3C_HW_A_RECOVERY_FIT_INVALID_INFO" in validate_image
assert "ret ? \"fail\" : \"success\"" in validate_image
