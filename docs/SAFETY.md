# Safety notes for wheelchair / R-Net bench work

waveUSBCAN_b can expose a powered wheelchair CAN/R-Net bus as a normal Linux SocketCAN interface. That is powerful and potentially dangerous.

## Rules

1. **Passive first.** Use `candump` and logging before any transmit test.
2. **No guessed control frames.** Do not send arbitrary `cansend` frames to a live wheelchair bus.
3. **Bench only for active tests.** Use `--allow-tx --bench-loopback` only with an isolated two-channel CAN bench harness, never on a person-carrying wheelchair.
4. **Use an immobilized setup.** For wheelchair captures, use an unoccupied, lifted or otherwise immobilized chair with a safe power cutoff.
5. **Keep original controls available.** Do not rely on experimental software as a safety stop.
6. **Log everything.** Save `candump -L` captures and `dmesg` output for each test.

## Recommended passive capture

```bash
sudo ip link set can0 down 2>/dev/null || true
sudo ip link set can0 type can bitrate 125000 restart-ms 100
sudo ip link set can0 up
candump -L can0 > ~/Downloads/wheelchair-passive.log
```

Stop with `Ctrl+C`.
