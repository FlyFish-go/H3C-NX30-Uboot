#!/usr/bin/env python3
"""Callable host gate for complete ABI-v2 c_record tables.

Provider inspect readiness is deliberately synthetic here. The gate executes
the production C selector against exact generated C records; it does not exec
provider binaries or claim ARM/device validation.
"""
from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Sequence

PROJECT = Path(__file__).resolve().parents[1]
TOOLS = PROJECT / "tools"
sys.path.insert(0, str(TOOLS))
import build  # noqa: E402


def run_dispatch_gate(source_sha256: str, product: str,
                      c_records: Sequence[dict], expected_provider_rows: int,
                      cc: str = "cc") -> dict:
    """Compile and exercise the production dispatcher over one complete table.

    `c_records` must be every final row after `build_bundle.c_record(row)`;
    a one-row or partial table is rejected. The current four final source
    tables contain 68 records each.
    """
    if type(expected_provider_rows) is not int or not 1 <= expected_provider_rows <= 160:
        raise ValueError("expected provider row count must come from the validated candidate inventory")
    if len(c_records) != expected_provider_rows:
        raise ValueError("dispatcher production gate row count differs from validated candidate inventory")
    header = build.generated_provider_bundle_header(source_sha256, product,
                                                    list(c_records)).encode("ascii")
    with tempfile.TemporaryDirectory(prefix="bridge-dispatch-e2e-") as td:
        work = Path(td)
        (work / "bridge_provider_bundle.h").write_bytes(header)
        exe = work / "dispatch-e2e"
        command = [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DBDI_TEST",
                   "-I", str(PROJECT / "src"), "-I", str(work),
                   str(PROJECT / "src/bridge_dispatch.c"),
                   str(PROJECT / "tests/dispatch_e2e_harness.c"), "-o", str(exe)]
        cp = subprocess.run(command, capture_output=True, text=True, timeout=90)
        if cp.returncode:
            raise RuntimeError("production dispatcher gate compile failed: " + cp.stderr[-4000:])
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        if run.returncode:
            raise AssertionError("production dispatcher gate failed: " +
                                 (run.stdout + run.stderr)[-4000:])
        if f"tested_candidates={expected_provider_rows}" not in run.stdout or \
                "production dispatcher complete-table gate PASS" not in run.stdout:
            raise AssertionError("production dispatcher gate omitted required coverage: " + run.stdout)
        normalized_command = [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DBDI_TEST",
            "-I", "<SOURCE>/src", "-I", "<TEMP>", "<SOURCE>/src/bridge_dispatch.c",
            "<SOURCE>/tests/dispatch_e2e_harness.c", "-o", "<TEMP>/dispatch-e2e"]
        return {
            "status": "PASS",
            "source_sha256": source_sha256,
            "product": product,
            "provider_rows": expected_provider_rows,
            "tested_candidates": expected_provider_rows,
            "synthetic_inspector_readiness": True,
            "provider_executables_run": False,
            "compile_command": normalized_command,
            "dispatcher_source_sha256": hashlib.sha256((PROJECT / "src/bridge_dispatch.c").read_bytes()).hexdigest(),
            "dispatcher_header_sha256": hashlib.sha256((PROJECT / "src/bridge_dispatch.h").read_bytes()).hexdigest(),
            "generated_table_sha256": hashlib.sha256(header).hexdigest(),
            "harness_sha256": hashlib.sha256((PROJECT / "tests/dispatch_e2e_harness.c").read_bytes()).hexdigest(),
            "stdout": run.stdout,
            "stderr": run.stderr,
        }


def run_fixture_gate(fixture_path: Path | str | None = None) -> list[dict]:
    path = Path(fixture_path) if fixture_path else PROJECT / "tests/fixtures/dispatch-c-records-v1.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != "dispatcher-c-record-fixtures-v1":
        raise ValueError("unexpected dispatcher fixture schema")
    tables = data.get("tables")
    if not isinstance(tables, list) or len(tables) != 4:
        raise ValueError("expected the four complete final source tables")
    results = []
    for table in tables:
        results.append(run_dispatch_gate(table["source_sha256"], table["product"],
                                         table["c_records"], table["provider_rows"],
                                         os.environ.get("CC", "cc")))
    return results


def main() -> int:
    results = run_fixture_gate(sys.argv[1] if len(sys.argv) > 1 else None)
    print(json.dumps({"status": "PASS", "tables": results}, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
