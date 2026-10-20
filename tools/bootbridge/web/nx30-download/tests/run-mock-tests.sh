#!/usr/bin/env bash
# Offline-ish regression for the NX30 R012 download commands.
# A local mock replaces only the H3C query API; the file itself is the public
# RC3000 object from the official OSS, so the https download path is real.
# Needs: bash, curl, python3; optional zsh and pwsh (PowerShell 7 = syntax only, not 5.1).
set -u
cd "$(dirname "$0")"
SN=TEST0000000000000000
RCSHA=69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44
T=$(mktemp -d); export HOME="$T/home"; mkdir -p "$HOME/dl"
python3 mock_api.py 2>"$T/mock.log" & MOCK=$!; trap 'kill $MOCK; rm -rf "$T"' EXIT; sleep 1
mk_sh(){ sed -e "s/__SN__/$SN/" -e "s#api=https://magic.h3c.com/smarthomeback/rest/ihomers/getGwDownloadNew#api=http://127.0.0.1:8765/#" \
  -e "s#curl -sS --proto '=https' --max-time 30#curl -sS --max-time 30#" -e "s#HOME}/Downloads#HOME}/dl#" "$@" ../sh.tpl; }
mk_ps(){ sed -e "s/__SN__/$SN/" -e "s#\$api='https://magic.h3c.com/smarthomeback/rest/ihomers/getGwDownloadNew'#\$api='http://127.0.0.1:8765/'#" \
  -e "s#Join-Path \$env:USERPROFILE 'Downloads'#Join-Path \$HOME 'dl'#" "$@" ../ps51.tpl; }
PASS=0; FAIL=0
check(){ if eval "$2"; then echo "PASS $1"; PASS=$((PASS+1)); else echo "FAIL $1"; FAIL=$((FAIL+1)); fi; rm -f "$HOME"/dl/*; }
OKPIN=(-e "s/sha=61fbc2a3[0-9a-f]*;/sha=$RCSHA;/" -e "s/size=31981568/size=33554432/")
OKPINPS=(-e "s/\$sha='61fbc2a3[0-9a-f]*'/\$sha='$RCSHA'/" -e "s/\$size=31981568/\$size=33554432/")
mk_sh > "$T/bad.sh"; mk_sh "${OKPIN[@]}" > "$T/ok.sh"
bash --norc -i < "$T/ok.sh" >"$T/o" 2>&1; check "bash success" '[ -s "$HOME/dl/NX30V100R012.bin" ] && grep -q "完成" "$T/o"'
bash --norc -i < "$T/bad.sh" >"$T/o" 2>&1; check "bash mismatch deletes" '[ -z "$(ls "$HOME/dl")" ] && grep -q "校验失败" "$T/o"'
if command -v zsh >/dev/null; then
  zsh -f -i < "$T/ok.sh" >"$T/o" 2>&1; check "zsh success" '[ -s "$HOME/dl/NX30V100R012.bin" ]'
  zsh -f -i < "$T/bad.sh" >"$T/o" 2>&1; check "zsh mismatch deletes" '[ -z "$(ls "$HOME/dl")" ]'
fi
PWSH=${PWSH:-$(command -v pwsh || true)}
if [ -n "$PWSH" ]; then
  mk_ps "${OKPINPS[@]}" > "$T/ok.ps1"; mk_ps > "$T/bad.ps1"
  "$PWSH" -NoProfile -Command - < "$T/ok.ps1" >"$T/o" 2>&1; check "pwsh success" '[ -s "$HOME/dl/NX30V100R012.bin" ]'
  "$PWSH" -NoProfile -Command - < "$T/bad.ps1" >"$T/o" 2>&1; check "pwsh mismatch deletes" '[ -z "$(ls "$HOME/dl")" ]'
fi
check "mock saw R011 then R012" 'head -2 "$T/mock.log" | grep -c "NX30V100R01[12]" | grep -q 2'
echo "passed=$PASS failed=$FAIL"; [ "$FAIL" = 0 ]
