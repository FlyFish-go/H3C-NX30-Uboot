#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate a BootBridge v2 rule file from a U-Boot release.

    make_rules.py --candidates CANDIDATES.json --nand-profiles nand_profiles.json \
                  --set BB2-R1 [--dist DIR] > rules.txt

CANDIDATES.json comes from the U-Boot release (uboot-v0.1.0-pre.3 and later).
The NAND profile list is the U-Boot NAND catalog in JSON form. With --dist,
every candidate's appsbl.mbn is checked against the recorded size and hash.
"""
import argparse
import hashlib
import json
import os
import re
import sys

# Board groups. A and C have their own OEM boot routes. HW-B and HW-D share
# mp02.1 / machid 0x08040000; only HW-D boards carry a 5 GHz FEM, which shows
# in the factory calibration block (ART + 0x26800): +0x3a 0x60/0x50 and
# +0x45 0x09/0x01. A FEM-less HW-D (RC3000 B0) therefore gets the HW-B build,
# which was bench-verified to run on HW-D.
BOARD_RULES = """\
board A route=mp03.1 machid=0x08040001
board C route=mp03.1-c2 machid=0x08040101
board D route=mp02.1 machid=0x08040000 art5g@0=01000404 art5g@3a=60 art5g@45=09
board B route=mp02.1 machid=0x08040000 art5g@0=01000404 art5g@3a=50 art5g@45=01
"""


def nand_rule(p):
    models = []
    for m in [p["oem_model"]] + p.get("model_aliases", []):
        if m not in models:
            models.append(m)
    for m in models:
        if not re.fullmatch(r"[A-Za-z0-9()._-]+", m):
            sys.exit(f"unsafe model name {m!r}")
    return (f"nand {p['profile_id']} nand.id={p['manufacturer_id']:02x}:{p['chip_id']:02x} "
            f"nand.model={'|'.join(models)} nand.mib={p['capacity_mib']} "
            f"nand.page={p['page_bytes']} nand.erase_kib={p['erase_bytes'] // 1024} "
            f"nand.oob={p['raw_spare_bytes']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidates", required=True)
    ap.add_argument("--nand-profiles", required=True)
    ap.add_argument("--set", required=True)
    ap.add_argument("--dist", help="extracted release folders HW-<board>/<model>-ECC<n>/")
    a = ap.parse_args()

    cands = json.load(open(a.candidates))
    profiles = {p["profile_id"]: p for p in json.load(open(a.nand_profiles))["profiles"]}

    out = ["bbrules 1", f"set {a.set}", f"# from {cands.get('release', '?')} "
           f"source {cands.get('source_commit', '?')}", "", BOARD_RULES]
    used = sorted({c["nand_profile_id"] for c in cands["candidates"]})
    missing = [u for u in used if u not in profiles]
    if missing:
        sys.exit(f"candidates use unknown NAND profiles: {missing}")
    out += [nand_rule(profiles[u]) for u in used] + [""]

    seen = set()
    for c in sorted(cands["candidates"], key=lambda c: (c["board"], c["nand_profile_id"],
                                                         c["storage_ecc_bits"])):
        if c["load_ecc_bits"] != c["storage_ecc_bits"]:
            sys.exit(f"{c['folder']}: load/store ECC differ, not supported")
        key = (c["board"], c["nand_profile_id"], c["storage_ecc_bits"])
        if key in seen:
            sys.exit(f"duplicate candidate {key}")
        seen.add(key)
        cid = f"{c['board']}-{c['nand_profile_id']}-ecc{c['storage_ecc_bits']}"
        if a.dist:
            path = os.path.join(a.dist, c["folder"], "appsbl.mbn")
            data = open(path, "rb").read()
            if len(data) != c["appsbl_mbn_bytes"] or \
                    hashlib.sha256(data).hexdigest() != c["appsbl_mbn_sha256"]:
                sys.exit(f"{path}: size or hash differs from CANDIDATES.json")
            part = data + b"\xff" * (c["appsbl_partition_bytes"] - len(data))
            if hashlib.sha256(part).hexdigest() != c["appsbl_partition_sha256"]:
                sys.exit(f"{path}: 0xff-padded partition hash differs from CANDIDATES.json")
        out.append(f"candidate {cid} board={c['board']} nand={c['nand_profile_id']} "
                   f"ecc={c['storage_ecc_bits']} bytes={c['appsbl_mbn_bytes']} "
                   f"sha256={c['appsbl_mbn_sha256']} part_bytes={c['appsbl_partition_bytes']} "
                   f"part_sha256={c['appsbl_partition_sha256']} file=candidates/{cid}.mbn")
    print("\n".join(out))


if __name__ == "__main__":
    main()
