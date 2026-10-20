#!/usr/bin/env bash
# BootBridge web edition: build + verification in one go (run in WSL by DSH).
#
#   web/tests/run-tests.sh            unit tests + real build into web/dist/ + full verification
#   web/tests/run-tests.sh --unit     unit/differential tests only (no release inputs needed)
#   E2E=1 web/tests/run-tests.sh      additionally run the Playwright UI test (needs node playwright + chromium)
#
# Overridable: HEADER DAT INPUTS OUT REPO_URL BUILD_LABEL CC
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); WEB=$(dirname "$HERE"); PROJ=$(dirname "$WEB")
SRC=$PROJ/desktop/src
GEN=$PROJ/desktop/build/offline-r4/generated-bundle-catalog
HEADER=${HEADER:-$GEN/bb_offline_catalog_generated.h}
DAT=${DAT:-$GEN/BootBridge.rules.dat}
INPUTS=${INPUTS:-$(cd "$PROJ/../../../../../.." 2>/dev/null && pwd)/RT3000/evidence/bootbridge-resume-20261016/gui-export-inputs}
OUT=${OUT:-$WEB/dist/site}
CC=${CC:-cc}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
step(){ printf '\n== %s\n' "$*"; }

step "engine differential test vs desktop/src/bb_export.c"
"$CC" -O2 -std=c99 -I"$SRC" -o "$T/c_harness" "$HERE/c_harness.c" "$SRC/bb_export.c"
node "$HERE/engine-vs-c.test.js" "$T/c_harness" 1200

step "NX30 SN / command unit tests"
BB_COMMANDS_DIR=$WEB/nx30-download node "$HERE/nx30.test.js"

[ "${1:-}" = "--unit" ] && { echo; echo "unit tests passed"; exit 0; }

step "build site from the pinned release ($HEADER)"
VER=$(sed -n 's/^WEB_VERSION = "\(.*\)"/\1/p' "$WEB/tools/build_site.py")
mkdir -p "$(dirname "$OUT")"
python3 "$WEB/tools/build_site.py" --header "$HEADER" --dat "$DAT" \
  --guide "$PROJ/desktop/guide-models.json" \
  --strings "$PROJ/design/desktop-offline-ui/strings.json" --strings-web "$WEB/strings-web.json" \
  --commands "$WEB/nx30-download" --inputs "$INPUTS" --out "$OUT" \
  --zip "$(dirname "$OUT")/BootBridge-Web-$VER.zip" \
  --repo-url "${REPO_URL:-}" --build-label "${BUILD_LABEL:-$(date -u +%Y%m%dT%H%MZ)}"

step "web engine on the built site vs desktop C engine (same header + sidecar)"
"$CC" -O2 -std=c99 -I"$SRC" -DBB_CATALOG_HEADER="\"$HEADER\"" -o "$T/c_cat" "$HERE/c_catalog_harness.c" "$SRC/bb_export.c" 2>"$T/cc.log" || { cat "$T/cc.log"; exit 1; }
node "$HERE/site-build.test.js" "$OUT" "$INPUTS" "$T/c_cat" "$DAT"

if [ "${E2E:-0}" = 1 ]; then
  step "Playwright UI test (http + file://)"
  PORT=${PORT:-8123}
  python3 -m http.server "$PORT" -d "$OUT" >"$T/http.log" 2>&1 & HTTP=$!
  trap 'kill $HTTP 2>/dev/null; rm -rf "$T"' EXIT; sleep 1
  node "$HERE/e2e.js" "$OUT" "http://127.0.0.1:$PORT" "$INPUTS" "$(dirname "$OUT")/e2e-shots" ${E2E_OFFLINE:+--offline}
fi
echo; echo "all checks passed -> $OUT"
( cd "$(dirname "$OUT")" && ls -l; sha256sum "BootBridge-Web-$VER.zip" )
