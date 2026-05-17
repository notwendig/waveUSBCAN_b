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

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG_NAME="$(awk -F'"' '/PACKAGE_NAME/ {print $2}' "$ROOT_DIR/dkms.conf")"
PKG_VER="$(awk -F'"' '/PACKAGE_VERSION/ {print $2}' "$ROOT_DIR/dkms.conf")"
KVER="$(uname -r)"

echo "[*] Rebuilding ${PKG_NAME}/${PKG_VER} for running kernel ${KVER}"
dkms remove -m "$PKG_NAME" -v "$PKG_VER" -k "$KVER" || true
dkms build -m "$PKG_NAME" -v "$PKG_VER" -k "$KVER"
dkms install -m "$PKG_NAME" -v "$PKG_VER" -k "$KVER"
depmod -a "$KVER"
modprobe -r waveusbcan_b 2>/dev/null || true
modprobe waveusbcan_b
echo "[OK] Rebuilt and loaded waveusbcan_b"
