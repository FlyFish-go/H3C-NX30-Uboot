#!/usr/bin/env python3
import pathlib, subprocess, tempfile
repo=pathlib.Path(__file__).resolve().parents[2]
uboot=repo/"u-boot-2016"
source=uboot/"test"/"hw_a_mac_readonly_hosttest.c"
module=(uboot/"failsafe/modules/mac_h3c_hw_a.c").read_text()
makefile=(uboot/"failsafe/modules/Makefile").read_text()
mainjs=(uboot/"failsafe/fsdata/main.js").read_text()
gate=(uboot/"include/h3c_hw_a_readonly.h").read_text()
assert "obj-$(CONFIG_FAILSAFE_MAC_MANAGEMENT) += mac_h3c_hw_a.o" in makefile
assert 'eth_getenv_enetaddr("ethaddr", mac)' in module
assert "read_data_from_nand" not in module and "read_data_from_spi" not in module
assert "malloc(" not in module
assert "writable" in module and "persistent" in module and "read_only" in module
assert 'save or verification failed' in module and 'response->info.code=ret?500:200' in module
assert '"/mac/set"' in gate
assert "data.writable === true && data.persistent === true" in mainjs
assert "if (data.read_only === true && data.available === false)" in mainjs
assert "mac.status.read_only" in mainjs and "mac.status.unavailable" in mainjs
assert "warningSection.hidden = !!data && data.read_only === true" in mainjs
with tempfile.TemporaryDirectory(prefix="hw-a-mac-") as temp:
 binary=pathlib.Path(temp)/"hw-a-mac-hosttest"
 subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror",str(source),"-o",str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print("HW-A MAC read-only handler/UI integration: PASS")
