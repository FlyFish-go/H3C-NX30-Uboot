#!/bin/sh
# Volatile diagnostic boot using the pinned source's own wired module closure.
# bridge_init is PID 1 and has already mounted only RAM-backed runtime paths.

PATH=/sbin:/bin:/usr/sbin:/usr/bin
export PATH

plan() {
	cat <<'EOF'
require BRIDGE_INIT_VERIFIED=H3C-BOOTBRIDGE-RUNTIME-v1
require BRIDGE_RUNTIME_MODE=RUNTIME_GATED
insmod /lib/modules/4.4.60/qca-ssdk.ko
insmod /lib/modules/4.4.60/qca-nss-dp.ko
require readable /lib/firmware/qca-nss0.bin (builder link to qca-nss0-retail.bin)
insmod /lib/modules/4.4.60/qca-nss-drv.ko
insmod /lib/modules/4.4.60/input-core.ko
insmod /lib/modules/4.4.60/button-hotplug.ko
insmod /lib/modules/4.4.60/gpio-button-hotplug.ko
ifconfig lo 127.0.0.1 up
ifconfig eth0 down
ifconfig eth1 192.168.10.1 netmask 255.255.255.0 up
EOF
}

if [ "${1:-}" = "--plan" ]; then
	plan
	exit 0
fi

[ "${BRIDGE_INIT_VERIFIED:-}" = "H3C-BOOTBRIDGE-RUNTIME-v1" ] || {
	echo "bridge-boot: must be launched by the verified bridge PID 1" >&2
	exit 125
}
[ "${BRIDGE_RUNTIME_MODE:-}" = "RUNTIME_GATED" ] || {
	echo "bridge-boot: diagnostic runtime mode missing" >&2
	exit 125
}
[ "$(cat /etc/bridge/profile 2>/dev/null)" = "H3C-BOOTBRIDGE-RUNTIME-v1" ] || {
	echo "bridge-boot: profile marker mismatch" >&2
	exit 1
}
[ "$(cat /etc/bridge/rootfs-id 2>/dev/null)" = "H3C-BOOTBRIDGE-ROOTFS-v1" ] || {
	echo "bridge-boot: rootfs marker mismatch" >&2
	exit 1
}

# Reviewed sources' modules.d entries establish this candidate wired path.
# This route is not a hardware classifier and does not qualify installation.
# No global kmodloader, radio, Bluetooth, bootconfig, MACsec, or OEM startup
# script is run. The builder provides insmod as a link to the OEM kmodloader.
[ -r /lib/firmware/qca-nss0.bin ] || {
	echo "bridge-boot: missing curated qca-nss0.bin firmware link" >&2
	exit 1
}
for module in qca-ssdk qca-nss-dp qca-nss-drv; do
	case "$module" in
		qca-ssdk) sysname=qca_ssdk ;;
		qca-nss-dp) sysname=qca_nss_dp ;;
		qca-nss-drv) sysname=qca_nss_drv ;;
		*) echo "bridge-boot: module outside explicit allowlist" >&2; exit 1 ;;
	esac
	if [ ! -d "/sys/module/$sysname" ]; then
		/usr/sbin/insmod "/lib/modules/4.4.60/$module.ko" || {
			echo "bridge-boot: could not load $module" >&2
			exit 1
		}
	fi
done

# Match the official firmware's gpio-keys driver and uevent delivery path.
for module in input-core button-hotplug gpio-button-hotplug; do
	case "$module" in
		input-core) sysname=input_core ;;
		button-hotplug) sysname=button_hotplug ;;
		gpio-button-hotplug) sysname=gpio_button_hotplug ;;
	esac
	if [ ! -d "/sys/module/$sysname" ]; then
		/usr/sbin/insmod "/lib/modules/4.4.60/$module.ko" ||
			echo "bridge-boot: button driver $module failed; use the web return button" >&2
	fi
done

ifconfig lo 127.0.0.1 up || { echo "bridge-boot: cannot configure loopback" >&2; exit 1; }
# Do not request an address, route, or DNS service on WAN.
ifconfig eth0 down 2>/dev/null || :
ifconfig eth1 192.168.10.1 netmask 255.255.255.0 up || {
	echo "bridge-boot: cannot configure eth1 maintenance LAN" >&2
	exit 1
}

exit 0
