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

set +e
modprobe -r waveusbcan_b
set -e

dkms remove -m "$PKG_NAME" -v "$PKG_VER" --all || true
rm -rf "/usr/src/${PKG_NAME}-${PKG_VER}"
rm -f /etc/udev/rules.d/99-waveusbcan-b.rules
udevadm control --reload-rules || true
depmod -a

echo "[OK] Removed ${PKG_NAME} ${PKG_VER}"
