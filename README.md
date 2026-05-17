# waveUSBCAN_b

Version 0.1.11 adds boot and hotplug auto-start support for both SocketCAN channels while keeping the proven v0.1.7+ bring-up path, the v0.1.8 dual-channel/bitrate regression tooling, the v0.1.9 GitHub/Doxygen cleanup, and the v0.1.10 protocol reverse-engineering attribution documentation.

Out-of-tree DKMS Linux SocketCAN driver prototype for Waveshare USB-CAN-B / Chuangxin CANalyst-II style USB-CAN adapters.

Target USB ID:

```text
04d8:0053  Microchip Technology Inc. / Chuangxin Tech USBCAN/CANalyst-II
```

The goal is to expose the two CAN channels as normal SocketCAN network devices, usually `can0` and `can1`, so existing tools and projects such as `candump`, `cansend`, `python-can` with `socketcan`, SavvyCAN, QtRNetAnalyzer and `open-rnet` can use the adapter without a proprietary userspace library.

> Status: first DKMS driver prototype. It is intended for careful bench testing on a safe CAN setup before connecting to a powered wheelchair or other safety-critical hardware.

## Authors

- **Jürgen W. Sievers** — project owner, protocol reverse-engineering contributor, hardware testing, R-Net / wheelchair bench validation, and live SocketCAN capture validation.
- **OpenAI ChatGPT** — driver generation, documentation, DKMS packaging and regression tooling assistance.

See [`AUTHORS.md`](AUTHORS.md) for details.

## Documentation

Generate the Doxygen reference locally with:

```bash
doxygen Doxyfile
xdg-open docs/doxygen/html/index.html
```

Important documents:

- [`docs/SAFETY.md`](docs/SAFETY.md) — safety rules for bench and wheelchair bus work.
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — reverse-engineered protocol notes.
- [`docs/REVERSE_ENGINEERING.md`](docs/REVERSE_ENGINEERING.md) — attribution, method, evidence handling, and safety boundaries for reverse-engineering work.
- [`docs/REGRESSION.md`](docs/REGRESSION.md) — dual-channel and bitrate regression tests.
- [`docs/AUTO_START.md`](docs/AUTO_START.md) — boot and USB hotplug auto-start for both channels.
- [`examples/open-rnet-quickstart.md`](examples/open-rnet-quickstart.md) — open-rnet usage with SocketCAN.

## Features

- Native SocketCAN netdevices, one per CAN channel.
- DKMS install/uninstall scripts.
- CAN 2.0 frames: standard, extended and RTR frames.
- Known bitrate table matching the public reverse-engineered CANalyst-II USB protocol.
- Polling RX worker, matching how the known userspace driver has to poll the device.
- Configurable endpoint profile for firmware variants.

## Supported bitrates

```text
5000, 10000, 20000, 33330, 40000, 50000, 66660, 80000, 83330,
100000, 125000, 200000, 250000, 400000, 500000, 666000, 800000, 1000000
```

For R-Net use:

```bash
sudo ip link set can0 down 2>/dev/null || true
sudo ip link set can0 type can bitrate 125000 restart-ms 100
sudo ip link set can0 up
candump can0
```

## Fedora install

```bash
sudo dnf install -y gcc make dkms kernel-devel kernel-headers elfutils-libelf-devel iproute can-utils usbutils
unzip waveUSBCAN_b-0.1.11.zip
cd waveUSBCAN_b-0.1.11
sudo ./scripts/install.sh
```

Unplug/replug the adapter, then check:

```bash
lsusb | grep -i '04d8:0053\|CANalyst\|USBCAN'
ip -details link show type can
sudo dmesg -w
```


## Fedora DKMS build troubleshooting

If `scripts/install.sh` fails with `bad exit status: 2`, the real compiler error is in DKMS' `make.log`. Collect it with:

```bash
sudo ./scripts/collect-dkms-debug.sh
```

On Fedora it is normal that `dnf` installs a newer kernel while DKMS still builds against the currently running kernel. Reboot first if `uname -r` differs from the newest `/lib/modules/*` kernel directory, then run the installer again.

## Debian/Ubuntu install

```bash
sudo apt update
sudo apt install -y build-essential dkms linux-headers-$(uname -r) iproute2 can-utils usbutils
unzip waveUSBCAN_b-0.1.11.zip
cd waveUSBCAN_b-0.1.11
sudo ./scripts/install.sh
```


## Automatic boot and hotplug setup

The installer places a receive-safe helper and systemd service on the host:

```bash
/usr/local/sbin/waveusbcan_b-auto-up
/etc/systemd/system/waveusbcan_b-auto.service
```

The service configures both channels at R-Net speed by default:

```bash
sudo systemctl enable --now waveusbcan_b-auto.service
```

When the adapter is plugged in later, the udev rule for USB ID `04d8:0053`
starts the same service again. The helper only configures SocketCAN interfaces;
it never transmits CAN frames.

Check with:

```bash
systemctl status waveusbcan_b-auto.service
journalctl -u waveusbcan_b-auto.service -b --no-pager
ip -details link show type can
```

See [`docs/AUTO_START.md`](docs/AUTO_START.md) for changing bitrate or interface names.

## R-Net setup

```bash
sudo ./scripts/setup-rnet-125k.sh can0
candump can0
```

With `open-rnet` / `can2RNET`, keep using the normal SocketCAN interface name (`can0`). No project-specific USB backend should be necessary once this driver is loaded.

## Endpoint profiles

The public Python driver for CANalyst-II uses separate command and message endpoints. Some Waveshare/clone captures appear to present or use channel endpoints more simply. This module therefore supports profiles:

```bash
# Auto, default. Prefer full CANalyst-II layout if endpoints 1..4 exist.
sudo modprobe waveusbcan_b endpoint_profile=auto

# Public CANalyst-II layout:
# ch0 command EP 2, ch0 message EP 1, ch1 command EP 4, ch1 message EP 3
sudo modprobe waveusbcan_b endpoint_profile=canalystii

# Simpler Waveshare-style layout:
# ch0 command/message EP 2, ch1 command/message EP 3
sudo modprobe waveusbcan_b endpoint_profile=waveshare
```

You can override endpoints explicitly. Endpoint numbers are decimal USB endpoint numbers without the `0x80` IN bit:

```bash
sudo modprobe waveusbcan_b endpoint_profile=manual ch0_cmd_ep=2 ch0_msg_ep=2 ch1_cmd_ep=3 ch1_msg_ep=3
```

To persist options:

```bash
sudoedit /etc/modprobe.d/waveusbcan_b.conf
```

Example:

```text
options waveusbcan_b endpoint_profile=auto poll_interval_ms=20 autopm_on_open=0 command_ep_autodetect=1 usb_timeout_ms=1000 usb_retries=5
```

Reload:

```bash
sudo modprobe -r waveusbcan_b
sudo modprobe waveusbcan_b
```

## Quick smoke test

Use a safe bench CAN device or loopback setup, not a live wheelchair first:

```bash
sudo ./scripts/smoke-test.sh can0 125000
```

Manual test:

```bash
sudo ip link set can0 down 2>/dev/null || true
sudo ip link set can0 type can bitrate 125000 restart-ms 100
sudo ip link set can0 up
candump can0
```

In another terminal:

```bash
cansend can0 123#1122334455667788
```


## Dual-channel / bitrate regression test

Version 0.1.8 and later include a regression script for both channels and changing CAN bitrates:

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh --bitrates 125000
```

Safe defaults:

- tests `can0` and `can1`
- configures each selected bitrate with `ip link set canX type can bitrate ...`
- brings the interface up and verifies the visible SocketCAN bitrate
- records a short passive `candump -L` capture per interface
- writes a timestamped report directory under `~/Downloads/`
- does **not** transmit CAN frames

Example passive sweep on an isolated bench bus:

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh \
  --bitrates 50000,100000,125000,250000,500000,1000000 \
  --rx-seconds 2
```

Active two-channel bench test, only with CAN0 and CAN1 wired together on an isolated test harness with proper termination:

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh \
  --bitrates 125000,250000,500000 \
  --allow-tx --bench-loopback
```

Do not use the active TX mode on a live R-Net / wheelchair bus. For a wheelchair bus, use passive mode and keep the bitrate at the known-good R-Net speed, normally `125000`.

## DKMS maintenance

```bash
sudo dkms status | grep waveUSBCAN_b
sudo ./scripts/uninstall.sh
```

## Known limitations

- This is a first kernel-driver prototype. Expect one or more endpoint-profile adjustments on real hardware.
- Hardware error counters and detailed bus error reporting are not fully decoded yet.
- RX is polling-based because the known CANalyst-II USB protocol exposes pending-message status through a command query.
- TX starts with one URB per CAN frame for safety and correctness, not maximum throughput.
- CAN FD is not supported.
- Listen-only and loopback device modes are not enabled because the public protocol notes do not fully define safe mode values.

## Safety note for R-Net / wheelchair work

Use this first on an isolated bench setup. R-Net wheelchairs are safety-critical systems. Confirm wiring, isolation, bus bitrate and emergency stop behavior before attempting any live integration.

## License

Kernel module source: GPL-2.0-only, compatible with Linux kernel module requirements.

Protocol notes are documented separately in `docs/PROTOCOL.md` and `docs/REVERSE_ENGINEERING.md`. They are based on public reverse-engineering information plus local project observations, endpoint experiments, Linux SocketCAN testing, and R-Net bench captures provided by Jürgen W. Sievers. No proprietary vendor headers or libraries are required.

## Debugging `ip link set canX up`

Version 0.1.4 and later add verbose startup logging, USB retry handling, runtime-PM handling, a longer USB command timeout, and command-endpoint fallback probing. If `ip link set can0 up` prints `RTNETLINK answers: Resource temporarily unavailable`, run:

```bash
sudo ./scripts/open-debug.sh can0 125000 auto
```

The preferred endpoint profile for the common CANalyst-II firmware is `auto`, which maps command endpoints `[2,4]` and message endpoints `[1,3]`. The `waveshare` profile is kept only for experiments with clones that actually use the same endpoint for command and messages.
