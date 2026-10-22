#!/bin/bash
# Build one (source snapshot x policy header) admission-matrix binary.
# usage: build_matrix.sh <snapshot.c> <policy-header|none> <matrix.h> <outdir>
set -eu
SNAP=$1; POLICY=$2; MATRIX=$3; OUT=$4
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
cp "$MATRIX" "$OUT/va_matrix.h"
cp "$HERE/src-snapshots/bridge_install_identity.h" "$OUT/"
FLAGS=(-std=c99 -O1 -g -Wall -Wextra -Werror -I"$OUT" -I"$HERE/harness" -I"$HERE/include")
if [ "$POLICY" != "none" ]; then
    FLAGS+=(-DBII_CANDIDATE_POLICY_HEADER="\"$POLICY\"")
fi
gcc "${FLAGS[@]}" "$SNAP" "$HERE/harness/va_runner.c" -o "$OUT/va_runner"
echo "built $OUT/va_runner"
