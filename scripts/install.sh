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
SRC_DIR="/usr/src/${PKG_NAME}-${PKG_VER}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing command: $1" >&2
    exit 1
  }
}

need_cmd dkms
need_cmd make
need_cmd depmod

RUNNING_KERNEL="$(uname -r)"
NEWEST_KERNEL="$(basename -a /lib/modules/* 2>/dev/null | sort -V | tail -n1 || true)"
if [[ -n "$NEWEST_KERNEL" && "$NEWEST_KERNEL" != "$RUNNING_KERNEL" ]]; then
  echo "[!] Running kernel: ${RUNNING_KERNEL}"
  echo "[!] Newest installed kernel appears to be: ${NEWEST_KERNEL}"
  echo "[!] If DKMS build fails, reboot into the newest kernel and run this installer again."
fi

if [[ ! -d "/lib/modules/$(uname -r)/build" ]]; then
  echo "Missing kernel build directory: /lib/modules/$(uname -r)/build" >&2
  echo "Install kernel headers/devel for the running kernel first." >&2
  exit 1
fi

echo "[*] Installing ${PKG_NAME} ${PKG_VER} into ${SRC_DIR}"
rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"

if command -v rsync >/dev/null 2>&1; then
  rsync -a --delete \
    --exclude='.git' --exclude='*.ko' --exclude='*.o' --exclude='*.mod' \
    --exclude='*.mod.c' --exclude='Module.symvers' --exclude='modules.order' \
    "$ROOT_DIR/" "$SRC_DIR/"
else
  cp -a "$ROOT_DIR/." "$SRC_DIR/"
fi

if dkms status -m "$PKG_NAME" -v "$PKG_VER" >/dev/null 2>&1; then
  echo "[*] Removing previous DKMS instance"
  dkms remove -m "$PKG_NAME" -v "$PKG_VER" --all || true
fi

echo "[*] dkms add/build/install"
dkms add -m "$PKG_NAME" -v "$PKG_VER"
if ! dkms build -m "$PKG_NAME" -v "$PKG_VER"; then
  LOG="/var/lib/dkms/${PKG_NAME}/${PKG_VER}/build/make.log"
  echo
  echo "[!] DKMS build failed." >&2
  echo "[!] Please inspect: ${LOG}" >&2
  if [[ -f "$LOG" ]]; then
    echo
    echo "========== DKMS make.log tail ==========" >&2
    tail -n 120 "$LOG" >&2 || true
    echo "========================================" >&2
  fi
  exit 2
fi
dkms install -m "$PKG_NAME" -v "$PKG_VER"

if [[ -f "$SRC_DIR/udev/99-waveusbcan-b.rules" ]]; then
  echo "[*] Installing udev rule"
  install -m 0644 "$SRC_DIR/udev/99-waveusbcan-b.rules" /etc/udev/rules.d/99-waveusbcan-b.rules
  udevadm control --reload-rules || true
fi

if [[ -f "$SRC_DIR/modprobe.d/waveusbcan_b.conf" && ! -f /etc/modprobe.d/waveusbcan_b.conf ]]; then
  echo "[*] Installing default modprobe config"
  install -m 0644 "$SRC_DIR/modprobe.d/waveusbcan_b.conf" /etc/modprobe.d/waveusbcan_b.conf
fi

depmod -a
modprobe can || true
modprobe can-dev || true

echo "[*] Loading module waveusbcan_b"
modprobe waveusbcan_b || {
  echo "Module build/install completed, but modprobe failed. Check: dmesg | tail -80" >&2
  exit 1
}

echo "[OK] Installed and loaded waveusbcan_b"
echo "Next: unplug/replug adapter, then run: ip -details link show type can"
