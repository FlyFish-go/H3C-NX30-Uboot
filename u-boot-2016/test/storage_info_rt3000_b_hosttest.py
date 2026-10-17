#!/usr/bin/env python3
# Host checks for cached B storage identities; no hardware or probe side effects.
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
api = (ROOT / "lib/ipq_api.c").read_text()
qpic = (ROOT / "drivers/mtd/nand/qpic_nand.c").read_text()
sysinfo = (ROOT / "failsafe/modules/sysinfo.c").read_text()
api_start = api.index("typedef struct {\n\tbool spi;\n\tbool nand;\n\tbool mmc;")
api_end = api.index("// =============================================================================\n// flash_type enum", api_start)
api_chunk = api[api_start:api_end]
qpic_start = qpic.index("static bool qpic_serial_model_identified;")
qpic_end = qpic.index("#endif", qpic_start)
qpic_chunk = qpic[qpic_start:qpic_end]

c = r'''#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_SF_DEFAULT_BUS 0
#define CONFIG_SF_DEFAULT_CS 0
#define CONFIG_SF_DEFAULT_SPEED 1000000
#define CONFIG_SF_DEFAULT_MODE 0
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define DEV_TYPE_UNKNOWN 0
struct spi_flash { unsigned size; };
typedef struct { const char *name; unsigned size, writesize; } nand_info_t;
typedef struct { int type; } block_dev_desc_t;
struct { int dev_num; } mmc_host;
static nand_info_t nand_info[1];
static struct spi_flash flash_ok = { 0x100000 };
static struct spi_flash *probe_result;
static unsigned probe_calls;
static struct spi_flash *spi_flash_probe(unsigned bus, unsigned cs, unsigned speed, unsigned mode)
{ (void)bus; (void)cs; (void)speed; (void)mode; probe_calls++; return probe_result; }
static block_dev_desc_t *mmc_get_dev(int n) { static block_dev_desc_t d; (void)n; return &d; }
static size_t strlcpy(char *d, const char *s, size_t n)
{ size_t l = strlen(s); if (n) { size_t c = l < n-1 ? l : n-1; memcpy(d,s,c); d[c]=0; } return l; }
''' + api_chunk + r'''
struct qpic_serial_nand_params { unsigned char id[4]; const char *name; };
struct qpic_nand_model_info { const char *name; unsigned char id[3]; };
static struct qpic_serial_nand_params *serial_params;
''' + qpic_chunk + r'''
int main(void)
{
 struct qpic_nand_model_info mi;
 unsigned i;
 nand_info[0].name = "nand0"; nand_info[0].size = 0x8000000; nand_info[0].writesize = 2048;
 probe_result = &flash_ok;
 detect_flash_device();
 if (probe_calls != 1 || !has_spi() || !has_nand() || get_detected_spi_flash() != &flash_ok) return 1;
 for (i=0; i<100; i++) if (get_detected_spi_flash() != &flash_ok || !has_spi()) return 2;
 if (probe_calls != 1 || strcmp(nand_info[0].name, "nand0")) return 3;
 if (!qpic_nand_get_model_info(&mi)) return 4;
 static struct qpic_serial_nand_params model = {{0xc8,0xc2,0,0}, "GD5F1GQ5REYIG"};
 serial_params = &model; qpic_serial_model_identified = true;
 if (qpic_nand_get_model_info(&mi) || strcmp(mi.name, "GD5F1GQ5REYIG") || mi.id[0] != 0xc8 || mi.id[1] != 0xc2) return 5;
 qpic_serial_model_identified = false;
 if (!qpic_nand_get_model_info(&mi)) return 6;
 probe_result = NULL;
 detect_flash_device();
 if (probe_calls != 2 || has_spi() || get_detected_spi_flash() != NULL || !has_nand()) return 7;
 for (i=0; i<100; i++) if (get_detected_spi_flash() != NULL || has_spi()) return 8;
 if (probe_calls != 2 || strcmp(nand_info[0].name, "nand0")) return 9;
 puts("STORAGE_CACHE_PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="rt3-storage-hosttest-") as td:
    src = pathlib.Path(td) / "test.c"
    exe = pathlib.Path(td) / "test"
    src.write_text(c)
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

spi_start = sysinfo.index("#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B", sysinfo.index("/* SPI info */"))
spi_end = sysinfo.index("#else", spi_start)
b_spi = sysinfo[spi_start:spi_end]
assert "get_detected_spi_flash()" in b_spi and "spi_flash_probe(" not in b_spi
assert "spi_nor" in sysinfo and "chip_select" in sysinfo and "sector_size" in sysinfo
nand_start = sysinfo.index("struct qpic_nand_model_info model;")
nand_end = sysinfo.index("#endif", nand_start)
b_nand = sysinfo[nand_start:nand_end]
assert "nand->name" in sysinfo and "model_id" in b_nand and "qpic_nand_get_model_info(&model)" in b_nand
# Compile the exact NAND model formatter block and production json_escape().
def c_function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for end in range(brace, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start:end + 1]
    raise AssertionError("unterminated C function")

json_escape = c_function(api, "size_t json_escape(")
formatter_start = sysinfo.index("\t\t{\n\t\t\tstruct qpic_nand_model_info model;")
formatter_end = sysinfo.index("\n\t\t}", formatter_start) + len("\n\t\t}")
nand_formatter = sysinfo[formatter_start:formatter_end]
formatter_c = r'''#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
struct qpic_nand_model_info { const char *name; unsigned char id[3]; };
static const char *fixture_name;
static bool fixture_present = true;
int qpic_nand_get_model_info(struct qpic_nand_model_info *m)
{ if (!fixture_present) return -1; m->name = fixture_name; m->id[0]=0xc8; m->id[1]=0xc2; m->id[2]=0; return 0; }
''' + json_escape + r'''
static char buf[512];
static int len, left;
#define APPEND_JSON_TO_BUF(fmt, args...) do { len += snprintf(buf + len, left - len, fmt, ##args); } while (0)
static void render(void)
{
 len = snprintf(buf, sizeof(buf), "{\"present\":true"); left = sizeof(buf);
''' + nand_formatter + r'''
 APPEND_JSON_TO_BUF("}");
 puts(buf);
}
int main(void)
{
 fixture_name = "GD \"quoted\" \\ rev"; render();
 fixture_name = "plain-chip"; render();
 fixture_present = false; render();
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="rt3-nand-json-hosttest-") as td:
    src = pathlib.Path(td) / "formatter.c"
    exe = pathlib.Path(td) / "formatter"
    src.write_text(formatter_c)
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Wno-sign-compare", "-Werror", str(src), "-o", str(exe)], check=True)
    lines = subprocess.check_output([str(exe)], text=True).splitlines()
import json
quoted, plain, absent = [json.loads(line) for line in lines]
assert quoted["model"] == 'GD "quoted" \\ rev' and quoted["model_id"] == "c8:c2:00"
assert plain["model"] == "plain-chip" and plain["model_id"] == "c8:c2:00"
assert absent["model"] is None and absent["model_id"] is None
print("NAND_MODEL_JSON_PASS: production formatter escapes quote/backslash and repeated/null cases do not leak")

print("SYSINFO_CONTRACT_PASS: nand0 MTD name retained, driver model separate, SPI NOR uses cached configured probe")
