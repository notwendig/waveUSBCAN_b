<!--
SPDX-License-Identifier: GPL-2.0-only
Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
Authors: Jürgen W. Sievers; OpenAI ChatGPT
-->

# Reverse-engineering record

## Attribution

**Jürgen W. Sievers** is credited as the protocol reverse-engineering contributor for the local hardware and R-Net bench work behind this project.

His contributions include:

- identifying the tested USB adapter as `04d8:0053` / Chuangxin Tech USBCAN/CANalyst-II compatible hardware,
- validating the DKMS module against real Fedora kernels and real USB hardware,
- confirming that the adapter exposes multiple bulk endpoint pairs (`0x01/0x81` through `0x06/0x86`) on the tested unit,
- testing `endpoint_profile=auto` and confirming the working command/message endpoint layout for the tested adapter,
- validating native SocketCAN registration of `can0` and `can1`,
- verifying `can0` at `125000` bit/s in `ERROR-ACTIVE` state,
- capturing live R-Net / powered wheelchair CAN traffic from an immobilized, bench-safe wheelchair setup, and
- defining the safety rule that live wheelchair work must begin with passive capture only.

OpenAI ChatGPT assisted by generating the kernel module, scripts, documentation, and iterative fixes from user-supplied logs, captures, and engineering direction.

## Scope of reverse engineering

This project does **not** claim to fully document every CANalyst-II firmware revision or every Waveshare USB-CAN-B compatible clone. The current driver behavior is based on:

- public CANalyst-II protocol information,
- local USB endpoint observations,
- Linux SocketCAN behavior,
- DKMS build results,
- `dmesg` logs,
- `ip -details link show type can` validation,
- `candump` captures, and
- controlled R-Net bench observations.

The protocol notes should therefore be treated as a living engineering record.

## Evidence handling

Useful evidence for future updates includes:

```bash
lsusb -v -d 04d8:0053
lsusb -t
ip -details link show type can
sudo dmesg -T | grep -iE 'waveusbcan|usbcan|canalyst|04d8|0053|endpoint|bulk|can0|can1'
candump -L can0
candump -L can1
```

When capturing a wheelchair bus, prefer passive logging:

```bash
candump -L can0 > ~/Downloads/rnet-passive-capture.log
```

Do not publish logs containing private serial numbers, medical-device identifiers, or personal information without review.

## Safety boundary

Powered wheelchair buses are safety-critical. Reverse-engineering work must follow these rules:

1. Start with passive capture.
2. Keep the chair immobilized and unoccupied during bench tests.
3. Do not transmit guessed frames to a live wheelchair bus.
4. Use active TX only on an isolated bench harness or explicit loopback setup.
5. Document the exact bitrate, endpoint profile, kernel version, and module parameters used for each capture.

## Known-good local milestone

The local validation milestone for this project is:

```text
USB adapter  : 04d8:0053 Chuangxin Tech USBCAN/CANalyst-II compatible
Driver       : waveusbcan_b
Interface    : can0
Bitrate      : 125000 bit/s
CAN state    : ERROR-ACTIVE
Use case     : passive R-Net / powered wheelchair bench capture
```
