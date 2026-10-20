#!/bin/sh
# Build the bundle-based offline GUI (optional H3C official-firmware download
# through WinHTTP, see src/bb_fetch.c). Cross-compiles only; never executes the PE.
set -eu
source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
project_root=$(CDPATH= cd -- "$source_root/.." && pwd)
arch=${1:-all}
bundle_result=${2:-${BOOTBRIDGE_OFFLINE_BUNDLE_RESULT:-}}
guide_models=${BOOTBRIDGE_OFFLINE_GUIDE_MODELS:-"$source_root/guide-models.json"}
output_root=${BOOTBRIDGE_OFFLINE_OUTPUT_ROOT:-"$source_root/build/offline-windows-bundle-new"}
case "$output_root" in /*) ;; *) output_root="$PWD/$output_root" ;; esac
case "$arch" in x86|x64|all) ;; *) echo 'Usage: build-windows-offline.sh x86|x64|all BUNDLE-RESULT.json' >&2; exit 2;; esac
if [ -z "$bundle_result" ] || [ ! -f "$bundle_result" ]; then
    echo 'Provide the audited provider-bundle RESULT.json as the second argument or BOOTBRIDGE_OFFLINE_BUNDLE_RESULT.' >&2
    exit 2
fi
case "$bundle_result" in /*) ;; *) bundle_result="$PWD/$bundle_result" ;; esac
case "$guide_models" in /*) ;; *) guide_models="$PWD/$guide_models" ;; esac
if [ -e "$output_root" ] || [ -L "$output_root" ]; then
    echo "Refusing to reuse existing offline build output: $output_root" >&2
    exit 2
fi

for path in design/desktop-offline-ui/strings.json guide-models.json \
            tools/gen_ui_strings.py tools/gen_guide_models.py \
            gui/offline_main.c offline/bb_offline_rules.c offline/bb_offline_rules.h \
            tools/prepare_offline_bundles.py tools/build_offline_bundle_catalog.py \
            tools/assemble_release.py tools/compile_delta_plan.py src/bb_export.c catalog/bb_catalog.c \
            catalog/bb_catalog.h gui/resources.rc gui/app.manifest \
            src/bb_fetch.c src/bb_fetch.h src/bb_net.h src/bb_net_policy.c src/bb_net_win.c \
            src/official_api.c src/official_api.h; do
    case "$path" in
        design/*) test -f "$project_root/$path" ||
            { echo "Missing required offline source: $path" >&2; exit 1; } ;;
        *) test -f "$source_root/$path" ||
            { echo "Missing required offline source: $path" >&2; exit 1; } ;;
    esac
done
mkdir -p "$output_root"
prepared="$output_root/prepared-bundles"
python3 "$source_root/tools/prepare_offline_bundles.py" "$bundle_result" "$prepared"
generated="$output_root/generated-bundle-catalog"
python3 "$source_root/tools/build_offline_bundle_catalog.py" "$prepared/prepared-bundles.json" "$prepared" "$generated"
test -s "$generated/bb_offline_catalog_generated.h"
test -f "$generated/BootBridge.rules.dat"
python3 "$source_root/tools/gen_ui_strings.py" "$project_root/design/desktop-offline-ui/strings.json" "$generated/bb_ui_strings_generated.h"
python3 "$source_root/tools/gen_guide_models.py" "$guide_models" "$prepared/prepared-bundles.json" "$generated/bb_guide_models_generated.h"
python3 - "$project_root/design/desktop-offline-ui/strings.json" "$source_root/guide-models.json" \
    "$generated/bb_ui_strings_generated.h" "$generated/bb_guide_models_generated.h" <<'PY'
import hashlib, pathlib, re, sys
strings, models, strings_header, models_header = map(pathlib.Path, sys.argv[1:])
def pin_header(path, macro):
    text = path.read_text(encoding="utf-8")
    match = re.search(r'#define\s+%s\s+L"([0-9a-f]{64})"' % macro, text)
    if not match:
        raise SystemExit("%s lacks %s" % (path, macro))
    return match.group(1)
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
checks = [
    (strings, pin_header(strings_header, "BB_UI_STRINGS_SOURCE_SHA256")),
    (models, pin_header(models_header, "BB_GUIDE_MODELS_SOURCE_SHA256")),
]
for source, pinned in checks:
    actual = digest(source)
    if actual != pinned:
        raise SystemExit("%s source pin mismatch: %s != %s" % (source, actual, pinned))
print("generated UI headers match their source pins")
PY

build_one() {
    target=$1; prefix=$2
    cc="${prefix}-gcc"; objdump="${prefix}-objdump"; windres="${prefix}-windres"
    command -v "$cc" >/dev/null 2>&1 || { echo "Missing compiler: $cc" >&2; exit 1; }
    command -v "$objdump" >/dev/null 2>&1 || { echo "Missing PE inspector: $objdump" >&2; exit 1; }
    command -v "$windres" >/dev/null 2>&1 || { echo "Missing resource compiler: $windres" >&2; exit 1; }
    out="$output_root/$target"
    mkdir -p "$out"
    "$windres" -I "$source_root/gui" "$source_root/gui/resources.rc" "$out/resources.o"
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -Wformat=2 \
        -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DUNICODE -D_UNICODE \
        -D__USE_MINGW_ANSI_STDIO=1 -finput-charset=UTF-8 -fexec-charset=UTF-8 \
        -I"$generated" -I"$source_root/offline" -I"$source_root/catalog" -I"$source_root/src" \
        -municode -mwindows -static -static-libgcc \
        -Wl,--major-subsystem-version,6,--minor-subsystem-version,1,--no-insert-timestamp \
        "$source_root/gui/offline_main.c" "$source_root/gui/bb_ui_text.c" "$source_root/offline/bb_offline_rules.c" \
        "$source_root/src/bb_export.c" "$source_root/catalog/bb_catalog.c" \
        "$source_root/src/bb_fetch.c" "$source_root/src/bb_net_policy.c" "$source_root/src/bb_net_win.c" \
        "$source_root/src/official_api.c" "$out/resources.o" \
        -lcomdlg32 -lcomctl32 -lgdi32 -lgdiplus -luser32 -lshell32 -ladvapi32 -ldwmapi -lversion -lwinhttp \
        -o "$out/BootBridge-Offline-$target.exe"
    cp "$generated/BootBridge.rules.dat" "$out/BootBridge.rules.dat"
    "$objdump" -p "$out/BootBridge-Offline-$target.exe" > "$out/pe-imports.txt"
    sh "$source_root/tools/check-offline-pe.sh" "$target" "$out/BootBridge-Offline-$target.exe"
    (
        cd "$project_root"
        sha256sum design/desktop-offline-ui/strings.json desktop/guide-models.json \
            desktop/tools/gen_ui_strings.py desktop/tools/gen_guide_models.py \
            desktop/gui/offline_main.c desktop/offline/bb_offline_rules.c desktop/offline/bb_offline_rules.h \
            "$generated/bb_offline_catalog_generated.h" "$generated/BootBridge.rules.dat" \
            "$generated/bb_ui_strings_generated.h" "$generated/bb_guide_models_generated.h" \
            desktop/src/bb_export.c desktop/catalog/bb_catalog.c desktop/gui/resources.rc desktop/gui/app.manifest \
            desktop/catalog/bb_catalog.h desktop/tools/prepare_offline_bundles.py desktop/tools/build_offline_bundle_catalog.py \
            desktop/tools/assemble_release.py desktop/tools/compile_delta_plan.py \
            desktop/src/bb_fetch.c desktop/src/bb_fetch.h desktop/src/bb_net.h desktop/src/bb_net_policy.c \
            desktop/src/bb_net_win.c desktop/src/official_api.c desktop/src/official_api.h \
            desktop/tools/build-windows-offline.sh desktop/tools/check-offline-pe.sh > "$out/source-inputs.sha256"
    )
    python3 - "$generated/bundle-catalog-manifest.json" "$out/bundle-inputs.sha256" <<'PY'
import hashlib, json, pathlib, sys
manifest_path=pathlib.Path(sys.argv[1]).resolve()
manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
prepared_path=pathlib.Path(manifest['prepared_manifest']['path']).resolve()
prepared=json.loads(prepared_path.read_text(encoding='utf-8'))
records=[manifest_path,prepared_path,pathlib.Path(prepared['aggregate_result']['path']).resolve()]
for row in manifest['bundles']:
    records.append(pathlib.Path(row['program']['path']).resolve())
    for identity in row.get('supported_product_identities',[]):
        if identity.get('identity_evidence'):
            records.append(pathlib.Path(identity['identity_evidence']['path']).resolve())
    for key in ('build_report','audit_report'):
        records.append(pathlib.Path(row[key]['path']).resolve())
    for coverage in row.get('logical_coverage',[]):
        records.append(pathlib.Path(coverage['evidence']['path']).resolve())
with open(sys.argv[2],'w',encoding='utf-8',newline='\n') as out:
    for p in records:
        if not p.is_file(): raise SystemExit('bundle build evidence is missing: '+str(p))
        out.write(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p)+'\n')
PY
    sha256sum "$out/BootBridge-Offline-$target.exe" "$out/BootBridge.rules.dat" > "$out/outputs.sha256"
    python3 - "$target" "$out" "$bundle_result" "$generated/bundle-catalog-manifest.json" <<'PY'
import hashlib, json, pathlib, sys
arch,out,result_path,cat_manifest_path=sys.argv[1],pathlib.Path(sys.argv[2]),pathlib.Path(sys.argv[3]).resolve(),pathlib.Path(sys.argv[4]).resolve()
def read_hashes(name):
    result={}
    for line in (out/name).read_text(encoding='utf-8').splitlines():
        digest,path=line.split('  ',1)
        result[pathlib.Path(path).name]=digest
    return result
manifest={
    'artifact':'BootBridge offline-only cross-build',
    'architecture':arch,
    'bundle_result':str(result_path),
    'bundle_result_sha256':hashlib.sha256(result_path.read_bytes()).hexdigest(),
    'bundle_catalog_manifest':str(cat_manifest_path),
    'bundle_catalog_manifest_sha256':hashlib.sha256(cat_manifest_path.read_bytes()).hexdigest(),
    'windows_subsystem':'GUI 6.01',
    'execution':'not run',
    'pe_import_audit':'passed; see pe-imports.txt',
    'linked_sources':['gui/offline_main.c','gui/bb_ui_text.c','offline/bb_offline_rules.c','src/bb_export.c','catalog/bb_catalog.c',
                      'src/bb_fetch.c','src/bb_net_policy.c','src/bb_net_win.c','src/official_api.c','gui/resources.rc'],
    'source_sha256':read_hashes('source-inputs.sha256'),
    'bundle_evidence_sha256':read_hashes('bundle-inputs.sha256'),
    'output_sha256':read_hashes('outputs.sha256'),
}
(out/'build-manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n',encoding='utf-8')
PY
    printf 'Built offline-only %s: %s\n' "$target" "$out/BootBridge-Offline-$target.exe"
}

if [ "$arch" = x86 ] || [ "$arch" = all ]; then build_one x86 i686-w64-mingw32; fi
if [ "$arch" = x64 ] || [ "$arch" = all ]; then build_one x64 x86_64-w64-mingw32; fi
echo 'Cross-build and static import audit complete. This does not claim Windows runtime compatibility.'
