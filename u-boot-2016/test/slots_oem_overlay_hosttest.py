#!/usr/bin/env python3
from pathlib import Path
SLOTS = (Path(__file__).resolve().parents[2] / "u-boot-2016/failsafe/modules/slots_rt3000_b.c").read_text()
UBIFS_CMD = (Path(__file__).resolve().parents[2] / "u-boot-2016/common/cmd_ubifs.c").read_text()
UBIFS_IMPL = (Path(__file__).resolve().parents[2] / "u-boot-2016/fs/ubifs/ubifs.c").read_text()
for forbidden in ("dashboard_", "rootfs_data", "sysupgrade.tgz", "ubifs_ro_", "RC3000_Version", "RC3000-defVersion"):
    assert forbidden not in SLOTS, forbidden
assert "ubifs_ro_mount" in UBIFS_CMD
assert "ubifs_ro_read_path" in UBIFS_IMPL
print("PASS: slot scanner has no dashboard/region/overlay reads; independent read-only UBIFS helpers remain available")
