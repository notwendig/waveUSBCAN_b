#!/usr/bin/env bash
#
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
# This file is part of waveUSBCAN_b.
# SPDX-License-Identifier: GPL-2.0-only
#
set -euo pipefail

IFACE="${1:-can0}"
BITRATE="${2:-125000}"

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  exec sudo "$0" "$@"
fi

echo "[*] USB devices matching 04d8:0053"
lsusb | grep -i '04d8:0053\|CANalyst\|USBCAN' || true

echo "[*] Loading module"
modprobe waveusbcan_b || true

echo "[*] Configuring ${IFACE} at ${BITRATE} bit/s"
ip link set "$IFACE" down 2>/dev/null || true
ip link set "$IFACE" type can bitrate "$BITRATE" restart-ms 100
ip link set "$IFACE" up
ip -details link show "$IFACE"

echo "[*] Recent kernel messages"
dmesg | tail -80 | grep -iE 'waveusbcan|canalyst|usbcan|04d8|can[0-9]' || true

echo "[OK] Now run in another shell: candump ${IFACE}"
echo "[OK] Optional TX test on safe bench bus: cansend ${IFACE} 123#1122334455667788"
