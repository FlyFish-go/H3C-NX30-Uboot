#!/usr/bin/env python3
"""Exercise built ARM userspace in a synthetic root. No kernel/MTD simulation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))
from test_http import fixture

EXPECTED_BOOT_PLAN = """require BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1
require BRIDGE_RUNTIME_MODE=RUNTIME_GATED
insmod /lib/modules/4.4.60/qca-ssdk.ko
insmod /lib/modules/4.4.60/qca-nss-dp.ko
require readable /lib/firmware/qca-nss0.bin (builder link to qca-nss0-retail.bin)
insmod /lib/modules/4.4.60/qca-nss-drv.ko
ifconfig lo 127.0.0.1 up
ifconfig eth0 down
ifconfig eth1 192.168.10.1 netmask 255.255.255.0 up
"""
EXPECTED_PID1_PLAN = """PID1 guard: getpid()==1; neutral /etc/bridge/profile and rootfs-id match; uname -r=4.4.60
mount -t proc -o nosuid,noexec,nodev proc /proc
mount -t sysfs -o nosuid,noexec,nodev sysfs /sys
check DT bridge marker via /proc/device-tree or /sys/firmware/devicetree/base
check reviewed DT compatible reference route; hardware UNKNOWN; RUNTIME_GATED
mount -t tmpfs -o nosuid tmpfs /dev; create console,null,zero,tty,urandom nodes
create only validated /dev/mtdN character nodes from numeric /sys/class/mtd/mtdN/dev major:minor values
mount -t tmpfs -o nosuid,nodev,size=64m tmpfs /tmp; create /tmp/run and /tmp/bridge
exec /bin/sh /etc/bridge/boot.sh once; failure => console message and PID1 stays alive
success => start /usr/sbin/bridge-dhcp -i eth1 -s 192.168.10.1 if present and /usr/sbin/bridge-check -b 192.168.10.1 -p 80 -w /www concurrently
waitpid(-1) supervises both children with bounded per-service retry delays; DHCP failure leaves static web LAN running
"""
EXPECTED_DHCP_SELFTEST = "selftest PASS: OFFER, matching and retransmitted ACK, bound DISCOVER lease retention, expiry sweep, foreign-server and malformed-cookie ignore\n"
EXPECTED_PID1_GUARD = "bridge-init: refusing runtime actions because this is not PID 1\n"


def boot_plan_command():
    # Nested ARM applets must not rely on host binfmt. Keep boot.sh unchanged.
    return ["/bin/busybox", "sh", "-c",
            'cat() { /qemu-arm /bin/busybox cat "$@"; }; set -- --plan; . /etc/bridge/boot.sh']


def validate_cli_run(label, result):
    """Pin both output channels and status for each bounded ARM CLI contract."""
    expected = {
        "pid1_plan": (0, EXPECTED_PID1_PLAN, ""),
        "pid1_host_guard": (125, "", EXPECTED_PID1_GUARD),
        "dhcp_packets": (0, EXPECTED_DHCP_SELFTEST, ""),
        "oem_busybox_shell_syntax": (0, "", ""),
        "boot_plan": (0, EXPECTED_BOOT_PLAN, ""),
    }
    if label not in expected:
        raise ValueError("unknown ARM CLI contract: " + label)
    actual = (result.get("rc"), result.get("stdout"), result.get("stderr"))
    if actual != expected[label]:
        raise ValueError(label + ": ARM exit status or exact stdout/stderr contract mismatch: " + repr(actual))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit("use a new output directory")
    args.output.mkdir(parents=True, mode=0o700)
    root = args.output / "guest"
    shutil.copytree(args.build / "rootfs", root, symlinks=True)
    for mountpoint in ("proc", "dev", "tmp"):
        (root / mountpoint).mkdir(exist_ok=True)
    fixture(root / "fixture")
    (root / "qemu-arm").touch()
    qemu, bwrap = shutil.which("qemu-arm-static"), shutil.which("bwrap")
    assert qemu and bwrap
    bundle = json.loads((args.build / "BUILD-REPORT.json").read_text())
    if bundle.get("schema") != "bootbridge-provider-bundle-v2":
        raise SystemExit("expected an ABI2 provider bundle BUILD-REPORT.json")

    def qemu_command(guest_binary, argv, share_net=False):
        return [bwrap, "--unshare-all", *( ["--share-net"] if share_net else []),
                "--die-with-parent", "--new-session", "--ro-bind", str(root), "/",
                "--ro-bind", qemu, "/qemu-arm", "--proc", "/proc", "--dev", "/dev",
                "--tmpfs", "/tmp", "--chdir", "/", "--setenv", "PATH",
                "/bin:/sbin:/usr/sbin", "/qemu-arm", guest_binary, *argv]

    def prefix(share_net=False):
        return [bwrap, "--unshare-all", *(["--share-net"] if share_net else []),
                "--die-with-parent", "--new-session", "--ro-bind", str(root), "/",
                "--ro-bind", qemu, "/qemu-arm", "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--chdir", "/",
                "--setenv", "PATH", "/bin:/sbin:/usr/sbin", "/qemu-arm"]

    runs = {}
    for label, argv in [
        ("pid1_plan", ["/sbin/init", "--plan"]),
        ("pid1_host_guard", ["/sbin/init"]),
        ("dhcp_packets", ["/usr/sbin/bridge-dhcp", "--selftest"]),
        ("oem_busybox_shell_syntax", ["/bin/busybox", "sh", "-n", "/etc/bridge/boot.sh"]),
        ("boot_plan", boot_plan_command()),
    ]:
        result = subprocess.run([*prefix(), *argv], capture_output=True, text=True, timeout=10)
        runs[label] = {"rc": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
        try:
            validate_cli_run(label, runs[label])
        except ValueError as error:
            (args.output / "RESULT.json").write_text(json.dumps({
                "status": "FAIL_ARM_USERSPACE_FIXTURES", "device_operations": False,
                "runs": runs, "failed_check": label, "error": str(error),
            }, indent=2) + "\n")
            raise SystemExit(str(error))
    # ABI2 dispatcher's nested ARM fexecve is not supported by QEMU user mode
    # without host binfmt_misc. Execute every pinned provider and the independent
    # helper directly under QEMU, using their production default-root path.
    source_sha = bundle["source_identity"]["sha256"]
    provider_rows = bundle.get("providers")
    if not isinstance(provider_rows, list) or not provider_rows:
        raise AssertionError("ABI2 bundle has no providers")
    provider_runs = []
    for row in provider_rows:
        provider = row.get("provider", {})
        guest_path = provider.get("path", "")
        if not guest_path.startswith("/usr/sbin/bridge-candidates/") or not guest_path.endswith("/provider"):
            raise AssertionError("provider path is outside the ABI2 provider tree")
        binary = root / guest_path.lstrip("/")
        if not binary.is_file() or binary.is_symlink():
            raise AssertionError("pinned provider is missing or not a regular file: " + guest_path)
        actual_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
        if actual_sha != provider.get("sha256") or binary.stat().st_size != provider.get("bytes"):
            raise AssertionError("provider bytes differ from the bundle report: " + guest_path)
        result = subprocess.run(qemu_command(guest_path, ["--bridge-dispatch-inspect"]),
                                 capture_output=True, text=True, timeout=20)
        if result.returncode != 1 or result.stderr or len(result.stdout) > 8192:
            raise AssertionError("direct ARM provider inspection did not fail closed: " +
                                 repr((result.returncode, result.stdout[-300:], result.stderr[-300:])))
        state = json.loads(result.stdout)
        expected = {"source_sha256": source_sha, "product": row.get("product"),
                    "route": row.get("route"), "hardware_class": row.get("hardware_class"),
                    "candidate_id": row.get("candidate_id")}
        for key, value in expected.items():
            if state.get(key) != value:
                raise AssertionError(f"provider {key} mismatch for {guest_path}: {state.get(key)!r} != {value!r}")
        if state.get("install_ready") is not False or not state.get("reason"):
            raise AssertionError("provider did not report hardware unavailable with a reason")
        provider_runs.append({"hardware_class": row["hardware_class"],
                              "candidate_id": row["candidate_id"], "path": guest_path,
                              "sha256": actual_sha, "qemu_exit": result.returncode,
                              "install_ready": False, "reason": state["reason"]})
    runs["arm_providers_direct_qemu"] = provider_runs

    helper = bundle.get("return_helper", {})
    helper_path, helper_rel = helper.get("path"), helper.get("binary_path")
    if helper_path != "/usr/sbin/bridge-oem-return" or helper_rel != "rootfs" + helper_path:
        raise AssertionError("helper bundle path mapping differs from its guest path")
    helper_binary = root / helper_path.lstrip("/")
    if not helper_binary.is_file() or helper_binary.is_symlink():
        raise AssertionError("independent OEM-return helper is missing from ABI2 rootfs")
    helper_sha = hashlib.sha256(helper_binary.read_bytes()).hexdigest()
    if helper_sha != helper.get("sha256") or helper_binary.stat().st_size != helper.get("bytes"):
        raise AssertionError("OEM-return helper bytes differ from the bundle report")
    helper_result = subprocess.run(qemu_command(helper_path, ["--bridge-dispatch-inspect"]),
                                    capture_output=True, text=True, timeout=20)
    if helper_result.returncode != 1 or helper_result.stderr or len(helper_result.stdout) > 8192:
        raise AssertionError("direct ARM OEM-return helper inspection failed: " +
                             repr((helper_result.returncode, helper_result.stdout[-300:],
                                   helper_result.stderr[-300:])))
    helper_state = json.loads(helper_result.stdout)
    for key, expected_type in (("install_ready", bool), ("return_ready", bool),
                               ("source_sha256", str), ("product", str), ("route", str),
                               ("hardware_class", str), ("candidate_id", str), ("reason", str)):
        if not isinstance(helper_state.get(key), expected_type):
            raise AssertionError("return helper inspection schema field malformed: " + key)
    if helper_state.get("install_ready") is not False or not helper_state.get("reason"):
        raise AssertionError("return helper did not fail closed without target hardware")
    runs["arm_return_helper_direct_qemu"] = {"path": helper_path, "sha256": helper_sha,
        "qemu_exit": helper_result.returncode, "install_ready": False,
        "source_sha256_report_pin": source_sha, "reason": helper_state["reason"]}
    dispatcher = bundle.get("dispatcher", {})
    dispatcher_path = dispatcher.get("path")
    if (dispatcher_path != "/usr/sbin/bridge-check" or
            dispatcher.get("binary_path") != "rootfs" + dispatcher_path):
        raise AssertionError("dispatcher bundle path mapping differs from its guest path")
    dispatcher_binary = root / dispatcher_path.lstrip("/")
    if not dispatcher_binary.is_file() or dispatcher_binary.is_symlink():
        raise AssertionError("ABI2 dispatcher is missing from bundle rootfs")
    dispatcher_sha = hashlib.sha256(dispatcher_binary.read_bytes()).hexdigest()
    if dispatcher_sha != dispatcher.get("sha256") or dispatcher_binary.stat().st_size != dispatcher.get("bytes"):
        raise AssertionError("dispatcher bytes differ from the bundle report")
    runs["dispatcher_artifact_pin"] = {"path": dispatcher.get("path"),
        "sha256": dispatcher_sha, "bytes": dispatcher_binary.stat().st_size,
        "execution": "not run as dispatcher under QEMU; nested ARM fexecve lacks host binfmt_misc"}
    runs["dispatcher_fexecve_qemu"] = {"status": "NOT_TESTED",
        "reason": "QEMU user mode lacks host ARM binfmt_misc for nested fexecve; see host dispatcher regression separately"}

    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    # Only sharing the host network permits a host client to reach loopback.
    # The server code contains no outbound connect(), and binds 127.0.0.1.
    selected_provider = provider_rows[0]["provider"]["path"]
    process = subprocess.Popen([*prefix(True), selected_provider, "-r", "/fixture",
                                "-b", "127.0.0.1", "-p", str(port), "-w", "/www"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        url = f"http://127.0.0.1:{port}"
        for _ in range(100):
            if process.poll() is not None:
                raise RuntimeError("ARM HTTP server exited: " + process.stderr.read().decode())
            try:
                with opener.open(url + "/api/check", timeout=.2) as response:
                    observed = json.load(response)
                break
            except OSError:
                time.sleep(.03)
        else:
            raise RuntimeError("ARM fixture HTTP startup timeout")
        assert observed["profile"] == "H3C-BOOTBRIDGE-MULTICONFIG-V1"
        assert not observed["install_available"]
        assert observed["wizard"]["phase"] in ("blocked", "checking")
        assert "fixture_root" in observed["wizard"]["blocked_reason"]
        with opener.open(url + "/api/status", timeout=3) as response:
            status = json.load(response)
        assert not status["install_ready"]
        assets = {}
        for path, source in [("/", "index.html"), ("/main.js", "main.js"), ("/style.css", "style.css")]:
            with opener.open(url + path, timeout=3) as response:
                assets[path] = response.read() == (root / "www" / source).read_bytes()
            assert assets[path]
        request = urllib.request.Request(url + "/api/check", data=b"forbidden", method="POST")
        try:
            opener.open(request, timeout=3)
            raise AssertionError("POST accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 405
        runs["arm_provider_http_fixture_guard"] = {"provider_path": selected_provider,
                            "fixture_checks": observed["checks"], "assets_identical": assets,
                            "POST": 405, "install_available": status["install_ready"],
                            "status_phase": status["phase"]}
    finally:
        process.terminate()
        process.wait(timeout=5)
        process.stderr.close()
    report = {"status": "PASS_ARM_ABI2_PROVIDER_AND_HELPER_USERSPACE", "device_operations": False,
              "dispatcher_fexecve_qemu": "NOT_TESTED: QEMU user lacks host ARM binfmt_misc; dispatcher boundary is a separate host regression",
              "runs": runs, "not_executed": ["ARM Linux kernel boot", "NAND drivers", "physical LAN",
                  "real DHCP sockets", "QEMU nested ARM dispatcher fexecve"]}
    (args.output / "RESULT.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "runs": list(runs), "device_operations": False}))


if __name__ == "__main__":
    main()
