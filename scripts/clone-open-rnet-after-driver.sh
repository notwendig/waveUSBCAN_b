#!/usr/bin/env bash
#
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
# This file is part of waveUSBCAN_b.
# SPDX-License-Identifier: GPL-2.0-only
#
set -euo pipefail

TARGET_DIR="${1:-$HOME/Projects/open-rnet}"
IFACE="${2:-can0}"

mkdir -p "$(dirname "$TARGET_DIR")"
if [[ ! -d "$TARGET_DIR/.git" ]]; then
  git clone https://github.com/redragonx/open-rnet.git "$TARGET_DIR"
else
  git -C "$TARGET_DIR" pull --ff-only
fi

sudo "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/setup-rnet-125k.sh" "$IFACE"

echo "[OK] open-rnet is in: $TARGET_DIR"
echo "[OK] CAN interface ready: $IFACE"
