#!/bin/bash
# Verbatim copy of the two generated headers used above, re-read from the R4 tree.
set -e
B=evidence/bridge10-remap-20261020/bundles-r4/RC3000/provider-builds
echo "== VARIANT-034 (board C)"; cat $B/VARIANT-034/bii_candidate_policy.generated.h; sha256sum $B/VARIANT-034/bii_candidate_policy.generated.h
echo "== VARIANT-000 (board A)"; cat $B/VARIANT-000/bii_candidate_policy.generated.h; sha256sum $B/VARIANT-000/bii_candidate_policy.generated.h
