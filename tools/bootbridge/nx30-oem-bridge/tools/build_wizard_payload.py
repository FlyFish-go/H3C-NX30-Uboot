#!/usr/bin/env python3
"""Offline fixed-address D payload build in a new private WSL clone.

The frozen RC1 worktree and tag are never modified. This derivative changes
only the D maintenance network defaults and its version label.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

BASE = "b000aa404e32cb57c94436f6e4a0d07fd32b2150"
IP = "192.168.10.1"


def run(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, **kw)


def replace_once(path, old, new):
    data = path.read_text()
    if data.count(old) != 1:
        raise ValueError("unexpected frozen source: " + str(path))
    path.write_text(data.replace(old, new, 1))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    source = args.source.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() or output.is_relative_to(source):
        raise ValueError("use a new private output directory outside the frozen source")
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != BASE:
        raise ValueError("source must be the frozen RC1 commit")
    if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True):
        raise ValueError("frozen tracked inputs changed")
    output.mkdir(parents=True, mode=0o700)
    clone = output / "source"
    run(["git", "clone", "--no-hardlinks", "--quiet", str(source), clone])
    run(["git", "-C", clone, "checkout", "--quiet", "--detach", BASE])
    u = clone / "u-boot-2016"
    replace_once(u / "include/configs/ipq5018.h",
                 "#ifdef CONFIG_TARGET_IPQ5018_H3C_RT3000_B\n#define CONFIG_IPADDR",
                 "#if defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B) || defined(CONFIG_TARGET_IPQ5018_H3C_HW_D)\n#define CONFIG_IPADDR")
    replace_once(u / "common/autoboot.c",
                 "#elif defined(CONFIG_H3C_DEFAULT_WEB)\n#ifdef CONFIG_H3C_HW_D_RAM_AUTOECC",
                 '#elif defined(CONFIG_H3C_DEFAULT_WEB)\n'
                 '#ifdef CONFIG_TARGET_IPQ5018_H3C_HW_D\n'
                 '    /* Bridge installer profile: fixed RAM-only LAN defaults. */\n'
                 '    if (setenv("ethprime", "eth1") || setenv("ethact", "eth1") ||\n'
                 '        setenv("ethrotate", "no") || setenv("ipaddr", "192.168.10.1") ||\n'
                 '        setenv("netmask", "255.255.255.0") || setenv("serverip", "192.168.10.2")) {\n'
                 '        puts("Bridge LAN setup failed; staying at CLI\\n");\n'
                 '        return;\n'
                 '    }\n'
                 '#endif\n#ifdef CONFIG_H3C_HW_D_RAM_AUTOECC')
    replace_once(u / "configs/ipq5018_h3c_hw_d_production_defconfig",
                 'CONFIG_LOCALVERSION="-h3c-common4"',
                 'CONFIG_LOCALVERSION="-h3c-common4-bridge10"')
    changed = ["u-boot-2016/include/configs/ipq5018.h", "u-boot-2016/common/autoboot.c",
               "u-boot-2016/configs/ipq5018_h3c_hw_d_production_defconfig"]
    patch = subprocess.check_output(["git", "-C", str(clone), "diff", "--", *changed])
    (output / "fixed-lan.patch").write_bytes(patch)
    run(["git", "-C", clone, "diff", "--check"])
    run(["git", "-C", clone, "add", "--", *changed])
    epoch = subprocess.check_output(["git", "-C", str(source), "show", "-s", "--format=%ct", BASE], text=True).strip()
    env = dict(os.environ, GIT_AUTHOR_DATE="@" + epoch + " +0000", GIT_COMMITTER_DATE="@" + epoch + " +0000")
    run(["git", "-C", clone, "-c", "user.name=Codex", "-c", "user.email=codex@localhost",
         "commit", "--quiet", "-m", "Set D bridge maintenance LAN to 192.168.10.1"], env=env)
    commit = subprocess.check_output(["git", "-C", str(clone), "rev-parse", "HEAD"], text=True).strip()
    with (output / "build.log").open("w") as log:
        run(["bash", clone / "build-h3c-common.sh", "D", "production", output / "build"], stdout=log, stderr=subprocess.STDOUT)
    with (output / "package.log").open("w") as log:
        run(["python3", clone / "tools/package_nx30_d_uboot.py", "--elf",
             output / "build/h3c-hw-d-common-production.strip.elf", "--elftombn",
             clone / "u-boot-2016/tools/elftombn.py", "--output-dir", output / "package"], stdout=log, stderr=subprocess.STDOUT)
    manifest = json.loads((output / "package/manifest.json").read_text())
    # The container format is independent of physical target selection. The
    # installer chooses the unselected APPSBL from fresh BOOTCONFIG/MTD data.
    manifest["target"] = {"size": "0x140000", "physical_role": "runtime-verified unselected APPSBL"}
    manifest.update(base_commit=BASE, derivative_commit=commit, ip=IP,
                    network_changes_persistent_env=False, frozen_tree_modified=False)
    (output / "PAYLOAD-REPORT.json").write_text(json.dumps(manifest, indent=2) + "\n")
    assert subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() == BASE
    assert not subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True)
    print(json.dumps({"status": "OFFLINE_PAYLOAD_BUILT", "base_commit": BASE,
                      "derivative_commit": commit, "ip": IP,
                      "bytes": manifest["partition_bytes"], "sha256": manifest["partition_sha256"]}))


if __name__ == "__main__":
    main()
