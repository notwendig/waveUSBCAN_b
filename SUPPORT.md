# Support

For help, open an issue with:

```bash
uname -a
lsusb | grep -iE '04d8|0053|canalyst|usbcan'
ip -details link show type can
sudo dmesg -T | grep -iE 'waveusbcan|usbcan|canalyst|04d8|0053|can0|can1' | tail -n 200
```

If DKMS fails, also attach:

```bash
sudo ./scripts/collect-dkms-debug.sh
```

For wheelchair/R-Net captures, prefer passive logs from an immobilized, unoccupied bench setup.
