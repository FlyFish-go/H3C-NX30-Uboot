#!/usr/bin/env python3
"""Sweep every one of the 21 R4 HW-C candidates as if it were the shipped
provider row, and assert cross-candidate isolation each time.

For each candidate X: compile the post-fix identity TU with a generated policy
header containing ONLY X's row, then run the full prodC matrix where 'own' = X.
That proves, per candidate, that (a) X is admitted on the exact C2 route with a
no-machid ENV and (b) the other 20 C candidates are all still refused.
"""
import json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(HERE, "src-snapshots/bridge_install_identity.head-2bbf85a.c")
GEN = os.path.join(HERE, "tools/gen_matrix.py")
BUILD = os.path.join(HERE, "tools/build_matrix.sh")
DIGESTS = os.path.join(HERE, "logs/digests-wsl.json")
OUTROOT = os.path.join(HERE, "out/sweep")

def main():
    board = sys.argv[1] if len(sys.argv) > 1 else "C"
    variant = f"prod{board}"
    digests = json.load(open(DIGESTS))
    cands = [r["partition_sha256"] for r in digests["rows"] if r["board"] == board]
    assert len(cands) == 21, len(cands)
    os.makedirs(OUTROOT, exist_ok=True)
    results = []
    for idx, own in enumerate(cands):
        d = os.path.join(OUTROOT, board, f"c{idx:02d}")
        os.makedirs(d, exist_ok=True)
        hdr = os.path.join(d, "bii_candidate_policy.generated.h")
        with open(hdr, "w") as f:
            f.write("/* generated for sweep: exactly one C row */\n")
            f.write("#define BII_EXTRA_CANDIDATE_POLICIES \\\n")
            mach = {"A": "08040001", "B": "08040000",
                    "C": "08040101", "D": "08040000"}[board]
            meta = next(r for r in digests["rows"] if r["partition_sha256"] == own)
            f.write('    {"%s", "%s", "%s", %s, %du, %du},\n' % (
                board, own, mach,
                "NULL" if meta["env_alias"] is None else '"%s"' % meta["env_alias"],
                meta["filter_env_root_keys"], meta["allow_missing_env_machid"]))
        matrix = os.path.join(d, f"va_matrix.{variant}.h")
        extra = ["--own-c", own] if board == "C" else ["--own-a", own]
        subprocess.run([sys.executable, GEN, "--variant", variant, "--digests", DIGESTS,
                        "--out", matrix] + extra, check=True, capture_output=True)
        r = subprocess.run([BUILD, SRC, "bii_candidate_policy.generated.h", matrix, d],
                           capture_output=True, text=True)
        if r.returncode:
            results.append({"index": idx, "candidate": own, "build": "FAIL",
                            "stderr": r.stderr[-800:]})
            continue
        run = subprocess.run([os.path.join(d, "va_runner")], capture_output=True, text=True)
        doc = json.loads(run.stdout)
        fails = [c["id"] for c in doc["cases"] if not c["pass"]]
        iso = [c for c in doc["cases"] if c["category"] == "cross-candidate-isolation"]
        iso_bad = [c["id"] for c in iso if not c["pass"]]
        own_case = next(c for c in doc["cases"] if c["id"].endswith("own-exact-route-base-env"))
        results.append({"index": idx, "candidate": own, "build": "OK",
                        "total": doc["total"], "pass": doc["pass"], "fail": doc["fail"],
                        "own_admitted": own_case["pass"],
                        "own_actual": own_case["actual"],
                        "isolation_cases": len(iso), "isolation_failures": iso_bad,
                        "failures": fails})
    ok = [r for r in results if r.get("build") == "OK" and r["fail"] == 0
          and r["own_admitted"] and not r["isolation_failures"]]
    print(json.dumps({"schema": 1, "board": board, "candidates": len(cands), "clean": len(ok),
                      "results": results}, indent=1, sort_keys=True))
    return 0 if len(ok) == len(cands) else 1

if __name__ == "__main__":
    raise SystemExit(main())
