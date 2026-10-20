#!/usr/bin/env python3
"""Run the ABI2 ARM userspace acceptance tool for four source bundles."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
PRODUCTS = ("RC3000", "RT3000", "RW3000", "NX30")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundles-root", type=Path, required=True,
                        help="directory containing RC3000, RT3000, RW3000, NX30 bundle outputs")
    parser.add_argument("--output", type=Path, required=True,
                        help="new output directory; existing evidence is never overwritten")
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("use a new output directory")
    args.output.mkdir(parents=True, mode=0o700)

    results = []
    for product in PRODUCTS:
        bundle_dir = args.bundles_root / product
        build_report = bundle_dir / "BUILD-REPORT.json"
        if not build_report.is_file():
            results.append({"product": product, "status": "MISSING_BUILD_REPORT"})
            continue
        report = json.loads(build_report.read_text())
        child_output = args.output / product
        child = subprocess.run([sys.executable, str(PROJECT / "tools/test_arm_userspace.py"),
                                "--build", str(bundle_dir), "--output", str(child_output)],
                               capture_output=True, text=True, timeout=1800)
        (args.output / (product + ".stdout.log")).write_text(child.stdout)
        (args.output / (product + ".stderr.log")).write_text(child.stderr)
        child_report_path = child_output / "RESULT.json"
        child_report = json.loads(child_report_path.read_text()) if child_report_path.is_file() else None
        results.append({"product": product, "bundle_report_sha256": sha256(build_report),
                        "bundle_status": report.get("status"), "exit_code": child.returncode,
                        "result_status": child_report.get("status") if child_report else None,
                        "result_path": child_report_path.relative_to(args.output).as_posix()
                        if child_report else None,
                        "result_sha256": sha256(child_report_path) if child_report_path.is_file() else None})

    passed = (len(results) == len(PRODUCTS) and all(
        row.get("exit_code") == 0 and
        row.get("result_status") == "PASS_ARM_ABI2_PROVIDER_AND_HELPER_USERSPACE"
        for row in results))
    result = {"status": "PASS_ARM_ABI2_FOUR_SOURCE_USERSPACE" if passed else "FAIL_ARM_ABI2_FOUR_SOURCE_USERSPACE",
              "device_operations": False, "dispatcher_fexecve_qemu": "NOT_TESTED",
              "dispatcher_note": "QEMU user mode lacks host ARM binfmt_misc for nested fexecve; see separate host dispatcher regression",
              "results": results}
    result_path = args.output / "RESULT.json"
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "results": results}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
