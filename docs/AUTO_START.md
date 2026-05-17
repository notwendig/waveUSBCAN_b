# Automatic boot and hotplug setup

`waveUSBCAN_b` can automatically bring up both SocketCAN channels when the
adapter is already connected during boot or when it is plugged in later.

The package installs two pieces:

- `systemd/waveusbcan_b-auto.service` → boot-time configuration
- `udev/99-waveusbcan-b.rules` → hotplug trigger for USB ID `04d8:0053`

The default service command is receive-safe. It never sends CAN frames:

```bash
/usr/local/sbin/waveusbcan_b-auto-up --bitrate 125000 --interfaces can0,can1 --wait 20 --restart-ms 0
```

## Enable auto-start

```bash
sudo systemctl enable --now waveusbcan_b-auto.service
```

If the adapter is not connected at boot, the service exits successfully without
configuring anything. When the USB-CAN-B / CANalyst-II adapter is plugged in
later, the udev rule asks systemd to run the same service again.

## Check state

```bash
systemctl status waveusbcan_b-auto.service
journalctl -u waveusbcan_b-auto.service -b --no-pager
ip -details link show type can
```

Expected result for R-Net bench testing:

```text
can0: <NOARP,UP,LOWER_UP,ECHO>
can1: <NOARP,UP,LOWER_UP,ECHO>
bitrate 125000
```

## Change bitrate

Edit the service override instead of editing the installed unit directly:

```bash
sudo systemctl edit waveusbcan_b-auto.service
```

Example override for 500 kbit/s:

```ini
[Service]
ExecStart=
ExecStart=/usr/local/sbin/waveusbcan_b-auto-up --bitrate 500000 --interfaces can0,can1 --wait 20 --restart-ms 0
```

Then reload and run:

```bash
sudo systemctl daemon-reload
sudo systemctl restart waveusbcan_b-auto.service
```

## Safety note

Automatic startup only configures Linux SocketCAN interfaces. It does not run
`cansend`, `open-rnet`, joystick emulation, or any active control software. For
powered wheelchair/R-Net work, keep active transmission opt-in and perform
passive capture before any transmit experiment.
