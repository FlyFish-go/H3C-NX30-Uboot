#!/usr/bin/env python3
"""Audit every shipped R4 provider binary: does it carry its OWN candidate digest
and the literal machid for its board? Read-only."""
import json, os, re, subprocess, sys

BUNDLES = "/home/yu/Project/RT3000/evidence/bridge10-remap-20261020/bundles-r4"
MODELS = ["RC3000", "RT3000", "RW3000", "NX30"]
MACHID = {"A": "08040001", "B": "08040000", "C": "08040101", "D": "08040000"}
HEX64 = re.compile(rb"[0-9a-f]{64}")

def strings(path):
    # Read raw; the literals are plain ASCII in .rodata.
    with open(path, "rb") as f:
        return f.read()

def main():
    rows, problems = [], []
    for model in MODELS:
        base = os.path.join(BUNDLES, model, "provider-builds")
        variants = sorted(os.listdir(base))
        counts = {"variants": 0, "providers": 0, "own_digest": 0, "machid": 0,
                  "both": 0, "missing_own": 0, "missing_machid": 0}
        for v in variants:
            vdir = os.path.join(base, v)
            hdr = os.path.join(vdir, "bii_candidate_policy.generated.h")
            if not os.path.isfile(hdr):
                problems.append(f"{model}/{v}: no generated policy header")
                continue
            text = open(hdr, encoding="utf-8").read()
            m = re.search(r'\{"([ABCD])",\s*"([0-9a-f]{64})",\s*"([0-9a-f]{8})"', text)
            if not m:
                problems.append(f"{model}/{v}: header has no policy row")
                continue
            board, digest, machid = m.group(1), m.group(2), m.group(3)
            counts["variants"] += 1
            cand_dir = os.path.join(vdir, "rootfs/usr/sbin/bridge-candidates")
            provs = []
            for root, _dirs, files in os.walk(cand_dir):
                if "provider" in files:
                    provs.append(os.path.join(root, "provider"))
            if len(provs) != 1:
                problems.append(f"{model}/{v}: expected 1 provider, found {len(provs)}")
                continue
            blob = strings(provs[0])
            counts["providers"] += 1
            has_own = digest.encode() in blob
            has_machid = machid.encode() in blob
            if has_own: counts["own_digest"] += 1
            else: counts["missing_own"] += 1
            if has_machid: counts["machid"] += 1
            else: counts["missing_machid"] += 1
            if has_own and has_machid: counts["both"] += 1
            # which other 64-hex digests appear (the 4 static pre.1 records)
            static = [d.decode() for d in HEX64.findall(blob)]
            rows.append({"model": model, "variant": v, "board": board,
                         "own_digest": digest, "machid": machid,
                         "has_own_digest": has_own, "has_machid": has_machid,
                         "hex64_literals": sorted(set(static))})
        print(json.dumps({"model": model, "counts": counts}, sort_keys=True), file=sys.stderr)
        problems += [f"{model}: counts {counts}"] if (
            counts["providers"] != counts["variants"] or counts["both"] != counts["variants"]) else []
    print(json.dumps({"schema": 1, "bundles": BUNDLES, "models": MODELS,
                      "rows": rows, "problems": problems}, indent=1, sort_keys=True))
    return 1 if problems else 0

if __name__ == "__main__":
    raise SystemExit(main())
