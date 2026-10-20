# BootBridge offline desktop source

This source subset builds the shared offline-only Windows GUI with an adjacent `BootBridge.rules.dat`. The user selects an OEM firmware file only; exact size and SHA-256 select one unique source bundle, while runtime board-route and NAND checks apply the existing install gates. The GUI does not ask for a hardware class and does not open `.bbprog` files.

The fixed generated header, DAT, and sanitized catalog descriptor are under `offline/generated/`. From the archive root, the supported public rebuild command is `python3 bootbridge-modifier/tools/build-offline-generated.py bootbridge-modifier bootbridge-modifier/offline/generated bootbridge-modifier/offline/generated/bundle-catalog-manifest.json <new-output-dir> --arch x64`. This verifies pinned inputs and statically audits x64; it never runs the PE.

The build-input directory includes one reviewed RX source-identity receipt only. It excludes referenced private captures, OEM images, complete candidate firmware, and private build reports. `.bbprog` files are source/rebuild recipes, not runtime GUI inputs.

The new BootBridge code is GPL-2.0-or-later; see `COPYING` and `LICENSE`. Third-party and upstream files retain their own notices.
