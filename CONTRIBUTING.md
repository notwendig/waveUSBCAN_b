# Contributing

Thank you for helping improve waveUSBCAN_b.

## Development rules

- Keep the driver conservative and readable.
- Prefer passive diagnostics before active bus interaction.
- Do not add examples that transmit guessed frames to a live powered wheelchair bus.
- Preserve DKMS install/uninstall behavior on Fedora and Debian/Ubuntu.
- Document new module parameters in README and Doxygen comments.

## Test expectations

For driver changes, include at least:

```bash
sudo ./scripts/collect-dkms-debug.sh
sudo ./scripts/regression-dual-channel-bitrate.sh --bitrates 125000 --rx-seconds 3
```

For active TX/RX changes, test only on an isolated bench harness:

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh --allow-tx --bench-loopback
```
