# Regression testing

The main regression entry point is:

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh
```

## Passive wheelchair/R-Net test

Use this mode on a real bus. It changes interface configuration and records RX, but sends no CAN frames.

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh --bitrates 125000 --rx-seconds 5 --keep-up
```

## Passive bitrate sweep

Use on a bench setup when validating driver open/close behavior across firmware-supported bitrates.

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh \
  --bitrates 50000,100000,125000,250000,500000,1000000 \
  --rx-seconds 2
```

## Active two-channel bench test

Only use with both channels wired to the same isolated CAN bench bus or direct CH0↔CH1 harness.

```bash
sudo ./scripts/regression-dual-channel-bitrate.sh \
  --bitrates 125000,250000,500000 \
  --allow-tx --bench-loopback
```

Reports are written to `~/Downloads/waveusbcan_b-regression-<timestamp>/`.
