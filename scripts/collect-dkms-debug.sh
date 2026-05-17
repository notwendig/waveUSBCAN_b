#!/usr/bin/env bash
#
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
# This file is part of waveUSBCAN_b.
# SPDX-License-Identifier: GPL-2.0-only
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG_NAME="$(awk -F'"' '/PACKAGE_NAME/ {print $2}' "$ROOT_DIR/dkms.conf")"
PKG_VER="$(awk -F'"' '/PACKAGE_VERSION/ {print $2}' "$ROOT_DIR/dkms.conf")"
OUT="${HOME}/Downloads/${PKG_NAME}-${PKG_VER}-dkms-debug.txt"

{
  echo "# ${PKG_NAME} ${PKG_VER} DKMS debug"
  date -Is
  echo
  echo "## System"
  uname -a || true
  cat /etc/os-release 2>/dev/null || true
  echo
  echo "## Installed module directories"
  ls -ld /lib/modules/* 2>/dev/null || true
  echo
  echo "## Running kernel build dir"
  ls -ld "/lib/modules/$(uname -r)/build" 2>/dev/null || true
  echo
  echo "## DKMS status"
  dkms status 2>/dev/null || true
  echo
  echo "## waveUSBCAN_b source dkms.conf"
  cat "$ROOT_DIR/dkms.conf" || true
  echo
  echo "## make.log"
  LOG="/var/lib/dkms/${PKG_NAME}/${PKG_VER}/build/make.log"
  if [[ -f "$LOG" ]]; then
    cat "$LOG"
  else
    echo "No make.log found at $LOG"
    find "/var/lib/dkms/${PKG_NAME}" -name make.log -print -exec cat {} \; 2>/dev/null || true
  fi
  echo
  echo "## Recent kernel messages"
  dmesg | tail -n 120 2>/dev/null || true
} | tee "$OUT"

echo "[OK] Wrote $OUT"
