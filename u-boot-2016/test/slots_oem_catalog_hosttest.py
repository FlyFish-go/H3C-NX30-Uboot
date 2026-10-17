#!/usr/bin/env python3
"""Compile and exercise the exact OEM classifier functions on the host."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "u-boot-2016/failsafe/modules"
SOURCE = MODULE / "slots_rt3000_b.c"
DATA = ROOT / "doc/RT3000-B-OEM-CATALOG-20261008.json"
QSDK = "31115a4c01c29357f6ed1e27a1f5007ef6e25904342054be48542610c5605f74"
OPENWRT = "3b9092062481cd1a18d0cf0c5ea9ae3baf687b81ddc6716444b4e1e8bde6781e"


def main():
    source = SOURCE.read_text()
    begin = source.index("/* Return 1 when the hash is in the OEM catalog")
    end = source.index("static int env_save(", begin)
    classifier = source[begin:end]
    entries = json.loads(DATA.read_text())["entries"]
    assert len(entries) == 47
    checks = []
    for i, row in enumerate(entries):
        checks.append(
            ' memset(&s, 0, sizeof(s)); classify("%s", &s); '
            'if (strcmp(s.status,"known") || strcmp(s.type,"OEM") || '
            'strcmp(s.version,"%s") || strcmp(s.label,"%s") || '
            'strcmp(s.detection,"known_rootfs_sha256")) return %d;\n'
            % (row["rootfs_sha256"], row["version"], row["label"], i + 1)
        )
    changed = "0" + entries[0]["rootfs_sha256"][1:]
    checks.append(
        ' memset(&s, 0, sizeof(s)); classify("%s", &s); '
        'if (strcmp(s.status,"unknown") || strcmp(s.type,"unknown") || '
        'strcmp(s.version,"unknown") || strcmp(s.label,"Unknown system") || '
        'strcmp(s.detection,"unknown_rootfs_sha256")) return 80;\n' % changed)
    checks.append(
        ' memset(&s, 0, sizeof(s)); classify("%s", &s); '
        'if (strcmp(s.status,"known") || strcmp(s.type,"QSDK") || '
        'strcmp(s.version,"Product R1 Upgrade Preview") || '
        'strcmp(s.label,"QSDK Product R1 Upgrade Preview") || '
        'strcmp(s.detection,"known_rootfs_sha256")) return 81;\n' % QSDK)
    checks.append(
        ' memset(&s, 0, sizeof(s)); classify("%s", &s); '
        'if (strcmp(s.status,"unknown") || strcmp(s.label,"Unknown system")) return 82;\n'
        % ("f" * 64))
    checks.append(
        ' memset(&s, 0, sizeof(s)); classify("%s", &s); '
        'if (strcmp(s.status,"unknown") || strcmp(s.type,"unknown")) return 86;\n' % OPENWRT)
    checks.append(
        ' memset(&s, 0, sizeof(s)); classify_release_file(&s, "OpenWrt 21.02.7 r16847-f8282da11e"); '
        'if (strcmp(s.status,"known") || strcmp(s.type,"OpenWrt") || '
        'strcmp(s.version,"OpenWrt 21.02.7 r16847-f8282da11e") || '
        'strcmp(s.detection,"rootfs_release_file")) return 87;\n')
    harness = '''#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static size_t strlcpy(char *dst, const char *src, size_t size) {
 size_t n = strlen(src), copy = n < size - 1 ? n : size - 1;
 if (size) { memcpy(dst, src, copy); dst[copy] = 0; }
 return n;
}
struct slot_info { char status[16], type[16], version[97], label[96], detection[40]; };
struct known_image { const char *sha256, *version, *label; };
#include "slots_oem_catalog.h"
static const char *known_qsdk = "'''+QSDK+'''";
'''+classifier+'''
int main(void) {
 struct slot_info s;
'''+''.join(checks)+'''
 { const struct known_image same[] = {
   { "'''+entries[0]["rootfs_sha256"]+'''", "NX30 R777", "OEM NX30 R777" },
   { "'''+entries[0]["rootfs_sha256"]+'''", "NX30 R777", "OEM NX30 R777" },
  };
  memset(&s, 0, sizeof(s)); strcpy(s.status,"unknown"); strcpy(s.type,"unknown");
  strcpy(s.version,"unknown"); strcpy(s.label,"Unknown system"); strcpy(s.detection,"unknown_rootfs_sha256");
  if (!classify_oem("'''+entries[0]["rootfs_sha256"]+'''", &s, same, ARRAY_SIZE(same), known_qsdk) ||
      strcmp(s.status,"known") || strcmp(s.version,"NX30 R777")) return 83;
 }
 { const struct known_image conflict[] = {
   { "'''+entries[0]["rootfs_sha256"]+'''", "NX30 R777", "OEM NX30 R777" },
   { "'''+entries[0]["rootfs_sha256"]+'''", "RT3000 R777", "OEM RT3000 R777" },
  };
  memset(&s, 0, sizeof(s)); strcpy(s.status,"unknown"); strcpy(s.type,"unknown");
  strcpy(s.version,"unknown"); strcpy(s.label,"Unknown system"); strcpy(s.detection,"unknown_rootfs_sha256");
  if (!classify_oem("'''+entries[0]["rootfs_sha256"]+'''", &s, conflict, ARRAY_SIZE(conflict), known_qsdk) ||
      strcmp(s.status,"unknown") || strcmp(s.type,"unknown") || strcmp(s.version,"unknown") ||
      strcmp(s.label,"Ambiguous OEM image") || strcmp(s.detection,"ambiguous_oem_rootfs_sha256")) return 84;
 }
 { const struct known_image overlap[] = {
   { "'''+QSDK+'''", "NX30 R777", "OEM NX30 R777" },
  };
  memset(&s, 0, sizeof(s)); strcpy(s.status,"unknown"); strcpy(s.type,"unknown");
  strcpy(s.version,"unknown"); strcpy(s.label,"Unknown system"); strcpy(s.detection,"unknown_rootfs_sha256");
  if (!classify_oem("'''+QSDK+'''", &s, overlap, ARRAY_SIZE(overlap), known_qsdk) ||
      strcmp(s.status,"unknown") || strcmp(s.detection,"ambiguous_oem_rootfs_sha256")) return 85;
 }
 puts("PASS: 47 exact OEM hashes/package versions, QSDK hash fallback, release-file third-party identity, unknown handling and ambiguity guards");
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="oem-catalog-hosttest-") as temp:
        cfile, exe = Path(temp) / "test.c", Path(temp) / "test"
        cfile.write_text(harness)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(MODULE), str(cfile), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

        # Compile the production JSON formatter too, checking resolved fields,
        # null fallbacks and the fixed 2048-byte response bound.
        format_start = source.index("static void format_slots(")
        format_end = source.index("static int same_layout(", format_start)
        formatter = source[format_start:format_end]
        json_harness = '''#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define SLOT_N 2
#define RT3_LAYOUT_UNSUPPORTED 0
#define RT3_LAYOUT_DUAL 1
typedef uint64_t u64;
typedef uint32_t u32;
static size_t strlcpy(char *dst, const char *src, size_t size) {
 size_t n = strlen(src), copy = size && n < size - 1 ? n : (size ? size - 1 : 0);
 if (size) { memcpy(dst, src, copy); dst[copy] = 0; }
 return n;
}
struct rt3_layout { int mode; int count; u32 fingerprint; };
const char *rt3_layout_mode_name(int mode) { return mode == RT3_LAYOUT_DUAL ? "dual" : "unsupported"; }
struct slot_info {
 u64 offset, size; unsigned int display_index; char label[96], part_name[64];
 char status[16], type[16], version[97], detection[40], error[48];
};
struct known_image { const char *sha256, *version, *label; };
#include "slots_oem_catalog.h"
static const char *known_qsdk = "''' + QSDK + '''";
''' + classifier + '''
static struct slot_info slots[SLOT_N];
static char slots_json[2048];
static char escaped_part[SLOT_N][128];
static char escaped_label[SLOT_N][128];
static char escaped_version[SLOT_N][256];
static int environment_restored = 1;
#define SLOT_ACCESS_JSON "\\\"read_only\\":false,\\"boot_supported\\":true,"
static size_t json_escape(const char *src, char *dst, size_t size) {
 size_t i, j=0; if (!size) return 0;
 for (i=0; src[i] && j+1<size; i++) {
  if (src[i]=='"' || (unsigned char)src[i]==92) { if(j+2>=size) break; dst[j++]=92; dst[j++]=src[i]; }
  else dst[j++]=src[i];
 }
 dst[j]=0; return j;
}
''' + formatter + '''
static void init_slot(struct slot_info *s, const char *name, const char *hash, unsigned int index) {
 memset(s, 0, sizeof(*s)); strlcpy(s->part_name, name, sizeof(s->part_name));
 s->offset = 0x900000 + (index - 16) * 0x2800000; s->size = 0x2800000; s->display_index = index;
 classify(hash, s);
}
int main(void) {
 struct rt3_layout layout = { RT3_LAYOUT_DUAL, 2, 0x12345678 };
 init_slot(&slots[0], "rootfs", "df700e2f7f686259afa614bd93b078a1731170147aacf4ee459040142a310dbc", 16);
 init_slot(&slots[1], "rootfs_1", known_qsdk, 17);
 strlcpy(slots[0].label, "OEM RT3000 R005", sizeof(slots[0].label));
 classify_release_file(&slots[1], "OpenWrt \\04221.02.7\\134preview");
 format_slots(&layout); if (strlen(slots_json) >= sizeof(slots_json)) return 1; puts(slots_json);
 init_slot(&slots[0], "rootfs", "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 16);
 strlcpy(slots[0].label, "Unknown system", sizeof(slots[0].label));
 format_slots(&layout); if (strlen(slots_json) >= sizeof(slots_json)) return 2; puts(slots_json);
 return 0;
}
'''
        api_source = (ROOT / "u-boot-2016/lib/ipq_api.c").read_text()
        escape_start = api_source.index("size_t json_escape(")
        brace = api_source.index("{", escape_start)
        depth, pos = 1, brace + 1
        while depth:
            depth += (api_source[pos] == "{") - (api_source[pos] == "}")
            pos += 1
        production_escape = api_source[escape_start:pos]
        stub_start = json_harness.index("static size_t json_escape(")
        brace = json_harness.index("{", stub_start)
        depth, pos = 1, brace + 1
        while depth:
            depth += (json_harness[pos] == "{") - (json_harness[pos] == "}")
            pos += 1
        json_harness = json_harness[:stub_start] + production_escape + json_harness[pos:]
        json_cfile, json_exe = Path(temp) / "json.c", Path(temp) / "json-test"
        json_cfile.write_text(json_harness)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-sign-compare", "-I", str(MODULE), str(json_cfile), "-o", str(json_exe)], check=True)
        result = subprocess.run([str(json_exe)], capture_output=True, text=True, check=True)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(responses) == 2
        assert len(json.dumps(responses[0], separators=(",", ":"))) < 2048
        r005, thirdparty = responses[0]["slots"]
        assert thirdparty["detection"] == "rootfs_release_file"
        assert thirdparty["type"] == "OpenWrt"
        assert thirdparty["version"] == 'OpenWrt "21.02.7\\preview'
        assert not any(key.startswith("dashboard_") for slot in responses[0]["slots"] for key in slot)
        assert responses[0]["read_only"] is False and responses[0]["boot_supported"] is True
        unknown = responses[1]["slots"][0]
        assert not any(key.startswith("dashboard_") for key in unknown)
        print("PASS: production JSON formatter emits OEM firmware version and contains no dashboard-derived fields")


if __name__ == "__main__":
    main()
