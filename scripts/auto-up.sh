#!/usr/bin/env bash
#
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
# This file is part of waveUSBCAN_b.
# SPDX-License-Identifier: GPL-2.0-only
#
# Automatically configure waveUSBCAN_b SocketCAN interfaces after boot or USB hotplug.
# The script is intentionally receive-safe: it configures CAN netdevices only and never
# transmits CAN frames.
#
set -euo pipefail

BITRATE="125000"
INTERFACES="can0,can1"
WAIT_SECONDS="20"
RESTART_MS="0"
DRY_RUN="0"

usage() {
  cat <<USAGE
Usage: $0 [options]

Options:
  --bitrate N              CAN bitrate for all configured interfaces (default: 125000)
  --interfaces A,B         Comma-separated CAN netdevices (default: can0,can1)
  --wait SECONDS           Wait up to SECONDS for interfaces to appear (default: 20)
  --restart-ms N           SocketCAN bus-off restart time in ms (default: 0)
  --dry-run                Print commands without executing them
  -h, --help               Show this help

Examples:
  $0 --bitrate 125000 --interfaces can0,can1
  $0 --bitrate 500000 --interfaces can0 --wait 5
USAGE
}

log() {
  echo "[waveusbcan_b-auto-up] $*"
  command -v logger >/dev/null 2>&1 && logger -t waveusbcan_b-auto-up -- "$*" || true
}

run() {
  if [[ "$DRY_RUN" == "1" ]]; then
    echo "+ $*"
  else
    "$@"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bitrate)
      BITRATE="${2:?missing bitrate}"
      shift 2
      ;;
    --interfaces)
      INTERFACES="${2:?missing interface list}"
      shift 2
      ;;
    --wait)
      WAIT_SECONDS="${2:?missing wait seconds}"
      shift 2
      ;;
    --restart-ms)
      RESTART_MS="${2:?missing restart-ms value}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN="1"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
 done

if [[ ${EUID:-$(id -u)} -ne 0 && "$DRY_RUN" != "1" ]]; then
  exec sudo "$0" --bitrate "$BITRATE" --interfaces "$INTERFACES" --wait "$WAIT_SECONDS" --restart-ms "$RESTART_MS"
fi

command -v ip >/dev/null 2>&1 || { echo "missing command: ip" >&2; exit 1; }

log "loading CAN core and waveusbcan_b module"
run modprobe can || true
run modprobe can-dev || true
run modprobe waveusbcan_b || true

IFS=',' read -r -a IFACES <<< "$INTERFACES"
found_any=0
configured_any=0

for iface in "${IFACES[@]}"; do
  iface="${iface//[[:space:]]/}"
  [[ -n "$iface" ]] || continue

  log "waiting for ${iface} up to ${WAIT_SECONDS}s"
  found=0
  for ((i=0; i<=WAIT_SECONDS; i++)); do
    if ip link show "$iface" >/dev/null 2>&1; then
      found=1
      found_any=1
      break
    fi
    sleep 1
  done

  if [[ "$found" != "1" ]]; then
    log "${iface} not present; skipping"
    continue
  fi

  log "configuring ${iface}: bitrate=${BITRATE}, restart-ms=${RESTART_MS}"
  run ip link set "$iface" down 2>/dev/null || true
  run ip link set "$iface" type can bitrate "$BITRATE" restart-ms "$RESTART_MS"
  run ip link set "$iface" up
  configured_any=1

  log "${iface} configured"
  ip -details link show "$iface" || true
 done

if [[ "$configured_any" == "1" ]]; then
  log "done: configured ${INTERFACES} at ${BITRATE} bit/s"
  exit 0
fi

if [[ "$found_any" == "0" ]]; then
  log "no requested CAN interfaces appeared; no adapter may be connected"
else
  log "requested interfaces were present but none were configured"
fi

# Keep boot/hotplug service non-fatal when the adapter is absent.
exit 0
