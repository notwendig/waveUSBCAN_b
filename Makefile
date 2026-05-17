# SPDX-License-Identifier: GPL-2.0-only
#
# waveUSBCAN_b DKMS/out-of-tree kernel build
# Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
# Authors: Jürgen W. Sievers; OpenAI ChatGPT
#
obj-m += waveusbcan_b.o
waveusbcan_b-y := src/waveusbcan_b_main.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

.PHONY: all clean
