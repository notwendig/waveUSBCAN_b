<!--
SPDX-License-Identifier: GPL-2.0-only
Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
Authors: Jürgen W. Sievers; OpenAI ChatGPT
-->

# open-rnet quickstart with waveUSBCAN_b

```bash
# 1. Install and load driver
cd waveUSBCAN_b
sudo ./scripts/install.sh

# 2. Replug USB-CAN-B / CANalyst-II adapter
ip -details link show type can

# 3. Configure R-Net speed
sudo ./scripts/setup-rnet-125k.sh can0

# 4. Watch the bus
candump can0

# 5. Clone open-rnet
mkdir -p ~/Projects
git clone https://github.com/redragonx/open-rnet.git ~/Projects/open-rnet
cd ~/Projects/open-rnet

# 6. Use open-rnet normally with can0 / SocketCAN
```

Safety: test on a bench CAN network before any powered wheelchair connection.
