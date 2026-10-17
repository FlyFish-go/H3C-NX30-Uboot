#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
cc -std=c99 -Wall -Wextra -Werror -I test/upgrade_rt3_stubs \
  -iquote include test/upgrade_rt3_stubs/upgrade_callback_hosttest.c -o "$tmp"
"$tmp"
