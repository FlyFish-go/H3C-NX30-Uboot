# Offline Windows bundle build

This build produces a separate GUI that accepts one local OEM BIN, identifies it by exact size and SHA-256, and exports only when the catalogue contains one audited combined output bundle. The GUI has no model or hardware selector. Multiple separate outputs for one source are rejected as ambiguous. It reads only its adjacent `BootBridge.rules.dat` and exports through the shared local C engine. The build does not compile downloader, transport, or OTA sources and does not execute the generated Windows program.

With MinGW-w64 x86 and x64 cross compilers already installed, run from the workspace:

```sh
sh projects/bootbridge-modifier/tools/build-windows-offline.sh x64 /path/to/audited-provider-bundle/RESULT.json
sh projects/bootbridge-modifier/tools/build-windows-offline.sh x86 /path/to/audited-provider-bundle/RESULT.json
```

The input must be an audited `bootbridge-provider-bundle-build-result-v1` PASS manifest. Each row pins the exact OEM source path, source identity, bundle report, independent audit, and combined candidate. Preparation reads the exact source and target and compiles a COPY/XOR/LITERAL delta; only delta bytes enter `BootBridge.rules.dat`, never complete source or target images. The default output is `projects/bootbridge-modifier/build/offline-windows-bundle-new/`; the script refuses to reuse an existing output path. Set `BOOTBRIDGE_OFFLINE_OUTPUT_ROOT` to a new dedicated directory. Each architecture directory contains the executable, adjacent delta data, PE import listing, source and bundle evidence hashes, and output hashes. The script checks a GUI PE subsystem version of 6.01 and limits imported DLLs to common Windows UI/runtime modules; this static import audit cannot establish runtime behavior or Windows-version compatibility.

The embedded application manifest requests `asInvoker`. Build success and import checks do not constitute Windows execution, antivirus scanning, hardware compatibility, or installation validation. Do not rename detected legacy executables or create antivirus exclusions.

## Rebuild from a published generated catalog

A source archive can rebuild the desktop executable without OEM or candidate BIN files. Include the frozen source snapshot, `bb_offline_catalog_generated.h`, `BootBridge.rules.dat`, and the matching `bundle-catalog-manifest.json`; the manifest must retain the exact header and sidecar byte/SHA pins. From that archive, run:

```sh
python3 tools/build-offline-generated.py /path/to/source-snapshot/desktop /path/to/generated-catalog /path/to/bundle-catalog-manifest.json /path/to/new-output --project-root /path/to/source-snapshot --source-manifest /path/to/source-snapshot/desktop/SOURCE-MANIFEST.json --arch x64
```

Use `--arch all` to cross-build both x86 and x64. The command refuses an existing output directory, verifies every source-snapshot hash and the generated header/sidecar pins, records the toolchain and output hashes, and performs static PE import checks. It does not regenerate bundle rules, read OEM/candidate firmware, or execute a PE. A report stating `execution: not run` records only a cross-build and static import audit.

The project root and desktop source root are deliberately distinct. Version 2 source manifests use project-relative paths (`desktop/...` and `design/...`); version 1 manifests remain desktop-relative. `--project-root` and `--source-manifest` make that choice explicit and fail closed when a required path is absent.

Pass absolute paths for `--project-root` and `--source-manifest`; relative values are resolved against the caller's current directory, not the script directory.
