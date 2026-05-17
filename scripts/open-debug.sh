#!/usr/bin/env bash
#
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
# This file is part of waveUSBCAN_b.
# SPDX-License-Identifier: GPL-2.0-only
#
set -u

IFACE="${1:-can0}"
BITRATE="${2:-125000}"
PROFILE="${3:-auto}"

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  exec sudo "$0" "$@"
fi

echo "[*] Reloading waveusbcan_b with endpoint_profile=${PROFILE}"
ip link set "$IFACE" down 2>/dev/null || true
modprobe -r waveusbcan_b 2>/dev/null || true
modprobe waveusbcan_b endpoint_profile="$PROFILE" debug_open=1 usb_retries=5 usb_timeout_ms=1000 autopm_on_open=0 command_ep_autodetect=1

echo "[*] If the adapter was already plugged in, wait 2 seconds for probe"
sleep 2

echo "[*] Interfaces"
ip -details link show type can || true

echo "[*] Configure bitrate: ${IFACE} ${BITRATE}"
set -x
ip link set "$IFACE" down 2>/dev/null || true
ip link set "$IFACE" type can bitrate "$BITRATE"
RET_SET=$?
ip link set "$IFACE" up
RET_UP=$?
set +x

echo "[*] return: set-bitrate=${RET_SET}, up=${RET_UP}"
ip -details link show "$IFACE" || true

echo "[*] Recent waveusbcan_b dmesg"
dmesg -T | grep -iE 'waveusbcan|usbcan|canalyst|04d8|0053|can[0-9]|COMMAND_|bulk OUT|bulk IN|open_candev' | tail -n 160 || true
exit $(( RET_SET != 0 ? RET_SET : RET_UP ))
