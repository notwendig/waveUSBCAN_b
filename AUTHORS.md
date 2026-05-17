# Authors

waveUSBCAN_b is a collaborative bench-engineering project.

## Primary contributors

- **Jürgen W. Sievers** — project owner; protocol reverse-engineering contributor; hardware testing; R-Net / powered wheelchair bench validation; Fedora/DKMS integration testing; USB endpoint mapping validation; SocketCAN live-capture validation; and project direction.
- **OpenAI ChatGPT** — generated driver source, DKMS packaging, scripts, documentation, troubleshooting iterations, and regression tooling under user direction.

## Authorship note

The project was developed interactively. Hardware behavior, logs, endpoint experiments, successful SocketCAN validation, live R-Net / powered wheelchair bench captures, safety context, and protocol reverse-engineering observations were provided by Jürgen W. Sievers. Source code and documentation were generated and iteratively corrected by OpenAI ChatGPT.

## Protocol reverse-engineering attribution

The reverse-engineering notes in `docs/PROTOCOL.md` and `docs/REVERSE_ENGINEERING.md` are credited to **Jürgen W. Sievers** for local hardware investigation and bench validation, including:

- identifying and validating the tested `04d8:0053` USB-CAN adapter on Fedora Linux,
- exercising DKMS builds against real Fedora kernels,
- validating USB endpoint profiles against live hardware,
- confirming `can0` / `can1` SocketCAN registration and 125 kbit/s R-Net operation,
- capturing live CAN/R-Net traffic from an immobilized powered wheelchair bench setup, and
- guiding safety constraints for passive capture before active transmission.

OpenAI ChatGPT assisted by generating driver code, documentation, scripts, and iterative troubleshooting text from the observations and logs supplied by Jürgen W. Sievers.

## License

Unless noted otherwise, project files are licensed under GPL-2.0-only.
