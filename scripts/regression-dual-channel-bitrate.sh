#!/usr/bin/env bash
#
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
# This file is part of waveUSBCAN_b.
# SPDX-License-Identifier: GPL-2.0-only
#
# waveUSBCAN_b dual-channel / bitrate regression test
#
# Safe default: passive configuration + RX capture only. No CAN frames are
# transmitted unless BOTH --allow-tx and --bench-loopback are given.

set -u -o pipefail

IFACES_CSV="can0,can1"
BITRATES_CSV="125000"
RX_SECONDS="3"
RESTART_MS="100"
OUTDIR=""
ALLOW_TX=0
BENCH_LOOPBACK=0
KEEP_UP=0
TX_ID_BASE="5A0"
TX_COUNT="1"
TX_DELAY="0.05"
VERBOSE=0
LOAD_MODULE=1
ENDPOINT_PROFILE="auto"
MODULE_OPTS="endpoint_profile=auto autopm_on_open=0 command_ep_autodetect=1 usb_timeout_ms=1000 usb_retries=5"

usage() {
  cat <<'USAGE'
Usage:
  sudo ./scripts/regression-dual-channel-bitrate.sh [options]

Safe passive defaults:
  - tests can0 and can1 at 125000 bit/s
  - sets each interface down/type/up
  - captures a short candump per interface
  - does NOT transmit any CAN frame

Options:
  --ifaces can0,can1             Interfaces to test. Default: can0,can1
  --bitrates 125000,250000       Comma-separated bitrate list. Default: 125000
  --rx-seconds N                 Passive candump duration per interface. Default: 3
  --restart-ms N                 SocketCAN restart-ms value. Default: 100
  --outdir PATH                  Report/log directory. Default: ~/Downloads/waveusbcan_b-regression-<timestamp>
  --keep-up                      Leave interfaces up at the last tested bitrate.
  --no-load-module               Do not reload/load waveusbcan_b before tests.
  --module-opts 'opts...'        modprobe options. Default: known-good v0.1.7+ options.
  --endpoint-profile NAME        Shorthand for module endpoint_profile. Default: auto
  --verbose                      Print more commands.

Bench-only active TX/RX test:
  --allow-tx --bench-loopback    Enable generic test-frame TX between can0 and can1.
                                  Use ONLY with both channels wired to the same isolated
                                  bench CAN bus or direct CH0<->CH1 harness.
  --tx-id-base HEX               Base CAN ID for bench frames. Default: 5A0
  --tx-count N                   Frames per direction/bitrate. Default: 1
  --tx-delay SEC                 Delay between TX frames. Default: 0.05

Examples:
  # Safe R-Net/Rollstuhl passive check at 125 kbit/s:
  sudo ./scripts/regression-dual-channel-bitrate.sh --bitrates 125000

  # Passive bench bitrate sweep on both channels, no TX:
  sudo ./scripts/regression-dual-channel-bitrate.sh --bitrates 50000,100000,125000,250000,500000,1000000

  # Active two-channel bench test. Do NOT run on a live wheelchair bus:
  sudo ./scripts/regression-dual-channel-bitrate.sh --bitrates 125000,250000,500000 --allow-tx --bench-loopback
USAGE
}

log() { printf '%s\n' "$*" | tee -a "$SUMMARY"; }
# helper kept for optional verbose command tracing
# shellcheck disable=SC2329
run() {
  if [[ "$VERBOSE" == "1" ]]; then echo "+ $*" | tee -a "$SUMMARY"; fi
  "$@"
}
fail_count=0
pass_count=0
warn_count=0

mark_pass() { pass_count=$((pass_count + 1)); log "[PASS] $*"; }
mark_fail() { fail_count=$((fail_count + 1)); log "[FAIL] $*"; }
mark_warn() { warn_count=$((warn_count + 1)); log "[WARN] $*"; }


split_csv() {
  local csv="$1"
  csv="${csv// /}"
  IFS=',' read -r -a __split_out <<< "$csv"
}

is_iface_present() {
  ip link show "$1" >/dev/null 2>&1
}

# diagnostic helper kept for manual debugging
# shellcheck disable=SC2329
iface_state_line() {
  ip -details link show "$1" 2>/dev/null | tr '\n' ' ' | sed 's/[[:space:]][[:space:]]*/ /g'
}

iface_has_bitrate() {
  local iface="$1" br="$2"
  ip -details link show "$iface" 2>/dev/null | grep -q "bitrate ${br}"
}

capture_iface() {
  local iface="$1" br="$2" out="$3" seconds="$4"
  if ! command -v candump >/dev/null 2>&1; then
    mark_warn "candump missing; skipped RX capture for ${iface} @ ${br}"
    return 0
  fi
  timeout --preserve-status "${seconds}" candump -L "$iface" >"$out" 2>"${out}.err"
  local rc=$?
  # timeout returns 143/124 depending on signal/preserve-status; both are fine.
  if [[ "$rc" == "0" || "$rc" == "124" || "$rc" == "143" ]]; then
    local frames
    frames=$(grep -c '^(' "$out" 2>/dev/null || true)
    mark_pass "RX capture ${iface} @ ${br}: ${frames} frame(s) in ${seconds}s -> ${out}"
  else
    mark_warn "RX capture ${iface} @ ${br}: candump rc=${rc}; stderr=$(tr '\n' ' ' <"${out}.err" 2>/dev/null)"
  fi
}

configure_iface() {
  local iface="$1" br="$2"
  local logfile="$3"
  {
    echo "### configure ${iface} bitrate=${br}"
    date -Is
    ip link set "$iface" down 2>/dev/null || true
    ip link set "$iface" type can bitrate "$br" restart-ms "$RESTART_MS"
    ip link set "$iface" up
    ip -details link show "$iface"
  } >"$logfile" 2>&1
  local rc=$?
  if [[ "$rc" -ne 0 ]]; then
    mark_fail "configure ${iface} @ ${br} failed rc=${rc}; see ${logfile}"
    return "$rc"
  fi
  if iface_has_bitrate "$iface" "$br"; then
    mark_pass "configured ${iface} @ ${br}"
  else
    mark_fail "${iface} is up attempt but bitrate ${br} not visible; see ${logfile}"
    return 1
  fi
}

active_pair_test() {
  local br="$1" tx_a="$2" rx_b="$3" tx_b="$4" rx_a="$5" outprefix="$6"
  if [[ "$ALLOW_TX" != "1" || "$BENCH_LOOPBACK" != "1" ]]; then
    return 0
  fi
  if ! command -v cansend >/dev/null 2>&1 || ! command -v candump >/dev/null 2>&1; then
    mark_warn "cansend/candump missing; skipped active bench TX/RX @ ${br}"
    return 0
  fi

  local ida idb data_a data_b
  # Keep standard IDs, below 0x7ff, and far away from common R-Net low control IDs.
  ida=$(printf "%03X" $((0x${TX_ID_BASE} & 0x7F0)))
  idb=$(printf "%03X" $(((0x${TX_ID_BASE} + 0x0F) & 0x7FF)))
  data_a=$(printf "A1%06X" "$br" | cut -c1-8)
  data_b=$(printf "B2%06X" "$br" | cut -c1-8)

  local cap_ab="${outprefix}_${tx_a}_to_${rx_b}.log"
  local cap_ba="${outprefix}_${tx_b}_to_${rx_a}.log"

  timeout $((RX_SECONDS + 2)) candump -L "$rx_b" >"$cap_ab" 2>"${cap_ab}.err" &
  local pid_ab=$!
  timeout $((RX_SECONDS + 2)) candump -L "$rx_a" >"$cap_ba" 2>"${cap_ba}.err" &
  local pid_ba=$!
  sleep 0.4

  for ((i=0; i<TX_COUNT; i++)); do
    cansend "$tx_a" "${ida}#${data_a}" >>"${outprefix}_tx.log" 2>&1 || true
    sleep "$TX_DELAY"
    cansend "$tx_b" "${idb}#${data_b}" >>"${outprefix}_tx.log" 2>&1 || true
    sleep "$TX_DELAY"
  done

  sleep 0.5
  kill "$pid_ab" "$pid_ba" >/dev/null 2>&1 || true
  wait "$pid_ab" >/dev/null 2>&1 || true
  wait "$pid_ba" >/dev/null 2>&1 || true

  if grep -Eq "(^|[[:space:]])${ida}#" "$cap_ab"; then
    mark_pass "active bench ${tx_a}->${rx_b} @ ${br}: saw ID ${ida}"
  else
    mark_fail "active bench ${tx_a}->${rx_b} @ ${br}: did not see ID ${ida}; see ${cap_ab}"
  fi
  if grep -Eq "(^|[[:space:]])${idb}#" "$cap_ba"; then
    mark_pass "active bench ${tx_b}->${rx_a} @ ${br}: saw ID ${idb}"
  else
    mark_fail "active bench ${tx_b}->${rx_a} @ ${br}: did not see ID ${idb}; see ${cap_ba}"
  fi
}

if [[ ${EUID:-$(id -u)} -ne 0 ]]; then
  exec sudo "$0" "$@"
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --ifaces) IFACES_CSV="$2"; shift 2 ;;
    --bitrates) BITRATES_CSV="$2"; shift 2 ;;
    --rx-seconds) RX_SECONDS="$2"; shift 2 ;;
    --restart-ms) RESTART_MS="$2"; shift 2 ;;
    --outdir) OUTDIR="$2"; shift 2 ;;
    --keep-up) KEEP_UP=1; shift ;;
    --no-load-module) LOAD_MODULE=0; shift ;;
    --module-opts) MODULE_OPTS="$2"; shift 2 ;;
    --endpoint-profile) ENDPOINT_PROFILE="$2"; shift 2 ;;
    --allow-tx) ALLOW_TX=1; shift ;;
    --bench-loopback) BENCH_LOOPBACK=1; shift ;;
    --tx-id-base) TX_ID_BASE="$2"; shift 2 ;;
    --tx-count) TX_COUNT="$2"; shift 2 ;;
    --tx-delay) TX_DELAY="$2"; shift 2 ;;
    --verbose) VERBOSE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$OUTDIR" ]]; then
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
    USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
  else
    USER_HOME="${HOME:-/root}"
  fi
  OUTDIR="${USER_HOME}/Downloads/waveusbcan_b-regression-$(date +%Y%m%d-%H%M%S)"
fi
mkdir -p "$OUTDIR"
SUMMARY="${OUTDIR}/summary.txt"
: >"$SUMMARY"

split_csv "$IFACES_CSV"; IFACES=("${__split_out[@]}")
split_csv "$BITRATES_CSV"; BITRATES=("${__split_out[@]}")

if [[ "$ENDPOINT_PROFILE" != "auto" ]]; then
  MODULE_OPTS="endpoint_profile=${ENDPOINT_PROFILE} autopm_on_open=0 command_ep_autodetect=1 usb_timeout_ms=1000 usb_retries=5"
fi

log "waveUSBCAN_b dual-channel/bitrate regression"
log "started: $(date -Is)"
log "outdir: ${OUTDIR}"
log "ifaces: ${IFACES[*]}"
log "bitrates: ${BITRATES[*]}"
log "rx_seconds: ${RX_SECONDS}"
log "active_tx: allow_tx=${ALLOW_TX} bench_loopback=${BENCH_LOOPBACK}"
log ""

if [[ "$ALLOW_TX" == "1" && "$BENCH_LOOPBACK" != "1" ]]; then
  mark_fail "--allow-tx requires --bench-loopback. Refusing active TX."
  exit 2
fi

if [[ "$ALLOW_TX" == "1" ]]; then
  log "SAFETY: active TX is enabled. Use only on isolated bench CAN wiring, not on a live wheelchair/R-Net bus."
  log "Sleeping 5 seconds before TX tests; press Ctrl+C to abort."
  sleep 5
fi

{
  echo "### system"
  date -Is
  uname -a
  echo
  echo "### loaded module"
  lsmod | grep -E '^waveusbcan_b\b' || true
  echo
  echo "### dkms"
  dkms status 2>/dev/null | grep -i waveUSBCAN_b || true
  echo
  echo "### usb"
  lsusb | grep -iE '04d8:0053|CANalyst|USBCAN|waveshare|chuangxin' || true
  lsusb -t || true
  echo
  echo "### ip type can before"
  ip -details link show type can || true
  echo
  echo "### module info"
  modinfo waveusbcan_b 2>/dev/null || true
} >"${OUTDIR}/environment.txt" 2>&1

if [[ "$LOAD_MODULE" == "1" ]]; then
  log "[*] Reloading waveusbcan_b with: ${MODULE_OPTS}"
  modprobe -r waveusbcan_b >/dev/null 2>&1 || true
  # shellcheck disable=SC2086
  if modprobe waveusbcan_b ${MODULE_OPTS}; then
    mark_pass "module loaded"
  else
    mark_fail "module load failed"
  fi
  sleep 2
fi

for iface in "${IFACES[@]}"; do
  if is_iface_present "$iface"; then
    mark_pass "interface present: ${iface}"
  else
    mark_fail "interface missing: ${iface}"
  fi
done

# Passive per-interface bitrate sweep.
for br in "${BITRATES[@]}"; do
  log ""
  log "=== passive bitrate ${br} ==="
  for iface in "${IFACES[@]}"; do
    if ! is_iface_present "$iface"; then
      mark_fail "skip ${iface} @ ${br}: interface missing"
      continue
    fi
    cfg_log="${OUTDIR}/${iface}_${br}_configure.log"
    if configure_iface "$iface" "$br" "$cfg_log"; then
      rx_log="${OUTDIR}/${iface}_${br}_rx.log"
      capture_iface "$iface" "$br" "$rx_log" "$RX_SECONDS"
    fi
  done

  # Optional active pair test only when at least two ifaces are present.
  if [[ "$ALLOW_TX" == "1" && "$BENCH_LOOPBACK" == "1" && "${#IFACES[@]}" -ge 2 ]]; then
    log ""
    log "=== active bench pair bitrate ${br} ==="
    a="${IFACES[0]}"; b="${IFACES[1]}"
    if is_iface_present "$a" && is_iface_present "$b"; then
      configure_iface "$a" "$br" "${OUTDIR}/${a}_${br}_active_configure.log" || true
      configure_iface "$b" "$br" "${OUTDIR}/${b}_${br}_active_configure.log" || true
      active_pair_test "$br" "$a" "$b" "$b" "$a" "${OUTDIR}/active_${br}"
    fi
  fi
done

{
  echo "### ip type can after"
  ip -details link show type can || true
  echo
  echo "### dmesg tail"
  dmesg -T | grep -iE 'waveusbcan|usbcan|canalyst|04d8|0053|can0|can1|COMMAND_|bulk|autopm' | tail -n 240 || true
} >"${OUTDIR}/after.txt" 2>&1

if [[ "$KEEP_UP" != "1" ]]; then
  for iface in "${IFACES[@]}"; do
    ip link set "$iface" down 2>/dev/null || true
  done
  log "[*] Interfaces brought down. Use --keep-up to leave them up."
fi

log ""
log "finished: $(date -Is)"
log "summary: pass=${pass_count} warn=${warn_count} fail=${fail_count}"
log "report directory: ${OUTDIR}"

if [[ "$fail_count" -gt 0 ]]; then
  exit 1
fi
exit 0
