#!/usr/bin/env python3
"""Make a synthetic release (header + sidecar) for end-to-end tests.

Keeps every real plan program of the generated header unchanged, replaces the
sidecar bytes with seeded random data, then re-pins the sidecar SHA-256 and,
for every bundle whose official input is found in --inputs, the output size and
SHA-256 (computed with build_site.py's reference).  Outputs are therefore not
real BootBridge images; they only exercise the real operation layout.
usage: make_synthetic.py --header H --inputs DIR --out-header H2 --out-dat D2 [--seed N]
"""
import argparse, hashlib, os, random, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import build_site as bs

def cbytes(hexs): return ",".join("0x%s" % hexs[i:i + 2] for i in range(0, 64, 2))

ap = argparse.ArgumentParser()
ap.add_argument("--header", required=True); ap.add_argument("--inputs", required=True)
ap.add_argument("--out-header", required=True); ap.add_argument("--out-dat", required=True)
ap.add_argument("--seed", type=int, default=20261020)
a = ap.parse_args()
text = open(a.header, encoding="ascii").read()
cat = bs.parse_header(text)
dat = random.Random(a.seed).randbytes(cat["dataSize"])
pool = {}
for root, _, files in os.walk(a.inputs):
    for fn in files:
        p = os.path.join(root, fn)
        pool.setdefault(hashlib.sha256(open(p, "rb").read()).hexdigest(), p)
old = re.search(r"bb_offline_expected_sha256\[32\] = \{([^}]*)\}", text).group(1)
text = text.replace(old, cbytes(hashlib.sha256(dat).hexdigest()), 1)
for b in cat["bundles"]:
    src = pool.get(b["inSha"])
    if not src:
        print("synthetic %-24s no input, output pin left unchanged" % b["id"]); continue
    s, e = bs.slice_bundle(b, dat)
    out = bs.apply_plan(b, open(src, "rb").read(), dat[s:e])
    osha = hashlib.sha256(out).hexdigest()
    oldpin = "%dULL,{%s}" % (b["outSize"], cbytes(b["outSha"]))
    assert text.count(oldpin) == 1, b["id"]
    text = text.replace(oldpin, "%dULL,{%s}" % (len(out), cbytes(osha)))
    print("synthetic %-24s output %s" % (b["id"], osha))
open(a.out_header, "w", encoding="ascii").write(text)
open(a.out_dat, "wb").write(dat)
