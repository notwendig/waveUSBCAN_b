#!/usr/bin/env bash
#
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
# This file is part of waveUSBCAN_b.
# SPDX-License-Identifier: GPL-2.0-only
#
set -euo pipefail

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  exec sudo "$0" "$@"
fi

IFACES=("${@:-can0}")
if [[ $# -eq 0 ]]; then
  IFACES=(can0)
fi

modprobe can || true
modprobe can-dev || true
modprobe waveusbcan_b || true

for IFACE in "${IFACES[@]}"; do
  echo "[*] Configuring ${IFACE} for R-Net / 125000 bit/s"
  ip link set "$IFACE" down 2>/dev/null || true
  ip link set "$IFACE" type can bitrate 125000 restart-ms 100
  ip link set "$IFACE" up
  ip -details link show "$IFACE"
done

echo "[OK] R-Net CAN setup done. Test with: candump ${IFACES[0]}"
