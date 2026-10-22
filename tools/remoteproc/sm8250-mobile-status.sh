#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Read-only collection: no reset, NV write, module reload or bearer creation.
set -u

run() {
    printf '\n---'
    printf ' %s' "$@"
    printf ' ---\n'
    if command -v "$1" >/dev/null 2>&1; then
        "$@" 2>&1 || printf 'Command failed: %s\n' "$1"
    else
        printf 'Not installed: %s\n' "$1"
    fi
}

run uname -a
printf '\n--- Device model ---\n'
if [ -r /sys/firmware/devicetree/base/model ]; then
    tr '\000' '\n' < /sys/firmware/devicetree/base/model
fi
run lspci -nn
printf '\n--- MHI devices and bound drivers ---\n'
for device in /sys/bus/mhi/devices/*; do
    [ -e "$device" ] || continue
    printf '%s driver=' "${device##*/}"
    if [ -L "$device/driver" ]; then
        readlink "$device/driver"
    else
        printf '(unbound)\n'
    fi
    for state in ee state; do
        [ ! -r "$device/$state" ] || cat "$device/$state"
    done
done
printf '\n--- WWAN control nodes ---\n'
for port in /dev/wwan*; do
    [ ! -e "$port" ] || ls -l "$port"
done
run ip -details link show
run ip address show
run ip route show table all
run ip -6 route show table all
run qrtr-lookup
run timeout 15 mmcli -L
run timeout 15 nmcli device status
printf '\n--- Modem services ---\n'
if command -v systemctl >/dev/null 2>&1; then
    systemctl --no-pager --full status ModemManager qrtr-ns \
        mdm-helper-native mhi-efs-sync qrtr-tftp-server ipa-qmi-proxy 2>&1 || :
fi
printf '\n--- Relevant kernel messages ---\n'
dmesg 2>&1 | grep -Ei 'mhi|sdx55|esoc|modem|qrtr|rmnet|ipa|sahara|firmware|pcie' || :
