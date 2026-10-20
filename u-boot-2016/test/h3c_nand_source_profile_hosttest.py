#!/usr/bin/env python3
"""Compile the production QPIC source-profile selector against exact catalog variants."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
src = (root / "drivers/mtd/nand/qpic_nand.c").read_text()
match = re.search(
    r"static int qpic_nand_select_source_profile\s*\([^;]*?\)\s*\{", src, re.S
)
if not match:
    raise SystemExit("production source-profile selector missing")
start = src.index("{", match.start())
depth = 0
for end in range(start, len(src)):
    if src[end] == "{":
        depth += 1
    elif src[end] == "}":
        depth -= 1
        if depth == 0:
            selector = src[match.start():end + 1]
            break
else:
    raise SystemExit("unterminated source-profile selector")
update_match = re.search(
    r"static void qpic_serial_update_dev_params\s*\([^;]*?\)\s*\{", src, re.S
)
if not update_match:
    raise SystemExit("production QPIC parameter updater missing")
update_start = src.index("{", update_match.start())
depth = 0
for update_end in range(update_start, len(src)):
    if src[update_end] == "{": depth += 1
    elif src[update_end] == "}":
        depth -= 1
        if depth == 0:
            updater = src[update_match.start():update_end + 1]
            break
else:
    raise SystemExit("unterminated QPIC parameter updater")

prelude = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
struct qpic_serial_nand_params { u8 id[4]; u16 page_size, pgs_per_blk; u32 spare_size, erase_blk_size; u16 no_of_blocks; u32 density, otp_region; u8 no_of_addr_cycle, no_of_dies, num_bits_ecc_correctability, timing_mode_support; bool quad_mode, check_quad_config; int prev_die_id; u8 protec_bpx; u64 pages_per_die; const char *name; };
struct qpic_nand_dev { u32 page_size, block_size, num_blocks, widebus, density, spare_size, num_pages_per_blk, num_pages_per_blk_mask, timing_mode_support; bool quad_mode, check_quad_config; };
struct mtd_info { struct qpic_nand_dev *priv; u32 writesize, erasesize, oobsize, ecc_strength; u64 size; };
#define MTD_QPIC_NAND_DEV(mtd) ((mtd)->priv)
static struct qpic_serial_nand_params *serial_params;
"""
# Declare the exact fixed-width type referenced by the production table API.
prelude = prelude.replace("typedef uint32_t u32;", "typedef uint32_t u32; typedef uint64_t u64;")

cases = [
    ("h3c-oem-a1a5-128m-2k-sp64-p2048-e131072", 0x08, 4, 4, (0xa1, 0xa5), "FM25LS01", 1),
    ("h3c-oem-a1a5-128m-2k-sp128-p2048-e131072", 0x02, 8, 8, (0xa1, 0xa5), "FM25LS01", 1),
    ("h3c-oem-a1a5-128m-2k-sp64-p2048-e131072", 0x02, 4, 4, (0xa1, 0xa5), "FM25LS01", -22),
    ("h3c-oem-e521-128m-2k-sp128", 0x04, 4, 4, (0xe5, 0x21), "DS35M1GA", 1),
    ("h3c-oem-c845-128m-2k-sp128", 0x04, 4, 4, (0xc8, 0x45), "GD5F4GQ6RExxG", 1),
    ("h3c-oem-c845-128m-2k-sp128", 0x04, 8, 8, (0xc8, 0x45), "GD5F4GQ6RExxG", -22),
    ("h3c-oem-c845-128m-2k-sp128", 0x04, 4, 8, (0xc8, 0x45), "GD5F4GQ6RExxG", -22),
]
with tempfile.TemporaryDirectory(prefix="h3c-nand-source-profile-") as td:
    for i, (profile, source, ecc, storage_ecc, ident, model, expected) in enumerate(cases):
        ids = ",".join(hex(v) for v in ident)
        blocks = 4096 if profile.startswith("h3c-oem-c845-") else 1024
        expected_size = blocks * 2048 * 64
        program = prelude + f"""
#define CONFIG_H3C_NAND_PROFILE_ID \"{profile}\"
#define CONFIG_H3C_NAND_SOURCE_MASK {source}U
#define CONFIG_H3C_NAND_LOAD_ECC_BITS {ecc}U
#define CONFIG_H3C_NAND_STORAGE_ECC_BITS {storage_ecc}U
#include <h3c_nand_catalog.h>
static const struct h3c_nand_catalog_profile *qpic_source_profile;
{selector}
{updater}
int main(void) {{
 struct qpic_serial_nand_params p = {{ .id = {{{ids}}}, .page_size=2048, .pgs_per_blk=64, .no_of_blocks={blocks}, .spare_size=64, .num_bits_ecc_correctability=4, .name = "{model}" }};
 struct qpic_nand_dev dev = {{0}}; struct mtd_info mtd = {{ .priv=&dev }};
 serial_params = &p;
 {{ int ret = qpic_nand_select_source_profile(&p); if (ret != {expected}) fprintf(stderr, "case got %d profile=%s source=%x ecc=%u model=%s id=%x:%x\\n", ret, CONFIG_H3C_NAND_PROFILE_ID, CONFIG_H3C_NAND_SOURCE_MASK, CONFIG_H3C_NAND_LOAD_ECC_BITS, p.name, p.id[0], p.id[1]); assert(ret == {expected}); }}
 if ({expected} > 0) {{
  assert(qpic_source_profile && !strcmp(qpic_source_profile->profile_id, CONFIG_H3C_NAND_PROFILE_ID));
  qpic_serial_update_dev_params(&mtd);
  assert(mtd.oobsize == qpic_source_profile->appsbl_raw_spare_bytes);
  assert(mtd.ecc_strength == CONFIG_H3C_NAND_STORAGE_ECC_BITS);
  assert(mtd.size == {expected_size}ULL);
 }}
 return 0;
}}
"""
        src_path = pathlib.Path(td) / f"case-{i}.c"
        exe_path = pathlib.Path(td) / f"case-{i}"
        src_path.write_text(program)
        subprocess.run([
            "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
            f"-I{root / 'include'}", str(src_path), "-o", str(exe_path),
        ], check=True)
        subprocess.run([str(exe_path)], check=True)

# An unpinned image may use a cataloged chip only when one exact row agrees
# with the existing descriptor. Duplicate FM25LS01 and stale DS35M1GA QPIC
# geometry are rejected; C8:45 keeps its full 512 MiB die capacity.
legacy = prelude + f"""
#define CONFIG_H3C_NAND_PROFILE_ID ""
#define CONFIG_H3C_NAND_SOURCE_MASK 0U
#define CONFIG_H3C_NAND_LOAD_ECC_BITS 0U
#define CONFIG_H3C_NAND_STORAGE_ECC_BITS 0U
#include <h3c_nand_catalog.h>
static const struct h3c_nand_catalog_profile *qpic_source_profile;
{selector}
int main(void) {{
 (void)serial_params;
 struct qpic_serial_nand_params fm = {{ .id={{0xa1,0xa5}}, .name="FM25LS01", .page_size=2048, .pgs_per_blk=64, .no_of_blocks=1024, .spare_size=128, .num_bits_ecc_correctability=8 }};
 struct qpic_serial_nand_params ds = {{ .id={{0xe5,0x21}}, .name="DS35M1GA", .page_size=2048, .pgs_per_blk=64, .no_of_blocks=1024, .spare_size=64, .num_bits_ecc_correctability=4 }};
 struct qpic_serial_nand_params c845 = {{ .id={{0xc8,0x45}}, .name="GD5F4GQ6RExxG", .page_size=2048, .pgs_per_blk=64, .no_of_blocks=4096, .spare_size=128, .num_bits_ecc_correctability=4 }};
 assert(qpic_nand_select_source_profile(&fm) == -EINVAL);
 assert(qpic_nand_select_source_profile(&ds) == -EINVAL);
 assert(qpic_nand_select_source_profile(&c845) == 0);
 return 0;
}}
"""
with tempfile.TemporaryDirectory(prefix="h3c-nand-legacy-profile-") as td:
    legacy_src = pathlib.Path(td) / "legacy.c"
    legacy_exe = pathlib.Path(td) / "legacy"
    legacy_src.write_text(legacy)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    f"-I{root / 'include'}", str(legacy_src), "-o", str(legacy_exe)], check=True)
    subprocess.run([str(legacy_exe)], check=True)
print("PASS source-bound QPIC selector/runtime override and fail-closed legacy fallback: duplicate IDs, exact source/ECC, DS35M1GA, C8:45")
