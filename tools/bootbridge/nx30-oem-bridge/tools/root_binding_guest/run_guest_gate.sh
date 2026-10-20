#!/bin/bash
set -euo pipefail

if [[ $# -lt 4 || $# -gt 5 ]]; then
    echo "usage: $0 QEMU_BIN CAPSULE_DIR MODEL OUTPUT_DIR [QEMU_DATA_DIR]" >&2
    exit 2
fi
qemu=$(realpath -e -- "$1")
capsule=$(realpath -e -- "$2")
model=$3
out=$(realpath -m -- "$4")
qemu_data=${5:-}
[[ -x "$qemu" ]] || { echo "QEMU binary is not executable" >&2; exit 2; }
[[ -f "$capsule/capsule.json" && -f "$capsule/vmlinuz" && -f "$capsule/initrd.img" ]] || {
    echo "capsule directory is incomplete" >&2; exit 2;
}
[[ "$model" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "invalid model label" >&2; exit 2; }
mkdir -p "$out"
out=$(realpath -e -- "$out")
log="$out/$model.qemu.log"
receipt="$out/$model.gate.json"
[[ ! -e "$log" && ! -e "$receipt" ]] || {
    echo "refusing to overwrite existing R3 attempt; choose a fresh OUTPUT_DIR" >&2
    exit 2
}

python3 - "$capsule/capsule.json" "$capsule/vmlinuz" "$capsule/initrd.img" <<'PY'
import hashlib, json, pathlib, sys
c=json.load(open(sys.argv[1], encoding="utf-8"))
assert c.get("schema") == "r3-live-gluebi-qemu-capsule-v1"
assert c.get("status") == "PREPARED"
assert c.get("bridge_ubi", {}).get("sha256")
assert c.get("generated_contract_header", {}).get("sha256")
assert hashlib.sha256(pathlib.Path(sys.argv[2]).read_bytes()).hexdigest() == c["kernel"]["sha256"]
assert hashlib.sha256(pathlib.Path(sys.argv[3]).read_bytes()).hexdigest() == c["capsule_initrd_sha256"]
modules=c.get("guest_modules", {})
assert modules.get("schema") == "r3-guest-module-set-v1" and modules.get("status") == "BUILT"
assert set(modules.get("modules", {})) == {
    "gluebi_guest.ko", "r3_mtd_slots.ko", "mtd_blkdevs_guest.ko", "mtdblock_guest.ko"
}
PY

kernel_sha=$(sha256sum "$capsule/vmlinuz" | awk '{print $1}')
initrd_sha=$(sha256sum "$capsule/initrd.img" | awk '{print $1}')
cmdline="console=ttyS0,115200n8 earlyprintk=ttyS0 rdinit=/init root=mtd:ubi_rootfs rootfstype=squashfs ubi.mtd=rootfs ro r3.model=$model"
args=(
    -machine pc,accel=tcg
    -cpu qemu64
    -m 1024
    -smp 1
    -nodefaults
    -no-user-config
    -no-reboot
    -nic none
    -nographic
    -monitor none
    -serial stdio
    -kernel "$capsule/vmlinuz"
    -initrd "$capsule/initrd.img"
    -append "$cmdline"
)
if [[ -n "$qemu_data" ]]; then
    qemu_data=$(realpath -e -- "$qemu_data")
    if [[ -f "$qemu_data/bios-256k.bin" ]]; then
        args+=(-L "$qemu_data")
    elif [[ -f "$qemu_data/../seabios/bios.bin" ]]; then
        args+=(-L "$qemu_data" -bios "$(realpath -e -- "$qemu_data/../seabios/bios.bin")")
    else
        echo "QEMU BIOS is absent from supplied QEMU data directory" >&2
        exit 2
    fi
fi

printf 'QEMU_ISOLATION no_host_block=1 no_host_mtd=1 no_nic=1 no_usb=1 no_shared_directory=1 guest_writes=nandsim_only\n' >"$log"
printf 'QEMU_COMMAND' >>"$log"
printf ' %q' "$qemu" "${args[@]}" >>"$log"
printf '\n' >>"$log"
set +e
timeout --signal=TERM --kill-after=10s 180s "$qemu" "${args[@]}" >>"$log" 2>&1
qemu_rc=$?
set -e

gate_line=$(grep -a '^R3_GATE ' "$log" | tail -n 1 || true)
if [[ "$qemu_rc" -eq 124 || "$qemu_rc" -eq 137 ]]; then
    status=FAIL
    reason=guest-timeout
elif [[ "$gate_line" == *"status=PASS"* && "$qemu_rc" -eq 0 ]]; then
    status=PASS
    reason=production-validator-passed-on-live-guest-facts
else
    status=FAIL
    reason=missing-pass-marker-or-qemu-failure
fi
log_sha=$(sha256sum "$log" | awk '{print $1}')
python3 - "$receipt" "$status" "$reason" "$qemu_rc" "$qemu" "$model" "$cmdline" "$kernel_sha" "$initrd_sha" "$log" "$log_sha" "$gate_line" "$capsule/capsule.json" <<'PY'
import json, os, sys
(out, status, reason, qemu_rc, qemu, model, cmdline, kernel_sha, initrd_sha,
 log, log_sha, gate_line, capsule_path) = sys.argv[1:]
c=json.load(open(capsule_path, encoding="utf-8"))
obj={
  "schema":"r3-live-gluebi-qemu-gate-v1",
  "status":status,
  "reason":reason,
  "model":model,
  "provider_executables_run":False,
  "synthetic_sysfs_or_mount_evidence":False,
  "qemu":{"path":os.path.realpath(qemu),"returncode":int(qemu_rc),"acceleration":"tcg","network":"none","host_block_devices":[],"host_mtd_devices":[],"shared_directories":[],"usb_passthrough":False},
  "guest":{"kernel_sha256":kernel_sha,"initrd_sha256":initrd_sha,"cmdline":cmdline},
  "input_bridge_ubi":c["bridge_ubi"],
  "ubi_layout":c["ubi_layout"],
  "generated_contract_header":c["generated_contract_header"],
  "production_probe_sha256":c["production_probe_sha256"],
  "observed_gate_line":gate_line,
  "qemu_log":{"path":os.path.realpath(log),"sha256":log_sha},
  "isolation_command_policy":{"nodefaults":True,"nic_none":True,"no_drive":True,"no_device_forwarding":True,"no_virtfs":True,"no_host_mtd_or_block_passthrough":True}
}
with open(out,"w",encoding="utf-8") as f:
  json.dump(obj,f,indent=2,sort_keys=True); f.write("\n")
PY
cat "$receipt"
[[ "$status" == PASS ]]
