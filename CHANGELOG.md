# Changelog

## 0.1.11

- Add boot-time and USB hotplug auto-start support for both SocketCAN channels.
- Add `scripts/auto-up.sh` installed as `/usr/local/sbin/waveusbcan_b-auto-up`.
- Add `systemd/waveusbcan_b-auto.service` and update the udev rule to trigger it when USB ID `04d8:0053` is connected.
- Add `docs/AUTO_START.md` and README instructions for enabling, checking, and overriding the automatic bitrate setup.
- Keep the helper receive-safe: it configures CAN interfaces only and never sends CAN frames.

## 0.1.10

- Explicitly credit **Jürgen W. Sievers** as protocol reverse-engineering contributor.
- Add `docs/REVERSE_ENGINEERING.md` documenting attribution, evidence handling, known-good local milestone, and safety boundaries.
- Update `AUTHORS.md`, `README.md`, `docs/PROTOCOL.md`, Doxygen main page, and source header with reverse-engineering attribution.
- Update package metadata and Doxygen project number to `0.1.10`.

## 0.1.9

- Add GitHub-ready repository metadata and documentation hygiene files.
- Add `AUTHORS.md`, `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `SUPPORT.md`, issue templates, PR template, CODEOWNERS, `.editorconfig`, and CI skeleton.
- Add Doxygen setup with `Doxyfile` and `docs/doxygen-mainpage.md`.
- Add safety and regression documentation.
- Add authorship headers to source, scripts, docs, udev rules, DKMS metadata, and build files.

## 0.1.8

- Add `scripts/regression-dual-channel-bitrate.sh` for safe dual-channel and multi-bitrate regression testing.
- Passive mode is the default: configure `can0`/`can1`, verify bitrates, capture short `candump -L` logs, and write a report directory under `~/Downloads`.
- Add opt-in active bench-only two-channel TX/RX test guarded by `--allow-tx --bench-loopback`.
- Update the packaged modprobe default to the known-good v0.1.7+ options: `endpoint_profile=auto`, `autopm_on_open=0`, `command_ep_autodetect=1`, `usb_timeout_ms=1000`, `usb_retries=5`.

## 0.1.7

- Make `autopm_on_open` default to `false`; the driver already calls `usb_disable_autosuspend()` while bound.
- If `autopm_on_open=1` is used and `usb_autopm_get_interface()` returns `-EACCES`, continue opening unless `autopm_strict=1` is set.
- Update `scripts/open-debug.sh` to test with `autopm_on_open=0`, so endpoint/COMMAND_INIT probing is not hidden by runtime-PM permission failures.

## 0.1.6

- Keep a USB runtime-PM reference while `can0`/`can1` is open (`usb_autopm_get_interface()` / `usb_autopm_put_interface()`).
- Add `usb_timeout_ms` module parameter and raise the default command bulk timeout to 1000 ms.
- Add command-endpoint fallback probing: if `COMMAND_INIT` fails on the selected command endpoint, try the other bidirectional bulk endpoints and keep the first one that accepts init.
- Keep v0.1.5 DMA-safe command buffers and v0.1.4 debug/retry logging.

## 0.1.4

- Add detailed `ndo_open`/startup diagnostics for `ip link set canX up` failures.
- Add retry/clear-halt handling around transient USB bulk `-EAGAIN`, `-ETIMEDOUT` and `-EPIPE` errors.
- Disable USB autosuspend while the adapter is bound to the driver.
- Keep `endpoint_profile=auto` as the recommended default; use `waveshare` only for experiments.

## 0.1.3

- Fix Fedora/Linux 7.0.x probe failure `-EINVAL` in `register_candev()`: do not set both `bitrate_const` and `bittiming_const` at the same time.
- Add clearer `register_candev` error logging and endpoint-map diagnostics.

## 0.1.2

- Fix Fedora 44 / Linux 7.0.x DKMS build failure where `can_change_mtu` is not declared for this out-of-tree module build.
- Add a local classical-CAN-only MTU handler accepting `CAN_MTU`.

## 0.1.1

- Add DKMS debug collection and installer make.log tail output on build failure.

## 0.1.0

- Initial SocketCAN DKMS prototype for Waveshare USB-CAN-B / CANalyst-II compatible adapters.
