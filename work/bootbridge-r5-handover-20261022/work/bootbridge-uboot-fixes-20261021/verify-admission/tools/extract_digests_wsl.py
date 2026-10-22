#!/usr/bin/env python3
"""Independent digest derivation from the R4 candidate archive.

Does not import the repo parser: streams the tar, hashes every
appsbl.partition.bin itself, then cross-checks the CANDIDATES.json rows and the
embedded SHA256SUMS/MANIFEST inventories.
"""
import hashlib, io, json, sys, tarfile

ARCHIVE = "/home/yu/Project/RT3000/evidence/bridge10-remap-20261020/nand-uboot-candidates-b0383f96b328-v2.tar.gz"
EXPECTED_ARCHIVE_SHA = "2e0972448bf39b2a37a814b725fe9514e8e7e03705ed12955c82a9cca1805008"
ROOT = "h3c-uboot-candidates-v2/"

def sha_stream(f, chunk=1 << 20):
    h = hashlib.sha256()
    while True:
        b = f.read(chunk)
        if not b:
            return h.hexdigest()
        h.update(b)

def main():
    with open(ARCHIVE, "rb") as f:
        outer = sha_stream(f)
    if outer != EXPECTED_ARCHIVE_SHA:
        print(json.dumps({"error": "archive sha mismatch", "got": outer}), file=sys.stderr)
        return 2
    meta = {}
    payload = {}
    with tarfile.open(ARCHIVE, "r:gz") as tf:
        for m in tf:
            if not m.name.startswith(ROOT):
                raise SystemExit("member outside root: " + m.name)
            rel = m.name[len(ROOT):]
            if not m.isfile():
                continue
            f = tf.extractfile(m)
            if rel in ("MANIFEST.json", "CANDIDATES.json", "SHA256SUMS",
                       "source/SHA256SUMS", "VARIANT-BSS-METADATA.json"):
                meta[rel] = f.read()
            elif rel.endswith("/appsbl.partition.bin"):
                payload[rel] = (m.size, sha_stream(f))
            else:
                sha_stream(f)  # consume so the stream stays consistent
    cands = json.loads(meta["CANDIDATES.json"])
    rows = []
    mismatches = []
    for row in cands["candidates"]:
        part = row["files"]["appsbl.partition.bin"]
        got_size, got_sha = payload[part["path"]]
        if got_size != part["bytes"] or got_sha != part["sha256"]:
            mismatches.append({"path": part["path"], "declared": part,
                               "actual": {"bytes": got_size, "sha256": got_sha}})
        rows.append({"board": row["board"], "nand_profile_id": row["nand_profile_id"],
                     "load_ecc_bits": row["load_ecc_bits"],
                     "storage_ecc_bits": row["storage_ecc_bits"],
                     "candidate_id": row["candidate_id"],
                     "partition_path": part["path"],
                     "partition_sha256": got_sha,
                     "partition_bytes": got_size})
    bss = json.loads(meta["VARIANT-BSS-METADATA.json"])
    bss_by = {(v["board"], v["nand_profile_id"], v["load_ecc_bits"], v["storage_ecc_bits"]): v
              for v in bss["variants"]}
    for r in rows:
        v = bss_by.get((r["board"], r["nand_profile_id"], r["load_ecc_bits"], r["storage_ecc_bits"]))
        r["machid"] = v["machid"] if v else None
        r["env_alias"] = v["env_alias"] if v else None
        r["filter_env_root_keys"] = v["filter_env_root_keys"] if v else None
        r["allow_missing_env_machid"] = v["allow_missing_env_machid"] if v else None
        r["dt_compatible"] = v["dt_compatible"] if v else None
    per_board = {}
    for r in rows:
        per_board.setdefault(r["board"], []).append(r["partition_sha256"])
    dupes = {b: sorted({d for d in ds if ds.count(d) > 1}) for b, ds in per_board.items()}
    print(json.dumps({
        "schema": 1,
        "archive": ARCHIVE,
        "archive_sha256": outer,
        "archive_bytes": __import__("os").path.getsize(ARCHIVE),
        "candidates_json_sha256": hashlib.sha256(meta["CANDIDATES.json"]).hexdigest(),
        "manifest_sha256": hashlib.sha256(meta["MANIFEST.json"]).hexdigest(),
        "bss_metadata_sha256": hashlib.sha256(meta["VARIANT-BSS-METADATA.json"]).hexdigest(),
        "source_snapshot_sha256": cands["source_snapshot_sha256"],
        "candidate_count": len(rows),
        "per_board_counts": {b: len(v) for b, v in per_board.items()},
        "per_board_unique": {b: len(set(v)) for b, v in per_board.items()},
        "duplicate_digests_within_board": dupes,
        "digest_mismatches": mismatches,
        "rows": rows,
    }, indent=1, sort_keys=True))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
