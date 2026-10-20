#!/usr/bin/env python3
"""Reproducibly cross-build the offline GUI from pinned generated catalog files.

Consumes only the frozen source snapshot, generated header/sidecar, and their
catalog manifest. It never opens or rebuilds official or candidate firmware.
The PE files are never executed.
"""
import argparse
import hashlib
import json
import locale
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


TARGETS = {
    "x64": ("x86_64-w64-mingw32", "pei-x86-64"),
    "x86": ("i686-w64-mingw32", "pei-i386"),
}


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(argv, stdout=None):
    print("+", " ".join(str(x) for x in argv), flush=True)
    subprocess.run(argv, check=True, stdout=stdout)


def command_info(path, args):
    proc = subprocess.run([path, *args], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    raw = proc.stdout
    return {"path": path, "sha256": sha(Path(path).resolve()), "version_output_sha256": hashlib.sha256(raw).hexdigest(),
            "version_first_line": raw.decode("utf-8", "replace").splitlines()[0] if raw else ""}


def snapshot_path(root, project_root, schema, rel):
    row_path = Path(rel)
    if row_path.is_absolute() or ".." in row_path.parts:
        raise ValueError("unsafe source file: " + rel)
    if schema == "bootbridge-offline-source-snapshot-v2":
        parts = row_path.parts
        if parts[:2] == ("design", "desktop-offline-ui"):
            base, suffix = project_root, row_path
        elif parts[:1] == ("desktop",):
            base, suffix = root, Path(*parts[1:])
        else:
            raise ValueError("v2 source path lacks desktop/ or design/desktop-offline-ui/ prefix: " + rel)
    else:
        base, suffix = root, row_path
    return (base / suffix).resolve(strict=True), base


def verify_source_snapshot(root, project_root, manifest_path=None):
    manifest_path = (manifest_path or (root / "SOURCE-MANIFEST.json")).resolve(strict=True)
    raw = manifest_path.read_bytes()
    manifest = json.loads(raw)
    if manifest.get("schema") not in ("bootbridge-offline-source-snapshot-v1",
                                      "bootbridge-offline-source-snapshot-v2") or manifest.get("status") != "SOURCE_ONLY":
        raise ValueError("source snapshot manifest is invalid")
    rows = manifest.get("files")
    if not isinstance(rows, list) or len(rows) != manifest.get("source_count"):
        raise ValueError("source snapshot file list is invalid")
    inputs = {}
    for row in rows:
        path, base = snapshot_path(root, project_root, manifest.get("schema"), row["path"])
        if base not in path.parents or path.is_symlink() or not path.is_file():
            raise ValueError("unsafe source file: " + row["path"])
        if path.stat().st_size != row["bytes"] or sha(path) != row["sha256"]:
            raise ValueError("source snapshot mismatch: " + row["path"])
        key = row["path"]
        if manifest.get("schema") == "bootbridge-offline-source-snapshot-v2":
            parts = Path(key).parts
            if parts[:1] == ("desktop",):
                key = Path(*parts[1:]).as_posix()
            elif parts[:2] == ("design", "desktop-offline-ui"):
                key = row["path"]
            else:
                raise ValueError("v2 source path lacks an approved prefix: " + row["path"])
        inputs[key] = row["sha256"]
    required = {
        "gui/offline_main.c", "gui/bb_ui_text.c", "gui/bb_ui_text.h",
        "gui/resources.rc", "gui/app.manifest",
        "gui/test_offline_gui_contract.py",
        "offline/bb_offline_rules.c", "offline/bb_offline_rules.h",
        "src/bb_export.c", "src/bb_export.h", "src/bb_export_plan.h", "src/bridge_sha256.h",
        "catalog/bb_catalog.c", "catalog/bb_catalog.h", "tools/check-offline-pe.sh",
        "tools/build-windows-offline.sh", "tools/build-offline-generated.py",
        "tools/gen_ui_strings.py", "tools/gen_guide_models.py", "guide-models.json",
        "design/desktop-offline-ui/strings.json",
        "src/bb_fetch.c", "src/bb_fetch.h", "src/bb_net.h", "src/bb_net_policy.c", "src/bb_net_win.c",
        "src/official_api.c", "src/official_api.h", "gui/host-tests/test_bb_net_policy.c",
    }
    missing = sorted(required - set(inputs))
    if missing:
        raise ValueError("source snapshot is incomplete: " + ", ".join(missing))
    design_strings = (project_root / "design/desktop-offline-ui/strings.json").resolve(strict=True)
    if project_root not in design_strings.parents or not design_strings.is_file() or design_strings.is_symlink():
        raise ValueError("design string source is unsafe or missing")
    if "design/desktop-offline-ui/strings.json" not in inputs:
        raise ValueError("design string source is missing from the source snapshot")
    return raw, inputs


def build(source_root, generated_dir, manifest_path, output_root, architectures, project_root=None,
          source_manifest_path=None):
    source_root = source_root.resolve(strict=True)
    generated_dir = generated_dir.resolve(strict=True)
    manifest_path = manifest_path.resolve(strict=True)
    project_root = (project_root or source_root.parent).resolve(strict=True)
    if output_root.exists() or output_root.is_symlink():
        raise ValueError("refusing existing output root: " + str(output_root))
    source_manifest_raw, source_inputs = verify_source_snapshot(source_root, project_root, source_manifest_path)
    catalog_raw = manifest_path.read_bytes()
    catalog = json.loads(catalog_raw)
    if (catalog.get("schema") != "bootbridge-offline-bundle-catalog-build-v1" or
            catalog.get("status") != "PASS" or catalog.get("catalog_schema") != "bootbridge-offline-bundle-catalog-v1"):
        raise ValueError("generated catalog manifest is not PASS")
    generated = catalog.get("generated", {})
    header_spec, dat_spec = generated.get("header", {}), generated.get("sidecar", {})
    header, sidecar = generated_dir / "bb_offline_catalog_generated.h", generated_dir / "BootBridge.rules.dat"
    for path, spec, label in ((header, header_spec, "header"), (sidecar, dat_spec, "sidecar")):
        if (not path.is_file() or path.is_symlink() or path.stat().st_size != spec.get("bytes") or
                sha(path) != spec.get("sha256")):
            raise ValueError("generated catalog " + label + " does not match its pinned manifest")
    if dat_spec.get("data_bytes_are") != "BBDP XOR/LITERAL deltas only":
        raise ValueError("catalog sidecar does not attest delta-only contents")
    strings_source = project_root / "design/desktop-offline-ui/strings.json"
    models_source = source_root / "guide-models.json"

    output_root.mkdir(parents=True)
    build_rows = []
    strings = generated_dir / "bb_ui_strings_generated.h"
    models = generated_dir / "bb_guide_models_generated.h"
    for arch in architectures:
        prefix, _ = TARGETS[arch]
        cc, objdump, windres = (shutil.which(prefix + "-" + suffix) for suffix in ("gcc", "objdump", "windres"))
        if not cc or not objdump or not windres:
            raise ValueError("missing MinGW tools for " + arch)
        out = output_root / arch
        out.mkdir()
        generated_copy = out / "generated-catalog"
        generated_copy.mkdir()
        shutil.copyfile(header, generated_copy / header.name)
        shutil.copyfile(sidecar, generated_copy / sidecar.name)
        for path, label in ((strings, "UI strings"), (models, "guide models")):
            if not path.is_file() or path.is_symlink():
                raise ValueError("generated " + label + " header is missing")
        pins = [
            (strings, strings_source, "BB_UI_STRINGS_SOURCE_SHA256", "UI strings"),
            (models, models_source, "BB_GUIDE_MODELS_SOURCE_SHA256", "guide models"),
        ]
        for header_path, source_path, macro, label in pins:
            match = re.search(r'#define\s+' + macro + r'\s+L"([0-9a-f]{64})"',
                              header_path.read_text(encoding="utf-8"))
            if not match or match.group(1) != sha(source_path):
                raise ValueError("generated " + label + " header does not match its source")
        shutil.copyfile(strings, generated_copy / strings.name)
        shutil.copyfile(models, generated_copy / models.name)
        resource_obj = out / "resources.o"
        run([windres, "-I", str(source_root / "gui"), str(source_root / "gui/resources.rc"), str(resource_obj)])
        exe = out / ("BootBridge-Offline-" + arch + ".exe")
        run([cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wformat=2",
             "-D_WIN32_WINNT=0x0601", "-DWINVER=0x0601", "-DUNICODE", "-D_UNICODE",
             "-D__USE_MINGW_ANSI_STDIO=1", "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
             "-I" + str(generated_copy), "-I" + str(source_root / "offline"),
             "-I" + str(source_root / "catalog"), "-I" + str(source_root / "src"),
             "-municode", "-mwindows", "-static", "-static-libgcc",
             "-Wl,--major-subsystem-version,6,--minor-subsystem-version,1,--no-insert-timestamp",
             str(source_root / "gui/offline_main.c"), str(source_root / "gui/bb_ui_text.c"),
             str(source_root / "offline/bb_offline_rules.c"),
             str(source_root / "src/bb_export.c"), str(source_root / "catalog/bb_catalog.c"),
             str(source_root / "src/bb_fetch.c"), str(source_root / "src/bb_net_policy.c"),
             str(source_root / "src/bb_net_win.c"), str(source_root / "src/official_api.c"),
             str(resource_obj), "-lcomdlg32", "-lcomctl32", "-lgdi32", "-lgdiplus", "-luser32",
             "-lshell32", "-ladvapi32", "-ldwmapi", "-lversion", "-lwinhttp", "-o", str(exe)])
        shutil.copyfile(sidecar, out / "BootBridge.rules.dat")
        imports = out / "pe-imports.txt"
        with imports.open("w", encoding="utf-8") as stream:
            run([objdump, "-p", str(exe)], stdout=stream)
        run(["sh", str(source_root / "tools/check-offline-pe.sh"), arch, str(exe)])
        toolchain = {"compiler": command_info(cc, ["--version"]),
                     "objdump": command_info(objdump, ["--version"]),
                     "windres": command_info(windres, ["--version"])}
        build_rows.append({
            "architecture": arch,
            "toolchain": toolchain,
            "execution": "not run",
            "pe_import_audit": "passed; see pe-imports.txt",
            "generated_ui": {
                "strings_sha256": sha(strings),
                "guide_models_sha256": sha(models),
            },
            "outputs": {
                "exe": {"name": exe.name, "bytes": exe.stat().st_size, "sha256": sha(exe)},
                "sidecar": {"name": "BootBridge.rules.dat", "bytes": (out / "BootBridge.rules.dat").stat().st_size,
                            "sha256": sha(out / "BootBridge.rules.dat")},
            },
        })
    manifest = {
        "schema": "bootbridge-offline-reproducible-crossbuild-v1",
        "status": "PASS_STATIC_BUILD_ONLY",
        "build_script_sha256": sha(Path(__file__).resolve()),
        "source_snapshot_manifest_sha256": hashlib.sha256(source_manifest_raw).hexdigest(),
        "source_sha256": source_inputs,
        "generated_catalog_manifest_sha256": hashlib.sha256(catalog_raw).hexdigest(),
        "generated_catalog": {"schema": catalog["catalog_schema"],
                              "header_sha256": header_spec["sha256"],
                              "sidecar_bytes": dat_spec["bytes"], "sidecar_sha256": dat_spec["sha256"]},
        "generated_ui": {
            "strings": {"name": strings.name, "bytes": strings.stat().st_size, "sha256": sha(strings)},
            "guide_models": {"name": models.name, "bytes": models.stat().st_size, "sha256": sha(models)},
        },
        "execution": "not run",
        "build_environment": {
            "locale_preferred_encoding": locale.getpreferredencoding(False),
            "LC_ALL": os.environ.get("LC_ALL", ""), "LANG": os.environ.get("LANG", ""),
            "TZ": os.environ.get("TZ", ""), "SOURCE_DATE_EPOCH": os.environ.get("SOURCE_DATE_EPOCH", ""),
            "timestamp_linking": "disabled by --no-insert-timestamp",
        },
        "builds": build_rows,
    }
    env_raw = json.dumps(manifest["build_environment"], sort_keys=True, separators=(",", ":")).encode()
    manifest["build_environment_sha256"] = hashlib.sha256(env_raw).hexdigest()
    toolchain_raw = json.dumps({b["architecture"]: b["toolchain"] for b in build_rows}, sort_keys=True, separators=(",", ":")).encode()
    manifest["toolchain_environment_sha256"] = hashlib.sha256(toolchain_raw).hexdigest()
    (output_root / "build-manifest.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source_root", type=Path)
    parser.add_argument("generated_dir", type=Path)
    parser.add_argument("catalog_manifest", type=Path)
    parser.add_argument("output_root", type=Path)
    parser.add_argument("--project-root", type=Path,
                        help="project root for v2 manifests and design/ inputs")
    parser.add_argument("--source-manifest", type=Path,
                        help="explicit SOURCE-MANIFEST.json (default: source_root/SOURCE-MANIFEST.json)")
    parser.add_argument("--arch", choices=("x86", "x64", "all"), default="all")
    args = parser.parse_args()
    try:
        architectures = ["x86", "x64"] if args.arch == "all" else [args.arch]
        manifest = build(args.source_root, args.generated_dir, args.catalog_manifest, args.output_root,
                         architectures, args.project_root, args.source_manifest)
    except Exception as exc:
        print("offline generated-catalog build refused: " + str(exc), file=sys.stderr)
        return 2
    for row in manifest["builds"]:
        print(row.get("architecture", "") + ": " + row.get("execution", ""))
    print(json.dumps(manifest["builds"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
