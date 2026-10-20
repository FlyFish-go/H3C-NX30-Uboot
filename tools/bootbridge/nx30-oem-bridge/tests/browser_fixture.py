#!/usr/bin/env python3
"""Localhost-only, synthetic NAND HTTP service for the production browser test.

Run through test_wizard_browser.js; never opens an actual device or backup.
"""
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

from test_http import fixture, install_fixture
from test_install import write_test_oem_manifest_header

project = Path(__file__).resolve().parents[1]
catalog = Path(os.environ.get("H3C_NAND_CATALOG_HEADER", ""))
if not catalog.is_file():
    raise SystemExit("H3C_NAND_CATALOG_HEADER must point to a shared source header")
with tempfile.TemporaryDirectory(prefix="bridge-ui-browser-") as temp:
    root = Path(temp)
    fixture(root)
    install_fixture(root)
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (root / "dev").iterdir()}
    include = root / "include"
    include.mkdir()
    (include / "h3c_nand_catalog.h").write_bytes(catalog.read_bytes())
    write_test_oem_manifest_header(include)
    binary = root / "bridge-http"
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST", "-I", str(include), "-I", str(project / "src"),
                    str(project / "src/bridge_check.c"), str(project / "src/bridge_install.c"),
                    str(project / "src/bridge_version.c"), str(project / "src/bridge_oem_slot.c"), str(project / "src/bridge_backup.c"), str(project / "src/bridge_root_binding.c"), str(project / "src/bridge_first_install.c"), str(project / "src/bridge_install_identity.c"), str(project / "src/bridge_mtd_observer.c"), "-o", str(binary)], check=True)
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    origin = f"http://127.0.0.1:{port}"
    with subprocess.Popen([str(binary), "-r", str(root), "-b", "127.0.0.1", "-p", str(port),
                           "-w", str(project / "web")], stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE) as proc:
        try:
            deadline = time.monotonic() + 30
            last_error = "no HTTP response"
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read().decode(errors="replace"))
                try:
                    with urllib.request.urlopen(origin + "/api/status", timeout=.5) as r:
                        json.load(r)
                    break
                except (OSError, ValueError) as error:
                    last_error = str(error)
                    time.sleep(.05)
            else:
                raise RuntimeError("fixture HTTP did not start within 30 seconds: " + last_error)
            print(json.dumps({"origin": origin}), flush=True)
            sys.stdin.readline()
            after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (root / "dev").iterdir()}
            changed = [name for name in before if before[name] != after[name]]
            print(json.dumps({"synthetic_changed_partitions": changed,
                              "real_device_operations": False}), flush=True)
        finally:
            proc.terminate()
            proc.wait(timeout=5)
