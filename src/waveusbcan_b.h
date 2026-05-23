// SPDX-License-Identifier: GPL-2.0-only
#ifndef WAVEUSBCAN_B_H
#define WAVEUSBCAN_B_H

#include <linux/atomic.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/length.h>
#include <linux/can/skb.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#define WUC_DRIVER_NAME "waveusbcan_b"
#define WUC_DRIVER_DESC "SocketCAN driver prototype for Waveshare USB-CAN-B / CANalyst-II"

enum wuc_driver_constant {
	WUC_VENDOR_ID = 0x04d8,
	WUC_PRODUCT_ID = 0x0053,

	WUC_CHANNELS = 2,
	WUC_USB_PACKET_SIZE = 64,
	WUC_CAN_MSG_SIZE = 21,
	WUC_MSGS_PER_PACKET = 3,
	WUC_ECHO_SKB_MAX = 1,

	WUC_DEFAULT_USB_TIMEOUT_MS = 1000,
	WUC_DEFAULT_POLL_MS = 20,
	WUC_MAX_RX_PACKETS = 64,
};

enum wuc_channel {
	WUC_CHANNEL_0 = 0,
	WUC_CHANNEL_1 = 1,
};

enum wuc_command {
	WUC_CMD_INIT = 0x01,
	WUC_CMD_START = 0x02,
	WUC_CMD_STOP = 0x03,
	WUC_CMD_CLEAR_RX = 0x05,
	WUC_CMD_MSG_STATUS = 0x0a,
	WUC_CMD_CAN_STATUS = 0x0b,
};

/**
 * struct wuc_timing - Firmware bitrate entry.
 * @bitrate: User-visible CAN bitrate in bit/s as accepted by SocketCAN.
 * @timing0: CAN controller BTR0 value used by the adapter firmware.
 * @timing1: CAN controller BTR1 value used by the adapter firmware.
 *
 * The adapter firmware does not expose arbitrary CAN bit timing through this
 * prototype. Instead, SocketCAN selects one of the fixed firmware-supported
 * rates and this driver converts it to the BTR pair expected by COMMAND_INIT.
 */
struct wuc_timing {
	u32 bitrate;
	u8 timing0;
	u8 timing1;
};

struct wuc_device;

/**
 * struct wuc_priv - Per-SocketCAN-channel private state.
 * @can: Embedded SocketCAN state managed by the CAN core.
 * @netdev: Linux network device exposed as can0/can1.
 * @parent: Shared USB device state.
 * @rx_work: Delayed polling worker used to read received CAN packets.
 * @tx_anchor: Anchor for outstanding USB TX URBs.
 * @opened: Non-zero while the netdev is administratively up.
 * @tx_busy: One-frame TX back-pressure flag matching WUC_ECHO_SKB_MAX.
 * @channel: Adapter channel index, 0 or 1.
 * @cmd_ep: Bidirectional command endpoint number without the 0x80 IN bit.
 * @msg_ep: Bidirectional CAN message endpoint number without the 0x80 IN bit.
 * @timing0: Selected firmware BTR0 value.
 * @timing1: Selected firmware BTR1 value.
 * @timing_valid: True after a supported SocketCAN bitrate was selected.
 * @pm_held: True if runtime-PM was successfully held during open().
 */
struct wuc_priv {
	struct can_priv can;
	struct net_device *netdev;
	struct wuc_device *parent;
	struct delayed_work rx_work;
	struct usb_anchor tx_anchor;
	atomic_t opened;
	atomic_t tx_busy;
	u8 channel;
	u8 cmd_ep;
	u8 msg_ep;
	u8 timing0;
	u8 timing1;
	bool timing_valid;
	bool pm_held;
};

/**
 * struct wuc_device - USB adapter state shared by both CAN channels.
 * @udev: Referenced USB device.
 * @intf: Claimed USB interface.
 * @netdev: Registered SocketCAN netdevices for both channels.
 * @cmd_ep: Selected command endpoint per channel.
 * @msg_ep: Selected CAN message endpoint per channel.
 * @present_bulk_in: Bit map of discovered bulk IN endpoint numbers.
 * @present_bulk_out: Bit map of discovered bulk OUT endpoint numbers.
 * @usb_lock: Serializes synchronous USB command/status transfers.
 */
struct wuc_device {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct net_device *netdev[WUC_CHANNELS];
	u8 cmd_ep[WUC_CHANNELS];
	u8 msg_ep[WUC_CHANNELS];
	unsigned long present_bulk_in;
	unsigned long present_bulk_out;
	struct mutex usb_lock;
};

/**
 * struct wuc_tx_context - Lifetime context for asynchronous USB TX URBs.
 * @urb: USB request block submitted on the channel message endpoint.
 * @netdev: SocketCAN netdevice owning the transmitted skb echo slot.
 * @buf: DMA-safe 64-byte CANalyst-II USB message packet.
 */
struct wuc_tx_context {
	struct urb *urb;
	struct net_device *netdev;
	u8 *buf;
};

#endif /* WAVEUSBCAN_B_H */
