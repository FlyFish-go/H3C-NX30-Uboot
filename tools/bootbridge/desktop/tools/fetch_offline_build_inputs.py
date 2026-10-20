#!/usr/bin/env python3
"""Fetch one already-generated offline catalog from the configured WSL tree."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import os
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
WSL_RUN = ROOT / "tools/wsl-run.py"
# Build inputs live in a separate working tree that the caller supplies.
REMOTE_ROOT = Path(
    os.environ.get("BOOTBRIDGE_REMOTE_ROOT", "")
    or "/path/to/bootbridge-r6-development"
)
FILES = (
    "prepared-bundles.json",
    "programs/NX30-61fbc2a356aa.bbprog",
    "programs/RC3000-69d3e558e770.bbprog",
    "programs/RT3000-cb2966e54ae1.bbprog",
    "programs/RW3000-5d2d8c62dd24.bbprog",
    "generated/bb_offline_catalog_generated.h",
    "generated/BootBridge.rules.dat",
    "generated/bundle-catalog-manifest.json",
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("remote_dir", type=Path)
    parser.add_argument("result_json", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.destination.exists() or args.destination.is_symlink():
        parser.error("fresh destination required")
    result = args.result_json.resolve(strict=True)
    document = json.loads(result.read_text())
    if document.get("schema") != "bootbridge-provider-bundle-build-result-v1" or document.get("status") != "PASS":
        parser.error("audited RESULT is not PASS")

    remote = REMOTE_ROOT / args.remote_dir
    request = {
        "root": str(remote),
        "files": list(FILES),
    }
    code = (
        "import base64,hashlib,json,pathlib,sys\n"
        "r=json.loads(sys.stdin.read())\n"
        "root=pathlib.Path(r['root']).resolve(strict=True)\n"
        "for name in r['files']:\n"
        " p=root/name\n"
        " assert p.is_file() and not p.is_symlink() and root in p.resolve().parents\n"
        " raw=p.read_bytes()\n"
        " print(json.dumps({'path':name,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),"
        "'data':base64.b64encode(raw).decode()}))\n"
    )
    proc = subprocess.run(
        [sys.executable, str(WSL_RUN), "--cwd", str(REMOTE_ROOT), "--", "python3", "-c", code],
        input=json.dumps(request), text=True, capture_output=True,
    )
    if proc.returncode:
        sys.stderr.write(proc.stderr)
        return proc.returncode

    rows = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
    if len(rows) != len(FILES):
        raise SystemExit("incomplete remote catalog listing")
    args.destination.mkdir(parents=True)
    for row in rows:
        raw = base64.b64decode(row["data"], validate=True)
        if len(raw) != row["bytes"] or hashlib.sha256(raw).hexdigest() != row["sha256"]:
            raise SystemExit("remote catalog changed during transfer: " + row["path"])
        target = args.destination / row["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        if sha(target) != row["sha256"]:
            raise SystemExit("local catalog transfer mismatch: " + row["path"])

    receipt = {
        "schema": "bootbridge-offline-catalog-fetch-v1",
        "status": "PASS_TRANSFER_HASH_VERIFIED",
        "remote_root": str(remote),
        "audited_result": {"path": str(result), "bytes": result.stat().st_size, "sha256": sha(result)},
        "files": [{"path": row["path"], "bytes": row["bytes"], "sha256": row["sha256"]} for row in rows],
    }
    (args.destination / "FETCH-RECEIPT.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(receipt, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
