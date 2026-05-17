<!--
SPDX-License-Identifier: GPL-2.0-only
Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
Authors: Jürgen W. Sievers; OpenAI ChatGPT
-->

# waveUSBCAN_b protocol notes

## Reverse-engineering attribution

Protocol reverse-engineering for this project is attributed to **Jürgen W. Sievers** for local hardware testing, USB endpoint validation, Fedora/DKMS bring-up, SocketCAN verification, and live R-Net / powered wheelchair bench captures. OpenAI ChatGPT assisted by turning those observations into driver code, documentation, and regression tooling.

These notes combine public CANalyst-II reverse-engineering information with local project observations. They are not vendor documentation and should be treated as an engineering record that may evolve as additional adapters, firmware revisions, and captures are tested.

This driver targets USB ID `04d8:0053`, commonly reported as `Chuangxin Tech USBCAN/CANalyst-II` and sold in compatible forms such as Waveshare USB-CAN-B / CANalyst-II style adapters.

## USB model

The known device exposes one USB interface with bulk endpoints. Public reverse-engineering notes and userspace drivers show two CAN channels.

Two endpoint profiles are supported:

### `canalystii`

```text
channel 0 command endpoint: 2
channel 0 CAN message endpoint: 1
channel 1 command endpoint: 4
channel 1 CAN message endpoint: 3
```

IN uses the same endpoint number with USB direction IN, for example endpoint number `2` means OUT address `0x02` and IN address `0x82`.

### `waveshare`

```text
channel 0 command/message endpoint: 2
channel 1 command/message endpoint: 3
```

This matches simpler USB captures where channel 1 appears as `0x02/0x82` and channel 2 appears as `0x03/0x83`.

## Command packet

All command packets are 64 bytes.

Simple command layout:

```text
offset  size  meaning
0       u32   command opcode, little endian
4       ...   zero padding up to 64 bytes
```

Known opcodes:

```text
0x01 init channel
0x02 start channel
0x03 stop channel
0x05 clear RX buffer
0x0a message status
0x0b CAN status, not fully decoded here
```

## Init command packet

```text
offset  size  meaning
0       u32   0x01 init command
4       u32   acc_code, set to 1
8       u32   acc_mask, set to 0xffffffff
12      u32   unknown/filter field, set to 0
16      u32   filter mode, set to 1
20      u32   unknown, set to 0
24      u32   timing0 / BTR0
28      u32   timing1 / BTR1
32      u32   mode, set to 0 for normal mode
36      u32   unknown, set to 1
40      ...   zero padding up to 64 bytes
```

## Message status response

```text
offset  size  meaning
0       u32   0x0a status command echo
4       u32   pending RX CAN messages
8       u16   pending TX CAN messages
10      u16   unknown
12      ...   padding
```

## CAN message buffer

CAN messages are transferred in 64-byte USB packets:

```text
offset  size       meaning
0       u8         count, 0..3
1       21 bytes   CAN message 0
22      21 bytes   CAN message 1
43      21 bytes   CAN message 2
```

CAN message layout:

```text
offset  size  meaning
0       u32   CAN ID, little endian
4       u32   hardware timestamp, units of 100 us on RX
8       i8    time_flag, usually 1
9       i8    send_type
10      bool  remote/RTR
11      bool  extended/EFF
12      u8    DLC
13      u8[8] data
```

## Timing table

The driver uses the known fixed timing table:

```text
bitrate   BTR0  BTR1
5000      bf    ff
10000     31    1c
20000     18    1c
33330     09    6f
40000     87    ff
50000     09    1c
66660     04    6f
80000     83    ff
83330     03    6f
100000    04    1c
125000    03    1c
200000    81    fa
250000    01    1c
400000    80    fa
500000    00    1c
666000    80    b6
800000    00    16
1000000   00    14
```

For R-Net the important value is `125000` with `BTR0=0x03`, `BTR1=0x1c`.
