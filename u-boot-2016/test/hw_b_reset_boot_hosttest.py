#!/usr/bin/env python3
"""Compile actual HW-B button and startup-env snippets with finite host mocks."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
api = (root / "lib/ipq_api.c").read_text()
kconfig = (root / "board/ipq5018/Kconfig").read_text()
b_target = kconfig[
    kconfig.index("config TARGET_IPQ5018_H3C_RT3000_B"):
    kconfig.index("config TARGET_IPQ5018_H3C_HW_A")
]
assert "select H3C_DEFAULT_WEB" in b_target

def extract_function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[start:pos + 1]
    raise ValueError("unterminated function: " + signature)

typedef_start = api.index("typedef struct {", api.index("// GPIO 帮助函数"))
typedef_end = api.index("} button_info_t;", typedef_start) + len("} button_info_t;")
button_info = api[typedef_start:typedef_end]
button_functions = "\n\n".join(extract_function(api, signature) for signature in (
    "static bool is_button_pressed(",
    "static bool is_any_button_pressed(",
    "static bool is_any_button_pressed_for_enough_time(",
))
network_start = api.index("#elif defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)", api.index("void do_network_check(void)"))
network_end = api.index("\n#elif", network_start)
network_block = api[network_start:network_end].replace(
    "#elif defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)",
    "#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)", 1) + "\n#endif"

source = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
typedef unsigned long ulong;
#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#define GPIO_NOT_FOUND 0xffffffffU
#define GPIO_ACTIVE_LOW 1U
#define GPIO_OUT_LOW 0U
#define GPIO_OUT_HIGH 1U

typedef struct { void *fdt_blob; } mock_gd_t;
static mock_gd_t test_gd;
#define gd (&test_gd)

struct mock_key { const char *name; unsigned int gpio; unsigned int active_level; };
static struct mock_key keys[4];
static int keys_present, key_count;
static ulong now_ms;
static int pressed_gpio = -1;
static long release_at_ms = -1;
static int saveenv_calls;
static unsigned int mock_gpio_get_value(unsigned int gpio) {
    if ((int)gpio == pressed_gpio && (release_at_ms < 0 || now_ms < (ulong)release_at_ms))
        return GPIO_OUT_LOW;
    return GPIO_OUT_HIGH;
}
static void mock_udelay(unsigned int usec) { now_ms += usec / 1000; }
static ulong mock_get_timer(ulong base) { return now_ms - base; }
static int fdt_path_offset(const void *fdt, const char *path) {
    (void)fdt;
    return (!strcmp(path, "/keys") && keys_present) ? 0 : -1;
}
#define fdt_for_each_subnode(fdt, node, parent) for ((node) = 0; (node) < key_count; ++(node))
static const char *fdt_get_name(const void *fdt, int node, int *len) {
    (void)fdt; (void)len;
    return node >= 0 && node < key_count ? keys[node].name : NULL;
}
static unsigned int fdtdec_get_uint(const void *fdt, int node, const char *property, unsigned int default_value) {
    (void)fdt;
    if (node < 0 || node >= key_count) return default_value;
    if (!strcmp(property, "gpio")) return keys[node].gpio;
    if (!strcmp(property, "active_level")) return keys[node].active_level;
    return default_value;
}
static void led_all_off(void) {}
static void led_off(const char *name) { (void)name; }
static void led_on(const char *name) { (void)name; }
static int mock_putc(int c) { return c; }
#define gpio_get_value mock_gpio_get_value
#define udelay mock_udelay
#define get_timer mock_get_timer
#define putc(c) mock_putc(c)
""" + button_info + "\n" + button_functions + r"""

static char bootcmd_value[96], bootdelay_value[32], ethact_value[32];
static int bootcmd_exists, bootdelay_exists, ethact_exists;
static const char *mock_getenv(const char *key) {
    if (!strcmp(key, "bootcmd")) return bootcmd_exists ? bootcmd_value : NULL;
    if (!strcmp(key, "bootdelay")) return bootdelay_exists ? bootdelay_value : NULL;
    if (!strcmp(key, "ethact")) return ethact_exists ? ethact_value : NULL;
    return NULL;
}
static int mock_setenv(const char *key, const char *value) {
    char *destination = NULL;
    int *exists = NULL;
    size_t capacity = 0;
    if (!strcmp(key, "bootcmd")) { destination = bootcmd_value; exists = &bootcmd_exists; capacity = sizeof(bootcmd_value); }
    else if (!strcmp(key, "bootdelay")) { destination = bootdelay_value; exists = &bootdelay_exists; capacity = sizeof(bootdelay_value); }
    else if (!strcmp(key, "ethact")) { destination = ethact_value; exists = &ethact_exists; capacity = sizeof(ethact_value); }
    else return -1;
    if (!value) { *exists = 0; destination[0] = '\0'; return 0; }
    snprintf(destination, capacity, "%s", value);
    *exists = 1;
    return 0;
}
#define getenv mock_getenv
#define setenv mock_setenv
#define saveenv() (saveenv_calls++, 0)
static void run_hw_b_env_block(void) {
""" + network_block + r"""
}

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
static void reset_inputs(void) {
    now_ms = 0; pressed_gpio = -1; release_at_ms = -1;
    keys_present = 1; key_count = 1;
    keys[0] = (struct mock_key){"reset", 34, GPIO_ACTIVE_LOW};
}
static void reset_env(void) {
    bootcmd_exists = bootdelay_exists = ethact_exists = 0;
    bootcmd_value[0] = bootdelay_value[0] = ethact_value[0] = '\0';
    saveenv_calls = 0;
}
int main(void) {
    button_info_t found;
    reset_inputs(); pressed_gpio = 34;
    CHECK(is_any_button_pressed_for_enough_time());
    CHECK(now_ms >= 3000);

    reset_inputs(); pressed_gpio = 34; release_at_ms = 150;
    CHECK(!is_any_button_pressed_for_enough_time());
    CHECK(now_ms < 3000);

    reset_inputs(); key_count = 2;
    keys[0] = (struct mock_key){"wps", 38, GPIO_ACTIVE_LOW};
    keys[1] = (struct mock_key){"reset", 34, GPIO_ACTIVE_LOW};
    pressed_gpio = 38;
    CHECK(!is_any_button_pressed(&found));
    CHECK(!is_any_button_pressed_for_enough_time());
    CHECK(now_ms == 0);

    reset_inputs(); keys_present = 0; pressed_gpio = 34;
    CHECK(!is_any_button_pressed(&found));
    CHECK(!is_any_button_pressed_for_enough_time());
    CHECK(now_ms == 0);

    reset_env();
    run_hw_b_env_block();
    CHECK(bootcmd_exists && !strcmp(bootcmd_value, "bootipq"));
    CHECK(bootdelay_exists && !strcmp(bootdelay_value, "3"));
    CHECK(ethact_exists && !strcmp(ethact_value, "eth1"));
    CHECK(saveenv_calls == 0);

    reset_env(); bootcmd_exists = 1; bootcmd_value[0] = '\0';
    run_hw_b_env_block();
    CHECK(!strcmp(bootcmd_value, "bootipq") && saveenv_calls == 0);

    reset_env(); bootcmd_exists = 1; strcpy(bootcmd_value, "httpd");
    run_hw_b_env_block();
    CHECK(!strcmp(bootcmd_value, "bootipq") && saveenv_calls == 0);

    reset_env(); bootcmd_exists = 1; strcpy(bootcmd_value, "run custom_boot");
    run_hw_b_env_block();
    CHECK(!strcmp(bootcmd_value, "run custom_boot") && saveenv_calls == 0);

    puts("HW-B RESET boot host tests: PASS");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="hw-b-reset-boot-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(source)
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-parameter", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


# Bind the automatic slot-selector assertions to the actual BOOTCONFIG reader.
bootconfig = (root / "lib/bootconfig.c").read_text()
bootconfig_functions = "\n\n".join(extract_function(bootconfig, signature) for signature in (
    "int read_bootconfig(",
    "int read_bootconfig_and_write_back_if_needed(",
    "int get_bootconfig(",
))
bootconfig_source = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef unsigned long ulong;
#define CONFIG_TARGET_IPQ5018_H3C_RT3000_B 1
#include <bootconfig.h>
#define SMEM_BOOT_NAND_FLASH 1
#define SMEM_BOOT_ONENAND_FLASH 2
#define SMEM_BOOT_QSPI_NAND_FLASH 3
#define SMEM_BOOT_NOR_FLASH 4
#define SMEM_BOOT_NORPLUSEMMC 5
#define SMEM_BOOT_NORPLUSNAND 6
#define SMEM_BOOT_SPI_FLASH 7
#define SMEM_BOOT_MMC_FLASH 8
#define SMEM_BOOT_NO_FLASH 9
#define SMEM_BOOT_SDC_FLASH 10
typedef struct { int flash_type; } qca_smem_flash_info_t;
static qca_smem_flash_info_t qca_smem_flash_info = {SMEM_BOOT_NAND_FLASH};
static const qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
static struct bootconfig_info primary_cfg, backup_cfg;
static int primary_present = 1, backup_present = 1;
static int write_calls;
int get_bootconfig_part_offset(const char *name) {
    if (!strcmp(name, BOOTCONFIG_PART_NAME)) return primary_present ? 0 : -ENOENT;
    if (!strcmp(name, BOOTCONFIG_BACKUP_PART_NAME)) return backup_present ? (int)sizeof(struct bootconfig_info) : -ENOENT;
    return -ENOENT;
}
static int read_data_from_nand(ulong offset, size_t size, void *out, size_t capacity) {
    const struct bootconfig_info *src;
    if (size > capacity || size != sizeof(struct bootconfig_info)) return -EINVAL;
    if (offset == 0 && primary_present) src = &primary_cfg;
    else if (offset == sizeof(struct bootconfig_info) && backup_present) src = &backup_cfg;
    else return -ENOENT;
    memcpy(out, src, size);
    return 0;
}
int write_bootconfig(bootconfig_info_t *cfg) { (void)cfg; write_calls++; return 0; }
static int read_data_from_spi(ulong offset, size_t size, void *out, size_t capacity) { (void)offset; (void)size; (void)out; (void)capacity; return -ENOSYS; }
static int read_data_from_mmc(ulong offset, size_t size, void *out, size_t capacity) { (void)offset; (void)size; (void)out; (void)capacity; return -ENOSYS; }
""" + bootconfig_functions + r"""
#define CHECK_BC(expr) do { if (!(expr)) { fprintf(stderr, "BOOTCONFIG FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
static void valid_config(struct bootconfig_info *cfg, unsigned int selector) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic_start = BOOTCONFIG_MAGIC_START;
    cfg->numaltpart = 1;
    strcpy(cfg->per_part_entry[0].name, "rootfs");
    cfg->per_part_entry[0].primaryboot = selector;
    cfg->magic_end = BOOTCONFIG_MAGIC_END;
}
int main(void) {
    valid_config(&primary_cfg, 0); valid_config(&backup_cfg, 0);
    CHECK_BC(get_bootconfig("rootfs") == 0 && write_calls == 0);
    valid_config(&primary_cfg, 1);
    CHECK_BC(get_bootconfig("rootfs") == 1 && write_calls == 0);

    valid_config(&primary_cfg, 0);
    strcpy(primary_cfg.per_part_entry[1].name, "rootfs");
    primary_cfg.per_part_entry[1].primaryboot = 1;
    primary_cfg.numaltpart = 2;
    CHECK_BC(get_bootconfig("rootfs") == -EINVAL && write_calls == 0);

    valid_config(&primary_cfg, 0); primary_cfg.numaltpart = NUM_ALT_PARTITION + 1;
    valid_config(&backup_cfg, 1); backup_cfg.numaltpart = NUM_ALT_PARTITION + 1;
    CHECK_BC(get_bootconfig("rootfs") == -EINVAL && write_calls == 0);

    /* A valid backup may be read into RAM; the HW-B path must not repair primary. */
    valid_config(&primary_cfg, 0); primary_cfg.magic_start = 0;
    valid_config(&backup_cfg, 1);
    CHECK_BC(get_bootconfig("rootfs") == 1 && write_calls == 0);
    CHECK_BC(primary_cfg.magic_start == 0);

    puts("HW-B BOOTCONFIG selector host tests: PASS");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="hw-b-bootconfig-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(bootconfig_source)
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare", "-I" + str(root / "include"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
