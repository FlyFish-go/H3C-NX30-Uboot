#!/usr/bin/env python3
"""Assemble RESULT.json from the recorded matrix runs."""
import json, os, hashlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGS = os.path.join(HERE, "logs")

def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()

def load(name):
    p = os.path.join(LOGS, name + ".json")
    return json.load(open(p)), p

def main():
    runs = {}
    for tag in ("head-static", "prefix-static",
                "prefix-prodA", "head-prodA", "prefix-prodB", "head-prodB",
                "prefix-prodC", "head-prodC", "prefix-prodD", "head-prodD",
                "head-table84"):
        try:
            doc, path = load(tag)
        except FileNotFoundError:
            continue
        runs[tag] = {"path": os.path.relpath(path, HERE), "sha256": sha(path),
                     "pass": doc["pass"], "fail": doc["fail"], "total": doc["total"],
                     "cases": doc["cases"]}

    sweeps = {}
    for b in "ABCD":
        p = os.path.join(LOGS, f"sweep-{b}.json")
        if not os.path.exists(p):
            continue
        d = json.load(open(p))
        sweeps[b] = {"path": os.path.relpath(p, HERE), "sha256": sha(p),
                     "candidates": d["candidates"], "clean": d["clean"],
                     "results": d["results"]}

    digests_path = os.path.join(LOGS, "digests-wsl.json")
    digests = json.load(open(digests_path))
    prov_path = os.path.join(LOGS, "providers-audit.json")
    providers = json.load(open(prov_path))

    adversarial = {}
    for name in ("adv-C-row-wrong-machid", "adv-two-rows"):
        pth = os.path.join(LOGS, name + ".json")
        if os.path.exists(pth):
            adversarial[name] = {"path": os.path.relpath(pth, HERE),
                                 "sha256": sha(pth),
                                 "result": json.load(open(pth))}

    summary = {
        "normative_build": "head-2bbf85a",
        "before_build": "prefix-1bb3ded",
        "reverted_build": "table84 (8465ced table + 2bbf85a runtime fix)",
        "normative_all_pass": all(r["fail"] == 0 for k, r in runs.items()
                                  if k.startswith("head-") and k != "head-table84"),
        "before_failures": {k: v["fail"] for k, v in runs.items() if k.startswith("prefix-")},
        "reverted_failures": runs.get("head-table84", {}).get("fail"),
        "sweep_total": sum(s["candidates"] for s in sweeps.values()),
        "sweep_clean": sum(s["clean"] for s in sweeps.values()),
    }
    out = {
        "schema": 1,
        "task": "task-2 independent admission-matrix verification",
        "generated_by": "admission-verifier",
        "repo_head": "2bbf85a00d665030480feffe20b44e6b0255a1d4",
        "source_snapshots": {
            "prefix-1bb3ded": sha(os.path.join(HERE, "src-snapshots/bridge_install_identity.prefix-1bb3ded.c")),
            "head-2bbf85a": sha(os.path.join(HERE, "src-snapshots/bridge_install_identity.head-2bbf85a.c")),
            "header": sha(os.path.join(HERE, "src-snapshots/bridge_install_identity.h")),
        },
        "archive": {
            "path": digests["archive"], "sha256": digests["archive_sha256"],
            "bytes": digests["archive_bytes"],
            "candidates_json_sha256": digests["candidates_json_sha256"],
            "manifest_sha256": digests["manifest_sha256"],
            "bss_metadata_sha256": digests["bss_metadata_sha256"],
            "candidate_count": digests["candidate_count"],
            "per_board_counts": digests["per_board_counts"],
            "digest_mismatches": digests["digest_mismatches"],
            "extraction_log": {"path": "logs/digests-wsl.json",
                                "sha256": sha(digests_path)},
        },
        "provider_binaries": {
            "path": "logs/providers-audit.json", "sha256": sha(prov_path),
            "rows": len(providers["rows"]), "problems": providers["problems"],
            "all_carry_own_digest": all(r["has_own_digest"] for r in providers["rows"]),
            "all_carry_board_machid": all(r["has_machid"] for r in providers["rows"]),
        },
        "summary": summary,
        "adversarial_probes": adversarial,
        "runs": runs,
        "sweeps": sweeps,
    }
    outpath = os.path.join(HERE, "RESULT.json")
    with open(outpath, "w") as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write("\n")
    print(json.dumps(summary, indent=1, sort_keys=True))
    print("wrote", outpath, "sha256", sha(outpath))

if __name__ == "__main__":
    raise SystemExit(main())
