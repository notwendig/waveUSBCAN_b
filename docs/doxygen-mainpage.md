/**
@mainpage waveUSBCAN_b

@brief Native SocketCAN DKMS driver prototype for Waveshare USB-CAN-B / Chuangxin CANalyst-II compatible adapters.

@section overview Overview

waveUSBCAN_b binds to USB ID `04d8:0053` and registers two classical CAN SocketCAN netdevices, normally `can0` and `can1`. The driver is intended for careful bench work with R-Net / powered wheelchair CAN traffic and for general Linux SocketCAN tooling such as `candump`, `cansend`, `python-can`, SavvyCAN, QtRNetAnalyzer and open-rnet.

@section safety Safety

Treat powered wheelchair buses as safety-critical. Use passive capture first, keep the chair immobilized and unoccupied, and never transmit guessed control frames to a live mobility device. See `docs/SAFETY.md`.

@section architecture Architecture

The kernel module has four main paths:

- USB probe/disconnect: endpoint discovery and SocketCAN netdevice registration.
- Netdevice open/close: bitrate selection and CANalyst-II COMMAND_INIT/START/STOP.
- RX polling: status query followed by message endpoint reads.
- TX path: SocketCAN skb conversion into one 64-byte CANalyst-II USB message packet.

@section reverseengineering Reverse engineering

Protocol reverse-engineering attribution belongs to Jürgen W. Sievers for local hardware observations, USB endpoint validation, Fedora/DKMS bring-up, SocketCAN testing and R-Net bench captures. See `docs/REVERSE_ENGINEERING.md` and `docs/PROTOCOL.md`.

@section authors Authors

- Jürgen W. Sievers — project owner, protocol reverse-engineering contributor, hardware testing, endpoint validation and R-Net bench validation.
- OpenAI ChatGPT — driver generation, documentation and regression tooling assistance.
*/
