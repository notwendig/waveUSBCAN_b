// SPDX-License-Identifier: GPL-2.0-only
/**
 * @file waveusbcan_b_main.c
 * @brief Native SocketCAN USB driver prototype for Waveshare USB-CAN-B /
 *        Chuangxin CANalyst-II compatible adapters.
 *
 * @details
 * waveUSBCAN_b is an out-of-tree DKMS Linux kernel module that binds to
 * vendor-specific USB-CAN adapters with USB ID 04d8:0053 and exposes both
 * CAN channels as classical SocketCAN network devices. The implementation is
 * intentionally conservative: fixed bitrates known from the public
 * reverse-engineered CANalyst-II command set, polling based RX, one outstanding
 * TX echo slot per channel, and extensive diagnostics around USB endpoint
 * selection.
 *
 * Protocol reverse-engineering attribution belongs to Jürgen W. Sievers for
 * local endpoint validation, SocketCAN testing and R-Net bench captures.
 *
 * This driver was developed for bench integration of R-Net / powered wheelchair
 * research setups. Treat any live wheelchair bus as safety-critical: passive
 * capture and controlled bench tests first; never transmit guessed control
 * frames to a person-carrying mobility device.
 *
 * @author Jürgen W. Sievers <project owner, protocol reverse engineering, hardware testing, R-Net bench use>
 * @author OpenAI ChatGPT <driver generation, documentation, regression tooling>
 * @copyright Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
 * @license GPL-2.0-only
 */


#include "waveusbcan_b.h"

/** Fixed firmware-supported bitrate table. */
static const struct wuc_timing wuc_timings[] = {
	{   5000, 0xbf, 0xff },
	{  10000, 0x31, 0x1c },
	{  20000, 0x18, 0x1c },
	{  33330, 0x09, 0x6f },
	{  40000, 0x87, 0xff },
	{  50000, 0x09, 0x1c },
	{  66660, 0x04, 0x6f },
	{  80000, 0x83, 0xff },
	{  83330, 0x03, 0x6f },
	{ 100000, 0x04, 0x1c },
	{ 125000, 0x03, 0x1c },
	{ 200000, 0x81, 0xfa },
	{ 250000, 0x01, 0x1c },
	{ 400000, 0x80, 0xfa },
	{ 500000, 0x00, 0x1c },
	{ 666000, 0x80, 0xb6 },
	{ 800000, 0x00, 0x16 },
	{1000000, 0x00, 0x14 },
};


/** SocketCAN fixed bitrate list exported to the CAN core. */
static const u32 wuc_bitrate_const[] = {
	5000, 10000, 20000, 33330, 40000, 50000, 66660, 80000, 83330,
	100000, 125000, 200000, 250000, 400000, 500000, 666000,
	800000, 1000000,
};

/** Reserved generic bit-timing limits; fixed bitrate_const is used in practice. */
static const struct can_bittiming_const wuc_bittiming_const = {
	.name = WUC_DRIVER_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

/** Endpoint selection profile module parameter. */
static char *endpoint_profile = "auto";
module_param(endpoint_profile, charp, 0444);
MODULE_PARM_DESC(endpoint_profile,
	"Endpoint profile: auto, canalystii, waveshare, manual (default: auto)");

/** Manual endpoint override parameters. Negative values mean auto/profile selection. */
static int ch0_cmd_ep = -1;
static int ch0_msg_ep = -1;
static int ch1_cmd_ep = -1;
static int ch1_msg_ep = -1;
module_param(ch0_cmd_ep, int, 0444);
module_param(ch0_msg_ep, int, 0444);
module_param(ch1_cmd_ep, int, 0444);
module_param(ch1_msg_ep, int, 0444);
MODULE_PARM_DESC(ch0_cmd_ep, "Channel 0 command endpoint number, without 0x80 IN bit");
MODULE_PARM_DESC(ch0_msg_ep, "Channel 0 CAN message endpoint number, without 0x80 IN bit");
MODULE_PARM_DESC(ch1_cmd_ep, "Channel 1 command endpoint number, without 0x80 IN bit");
MODULE_PARM_DESC(ch1_msg_ep, "Channel 1 CAN message endpoint number, without 0x80 IN bit");

/** RX status polling interval in milliseconds. */
static int poll_interval_ms = WUC_DEFAULT_POLL_MS;
module_param(poll_interval_ms, int, 0644);
MODULE_PARM_DESC(poll_interval_ms, "RX polling interval in milliseconds (default: 20)");

/** Retry count for transient synchronous USB command/status transfers. */
static int usb_retries = 3;
module_param(usb_retries, int, 0644);
MODULE_PARM_DESC(usb_retries, "USB bulk retry count for transient -EAGAIN/-ETIMEDOUT/-EPIPE startup errors (default: 3)");

/** Verbose channel-open diagnostics. */
static bool debug_open = true;
module_param(debug_open, bool, 0644);
MODULE_PARM_DESC(debug_open, "Log each netdev open/startup step to dmesg (default: true)");

/** Timeout for synchronous USB bulk command/status transfers. */
static int usb_timeout_ms = WUC_DEFAULT_USB_TIMEOUT_MS;
module_param(usb_timeout_ms, int, 0644);
MODULE_PARM_DESC(usb_timeout_ms, "USB bulk transfer timeout in milliseconds (default: 1000)");

/** Optional runtime-PM hold while a CAN channel is open. */
static bool autopm_on_open = false;
module_param(autopm_on_open, bool, 0644);
MODULE_PARM_DESC(autopm_on_open, "Try to hold a USB runtime-PM reference while CAN netdev is open (default: false; usb_disable_autosuspend() is already used while bound)");

/** If true, runtime-PM errors abort netdevice open. */
static bool autopm_strict = false;
module_param(autopm_strict, bool, 0644);
MODULE_PARM_DESC(autopm_strict, "Fail CAN open if usb_autopm_get_interface() fails (default: false)");

/** If true, retry COMMAND_INIT on alternate endpoint pairs. */
static bool command_ep_autodetect = true;
module_param(command_ep_autodetect, bool, 0644);
MODULE_PARM_DESC(command_ep_autodetect, "On open, retry COMMAND_INIT on other bidirectional bulk endpoint numbers if the selected command endpoint fails (default: true)");

/**
 * wuc_put_le32() - Store a 32-bit value in little-endian USB packet format.
 * @p: Destination byte pointer.
 * @v: Host-endian value to encode.
 */
static void wuc_put_le32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/**
 * wuc_get_le32() - Read a 32-bit little-endian value from a USB packet.
 * @p: Source byte pointer.
 * Return: Host-endian decoded value.
 */
static u32 wuc_get_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

/**
 * wuc_ep_present() - Test whether a numeric USB bulk endpoint exists.
 * @map: Endpoint bitmap collected by wuc_scan_endpoints().
 * @ep: Endpoint number without direction bit.
 * Return: true if the endpoint number is usable.
 */
static bool wuc_ep_present(unsigned long map, u8 ep)
{
	return ep > 0 && ep < 16 && test_bit(ep, &map);
}

/**
 * wuc_bidirectional_ep_present() - Check whether both IN and OUT exist for @ep.
 * @wdev: Shared USB adapter state.
 * @ep: Endpoint number without the 0x80 IN bit.
 * Return: true if both bulk directions are present.
 */
static bool wuc_bidirectional_ep_present(struct wuc_device *wdev, u8 ep)
{
	return wuc_ep_present(wdev->present_bulk_out, ep) &&
	       wuc_ep_present(wdev->present_bulk_in, ep);
}

/**
 * wuc_find_timing() - Look up the firmware timing tuple for a bitrate.
 * @bitrate: Requested SocketCAN bitrate.
 * Return: Matching timing entry, or NULL if unsupported.
 */
static const struct wuc_timing *wuc_find_timing(u32 bitrate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wuc_timings); i++) {
		if (wuc_timings[i].bitrate == bitrate)
			return &wuc_timings[i];
	}

	return NULL;
}

/**
 * wuc_usb_retryable() - Classify transient USB errors during startup/polling.
 * @ret: Negative errno returned by the USB core.
 * Return: true if retrying the transfer is reasonable.
 */
static bool wuc_usb_retryable(int ret)
{
	return ret == -EAGAIN || ret == -ETIMEDOUT || ret == -EPIPE ||
	       ret == -EPROTO || ret == -EOVERFLOW;
}

/**
 * wuc_usb_bulk_write() - Synchronous bulk OUT transfer with retry handling.
 * @priv: Per-channel state used for logging and parent USB access.
 * @ep: OUT endpoint number without direction bit.
 * @buf: DMA-safe transfer buffer.
 * @len: Transfer length in bytes.
 * Return: 0 on complete transfer, otherwise negative errno.
 */
static int wuc_usb_bulk_write(struct wuc_priv *priv, u8 ep, u8 *buf, int len)
{
	struct wuc_device *wdev = priv->parent;
	unsigned int pipe = usb_sndbulkpipe(wdev->udev, ep);
	int max_attempts = usb_retries < 1 ? 1 : usb_retries;
	int timeout = usb_timeout_ms < 100 ? 100 : usb_timeout_ms;
	int actual = 0;
	int ret = 0;
	int attempt;

	for (attempt = 1; attempt <= max_attempts; attempt++) {
		actual = 0;
		ret = usb_bulk_msg(wdev->udev, pipe, buf, len, &actual,
				   timeout);
		if (!ret && actual == len)
			return 0;

		if (!ret)
			ret = -EIO;

		if (!wuc_usb_retryable(ret))
			return ret;

		if (ret == -EPIPE)
			usb_clear_halt(wdev->udev, pipe);

		if (debug_open && priv->netdev)
			netdev_warn(priv->netdev,
				    "bulk OUT ep=%u attempt %d/%d failed: ret=%d actual=%d; retrying\n",
				    ep, attempt, max_attempts, ret, actual);
		usleep_range(1000, 3000);
	}

	return ret;
}

/**
 * wuc_usb_bulk_read() - Synchronous bulk IN transfer with retry handling.
 * @priv: Per-channel state used for logging and parent USB access.
 * @ep: IN endpoint number without direction bit.
 * @buf: DMA-safe receive buffer.
 * @len: Maximum transfer length in bytes.
 * Return: Positive byte count on success, otherwise negative errno.
 */
static int wuc_usb_bulk_read(struct wuc_priv *priv, u8 ep, u8 *buf, int len)
{
	struct wuc_device *wdev = priv->parent;
	unsigned int pipe = usb_rcvbulkpipe(wdev->udev, ep);
	int max_attempts = usb_retries < 1 ? 1 : usb_retries;
	int timeout = usb_timeout_ms < 100 ? 100 : usb_timeout_ms;
	int actual = 0;
	int ret = 0;
	int attempt;

	for (attempt = 1; attempt <= max_attempts; attempt++) {
		actual = 0;
		ret = usb_bulk_msg(wdev->udev, pipe, buf, len, &actual,
				   timeout);
		if (!ret)
			return actual;

		if (!wuc_usb_retryable(ret))
			return ret;

		if (ret == -EPIPE)
			usb_clear_halt(wdev->udev, pipe);

		if (debug_open && priv->netdev)
			netdev_warn(priv->netdev,
				    "bulk IN ep=0x%02x attempt %d/%d failed: ret=%d; retrying\n",
				    ep | 0x80, attempt, max_attempts, ret);
		usleep_range(1000, 3000);
	}

	return ret;
}

/**
 * wuc_build_simple_command() - Build a 64-byte command packet with one opcode.
 * @buf: DMA-safe 64-byte command buffer.
 * @command: CANalyst-II command opcode.
 */
static void wuc_build_simple_command(u8 *buf, u32 command)
{
	memset(buf, 0, WUC_USB_PACKET_SIZE);
	wuc_put_le32(buf, command);
}

static void wuc_clear_channel_halts(struct wuc_priv *priv)
{
	struct wuc_device *wdev = priv->parent;

	mutex_lock(&wdev->usb_lock);
	usb_clear_halt(wdev->udev, usb_sndbulkpipe(wdev->udev, priv->cmd_ep));
	usb_clear_halt(wdev->udev, usb_rcvbulkpipe(wdev->udev, priv->cmd_ep));
	usb_clear_halt(wdev->udev, usb_sndbulkpipe(wdev->udev, priv->msg_ep));
	usb_clear_halt(wdev->udev, usb_rcvbulkpipe(wdev->udev, priv->msg_ep));
	mutex_unlock(&wdev->usb_lock);
}

/**
 * wuc_send_simple_command_on_ep() - Send a simple command to a specific EP.
 * @priv: Per-channel state.
 * @cmd_ep: Command endpoint number without direction bit.
 * @command: CANalyst-II command opcode.
 * Return: 0 on success, otherwise negative errno.
 */
static int wuc_send_simple_command_on_ep(struct wuc_priv *priv, u8 cmd_ep, u32 command)
{
	u8 *buf;
	int ret;

	/* USB HCDs need DMA-safe transfer buffers. */
	buf = kzalloc(WUC_USB_PACKET_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	wuc_build_simple_command(buf, command);

	mutex_lock(&priv->parent->usb_lock);
	ret = wuc_usb_bulk_write(priv, cmd_ep, buf, WUC_USB_PACKET_SIZE);
	mutex_unlock(&priv->parent->usb_lock);

	kfree(buf);
	return ret;
}

/**
 * wuc_send_simple_command() - Send a simple command to the selected command EP.
 * @priv: Per-channel state.
 * @command: CANalyst-II command opcode.
 * Return: 0 on success, otherwise negative errno.
 */
static int wuc_send_simple_command(struct wuc_priv *priv, u32 command)
{
	return wuc_send_simple_command_on_ep(priv, priv->cmd_ep, command);
}

static int wuc_get_msg_status_on_ep(struct wuc_priv *priv, u8 cmd_ep,
				    u32 *rx_pending, u16 *tx_pending)
{
	u8 *cmd;
	u8 *resp;
	int ret;

	cmd = kzalloc(WUC_USB_PACKET_SIZE, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	resp = kzalloc(WUC_USB_PACKET_SIZE, GFP_KERNEL);
	if (!resp) {
		kfree(cmd);
		return -ENOMEM;
	}

	wuc_build_simple_command(cmd, WUC_CMD_MSG_STATUS);

	mutex_lock(&priv->parent->usb_lock);
	ret = wuc_usb_bulk_write(priv, cmd_ep, cmd, WUC_USB_PACKET_SIZE);
	if (!ret)
		ret = wuc_usb_bulk_read(priv, cmd_ep, resp, WUC_USB_PACKET_SIZE);
	mutex_unlock(&priv->parent->usb_lock);

	if (ret < 0)
		goto out;
	if (ret < 12) {
		ret = -EIO;
		goto out;
	}

	if (wuc_get_le32(resp) != WUC_CMD_MSG_STATUS) {
		ret = -EPROTO;
		goto out;
	}

	*rx_pending = wuc_get_le32(resp + 4);
	*tx_pending = (u16)resp[8] | ((u16)resp[9] << 8);
	ret = 0;

out:
	kfree(resp);
	kfree(cmd);
	return ret;
}

/**
 * wuc_get_msg_status() - Query message status on the selected command EP.
 * @priv: Per-channel state.
 * @rx_pending: Receives pending CAN message count.
 * @tx_pending: Receives adapter TX pending count.
 * Return: 0 on success, otherwise negative errno.
 */
static int wuc_get_msg_status(struct wuc_priv *priv, u32 *rx_pending, u16 *tx_pending)
{
	return wuc_get_msg_status_on_ep(priv, priv->cmd_ep, rx_pending, tx_pending);
}

/**
 * wuc_build_init_command() - Build COMMAND_INIT for the selected CAN bitrate.
 * @priv: Per-channel state containing timing0/timing1.
 * @buf: DMA-safe 64-byte command buffer.
 *
 * The packet layout follows the public reverse-engineered CANalyst-II command
 * format. Unknown fields are kept at values proven during bench bring-up.
 */
static void wuc_build_init_command(struct wuc_priv *priv, u8 *buf)
{
	memset(buf, 0, WUC_USB_PACKET_SIZE);
	wuc_put_le32(buf + 0, WUC_CMD_INIT);
	wuc_put_le32(buf + 4, 0x00000001);        /* acc_code */
	wuc_put_le32(buf + 8, 0xffffffff);        /* acc_mask */
	wuc_put_le32(buf + 12, 0x00000000);       /* unknown0 */
	wuc_put_le32(buf + 16, 0x00000001);       /* single filter */
	wuc_put_le32(buf + 20, 0x00000000);       /* unknown1 */
	wuc_put_le32(buf + 24, priv->timing0);    /* BTR0 */
	wuc_put_le32(buf + 28, priv->timing1);    /* BTR1 */
	wuc_put_le32(buf + 32, 0x00000000);       /* normal mode */
	wuc_put_le32(buf + 36, 0x00000001);       /* unknown2 */
}

/**
 * wuc_send_init_on_ep() - Send COMMAND_INIT to one command endpoint.
 * @priv: Per-channel state.
 * @cmd_ep: Command endpoint number without direction bit.
 * Return: 0 on success, otherwise negative errno.
 */
static int wuc_send_init_on_ep(struct wuc_priv *priv, u8 cmd_ep)
{
	u8 *buf;
	int ret;

	if (!priv->timing_valid)
		return -EINVAL;

	buf = kzalloc(WUC_USB_PACKET_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	wuc_build_init_command(priv, buf);

	mutex_lock(&priv->parent->usb_lock);
	ret = wuc_usb_bulk_write(priv, cmd_ep, buf, WUC_USB_PACKET_SIZE);
	mutex_unlock(&priv->parent->usb_lock);

	kfree(buf);
	return ret;
}

/**
 * wuc_send_init() - Send COMMAND_INIT to the currently selected command EP.
 * @priv: Per-channel state.
 * Return: 0 on success, otherwise negative errno.
 */
static int wuc_send_init(struct wuc_priv *priv)
{
	return wuc_send_init_on_ep(priv, priv->cmd_ep);
}

static int wuc_send_init_with_endpoint_fallback(struct wuc_priv *priv)
{
	struct wuc_device *wdev = priv->parent;
	u8 original = priv->cmd_ep;
	u8 candidates[] = { original, 2, 4, 1, 3, 5, 6 };
	int first_ret = 0;
	int ret;
	int i;

	ret = wuc_send_init_on_ep(priv, original);
	if (!ret)
		return 0;
	first_ret = ret;

	if (!command_ep_autodetect)
		return first_ret;

	netdev_warn(priv->netdev,
		    "COMMAND_INIT on selected cmd_ep=%u failed: %d; trying fallback command endpoints\n",
		    original, first_ret);

	for (i = 0; i < ARRAY_SIZE(candidates); i++) {
		u8 ep = candidates[i];

		if (ep == original)
			continue;
		if (!wuc_bidirectional_ep_present(wdev, ep))
			continue;

		if (debug_open)
			netdev_info(priv->netdev, "trying COMMAND_INIT fallback cmd_ep=%u\n", ep);

		ret = wuc_send_init_on_ep(priv, ep);
		if (!ret) {
			priv->cmd_ep = ep;
			netdev_info(priv->netdev, "using fallback cmd_ep=%u after successful COMMAND_INIT\n", ep);
			return 0;
		}

		if (debug_open)
			netdev_warn(priv->netdev, "COMMAND_INIT fallback cmd_ep=%u failed: %d\n", ep, ret);
	}

	priv->cmd_ep = original;
	return first_ret;
}

/**
 * wuc_set_bittiming() - SocketCAN fixed-bitrate selection callback.
 * @netdev: CAN network device.
 * Return: 0 if the bitrate exists in wuc_timings[], otherwise -EINVAL.
 */
static int wuc_set_bittiming(struct net_device *netdev)
{
	struct wuc_priv *priv = netdev_priv(netdev);
	const struct wuc_timing *timing;
	u32 bitrate = priv->can.bittiming.bitrate;

	timing = wuc_find_timing(bitrate);
	if (!timing) {
		netdev_err(netdev, "unsupported bitrate %u; use one of the documented fixed rates\n",
			   bitrate);
		return -EINVAL;
	}

	priv->timing0 = timing->timing0;
	priv->timing1 = timing->timing1;
	priv->timing_valid = true;

	netdev_info(netdev, "bitrate %u selected: timing0=0x%02x timing1=0x%02x\n",
		    bitrate, priv->timing0, priv->timing1);
	return 0;
}

/**
 * wuc_parse_rx_packet() - Convert one 64-byte USB RX packet to CAN skbs.
 * @priv: Per-channel state.
 * @packet: DMA-safe 64-byte packet read from the message endpoint.
 */
static void wuc_parse_rx_packet(struct wuc_priv *priv, const u8 *packet)
{
	struct net_device *netdev = priv->netdev;
	struct net_device_stats *stats = &netdev->stats;
	u8 count = packet[0];
	int i;

	if (count > WUC_MSGS_PER_PACKET) {
		stats->rx_errors++;
		return;
	}

	for (i = 0; i < count; i++) {
		const u8 *m = packet + 1 + i * WUC_CAN_MSG_SIZE;
		struct can_frame *cf;
		struct sk_buff *skb;
		u32 can_id = wuc_get_le32(m + 0);
		u8 remote = m[10];
		u8 extended = m[11];
		u8 dlc = can_cc_dlc2len(m[12]);

		skb = alloc_can_skb(netdev, &cf);
		if (!skb) {
			stats->rx_dropped++;
			continue;
		}

		cf->can_id = can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
		if (extended)
			cf->can_id |= CAN_EFF_FLAG;
		if (remote)
			cf->can_id |= CAN_RTR_FLAG;
		can_frame_set_cc_len(cf, dlc, priv->can.ctrlmode);
		memcpy(cf->data, m + 13, cf->len);

		stats->rx_packets++;
		stats->rx_bytes += cf->len;
		netif_rx(skb);
	}
}

/**
 * wuc_rx_should_run() - Check whether RX polling is still allowed.
 * @priv: Per-channel state.
 *
 * The delayed RX worker can overlap with netdevice close() and USB
 * disconnect().  This helper keeps all rescheduling decisions tied to both
 * the channel open state and the parent USB device teardown state.
 *
 * Return: true while the channel is open and the USB device is not tearing
 * down; false otherwise.
 */
static bool wuc_rx_should_run(struct wuc_priv *priv)
{
	struct wuc_device *wdev = priv->parent;

	return atomic_read(&priv->opened) &&
	       !test_bit(WUC_PRIV_SHUTDOWN, &priv->flags) &&
	       wdev &&
	       !test_bit(WUC_DEV_DISCONNECTING, &wdev->flags);
}

/**
 * wuc_rx_work() - Polling RX worker for one CAN channel.
 * @work: Delayed work item embedded in struct wuc_priv.
 *
 * The adapter does not provide a conventional interrupt endpoint for CAN RX in
 * this prototype, so RX is driven by periodic COMMAND_MSG_STATUS polling.
 */
static void wuc_rx_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct wuc_priv *priv = container_of(dwork, struct wuc_priv, rx_work);
	struct net_device *netdev = priv->netdev;
	struct net_device_stats *stats = &netdev->stats;
	u32 rx_pending = 0;
	u16 tx_pending = 0;
	int ret;
	int packets;
	int len;
	u8 *buf;
	int actual;
	int i;

	if (!wuc_rx_should_run(priv))
		return;

	ret = wuc_get_msg_status(priv, &rx_pending, &tx_pending);
	if (ret) {
		stats->rx_errors++;
		goto reschedule;
	}

	if (!rx_pending)
		goto reschedule;

	if (!wuc_rx_should_run(priv))
		goto reschedule;

	packets = DIV_ROUND_UP(rx_pending, WUC_MSGS_PER_PACKET) + 1;
	if (packets > WUC_MAX_RX_PACKETS)
		packets = WUC_MAX_RX_PACKETS;
	len = packets * WUC_USB_PACKET_SIZE;

	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		stats->rx_dropped++;
		goto reschedule;
	}

	if (!wuc_rx_should_run(priv)) {
		kfree(buf);
		goto reschedule;
	}

	mutex_lock(&priv->parent->usb_lock);
	actual = wuc_usb_bulk_read(priv, priv->msg_ep, buf, len);
	mutex_unlock(&priv->parent->usb_lock);

	if (actual < 0) {
		stats->rx_errors++;
		kfree(buf);
		goto reschedule;
	}

	for (i = 0; i + WUC_USB_PACKET_SIZE <= actual; i += WUC_USB_PACKET_SIZE)
		wuc_parse_rx_packet(priv, buf + i);

	kfree(buf);

reschedule:
	if (wuc_rx_should_run(priv)) {
		int delay = poll_interval_ms;

		if (delay < 1)
			delay = 1;
		schedule_delayed_work(&priv->rx_work, msecs_to_jiffies(delay));
	}
}

/**
 * wuc_tx_complete() - USB URB completion handler for transmitted CAN frames.
 * @urb: Completed USB request block.
 */
static void wuc_tx_complete(struct urb *urb)
{
	struct wuc_tx_context *ctx = urb->context;
	struct net_device *netdev = ctx->netdev;
	struct wuc_priv *priv = netdev_priv(netdev);
	struct net_device_stats *stats = &netdev->stats;

	if (urb->status) {
		stats->tx_errors++;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		can_free_echo_skb(netdev, 0, NULL);
#else
		can_free_echo_skb(netdev, 0);
#endif
	} else {
		stats->tx_packets++;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		stats->tx_bytes += can_get_echo_skb(netdev, 0, NULL);
#else
		stats->tx_bytes += can_get_echo_skb(netdev, 0);
#endif
	}

	atomic_set(&priv->tx_busy, 0);
	if (netif_device_present(netdev) && netif_running(netdev))
		netif_wake_queue(netdev);

	usb_unanchor_urb(ctx->urb);
	usb_free_urb(ctx->urb);
	kfree(ctx->buf);
	kfree(ctx);
}

/**
 * wuc_start_xmit() - SocketCAN ndo_start_xmit implementation.
 * @skb: SocketCAN skb containing a classical struct can_frame.
 * @netdev: CAN network device.
 * Return: NETDEV_TX_OK or NETDEV_TX_BUSY according to netdev semantics.
 */
static netdev_tx_t wuc_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct wuc_priv *priv = netdev_priv(netdev);
	struct wuc_device *wdev = priv->parent;
	struct can_frame *cf = (struct can_frame *)skb->data;
	u8 data_len;
	u8 raw_dlc;
	struct wuc_tx_context *ctx;
	u8 *m;
	int ret;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	if (cf->can_id & CAN_ERR_FLAG) {
		netdev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	data_len = cf->can_id & CAN_RTR_FLAG ? 0 : cf->len;
	raw_dlc = can_get_cc_dlc(cf, priv->can.ctrlmode);

	if (atomic_xchg(&priv->tx_busy, 1))
		return NETDEV_TX_BUSY;

	ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		goto drop_busy;

	ctx->buf = kzalloc(WUC_USB_PACKET_SIZE, GFP_ATOMIC);
	if (!ctx->buf)
		goto drop_ctx;

	ctx->urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!ctx->urb)
		goto drop_buf;

	ctx->netdev = netdev;
	ctx->buf[0] = 1;
	m = ctx->buf + 1;
	wuc_put_le32(m + 0, cf->can_id & CAN_ERR_MASK);
	wuc_put_le32(m + 4, 0);               /* timestamp, ignored on TX */
	m[8] = 1;                             /* time_flag */
	m[9] = 0;                             /* send_type */
	m[10] = !!(cf->can_id & CAN_RTR_FLAG);
	m[11] = !!(cf->can_id & CAN_EFF_FLAG);
	m[12] = raw_dlc;
	memcpy(m + 13, cf->data, data_len);

	netif_stop_queue(netdev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	can_put_echo_skb(skb, netdev, 0, 0);
#else
	can_put_echo_skb(skb, netdev, 0);
#endif

	usb_fill_bulk_urb(ctx->urb, wdev->udev,
			  usb_sndbulkpipe(wdev->udev, priv->msg_ep), ctx->buf,
			  WUC_USB_PACKET_SIZE, wuc_tx_complete, ctx);
	usb_anchor_urb(ctx->urb, &priv->tx_anchor);

	ret = usb_submit_urb(ctx->urb, GFP_ATOMIC);
	if (ret) {
		usb_unanchor_urb(ctx->urb);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		can_free_echo_skb(netdev, 0, NULL);
#else
		can_free_echo_skb(netdev, 0);
#endif
		netdev->stats.tx_errors++;
		netif_wake_queue(netdev);
		usb_free_urb(ctx->urb);
		kfree(ctx->buf);
		kfree(ctx);
		atomic_set(&priv->tx_busy, 0);
		return NETDEV_TX_OK;
	}

	return NETDEV_TX_OK;

drop_buf:
	kfree(ctx->buf);
drop_ctx:
	kfree(ctx);
drop_busy:
	atomic_set(&priv->tx_busy, 0);
	netdev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * wuc_open() - Bring one SocketCAN channel up.
 * @netdev: CAN network device.
 * Return: 0 on success, otherwise negative errno.
 *
 * The sequence is: ensure bitrate, open the CAN core, clear endpoint halts,
 * send COMMAND_INIT, clear RX, start the adapter channel, then start polling.
 */
static int wuc_open(struct net_device *netdev)
{
	struct wuc_priv *priv = netdev_priv(netdev);
	int ret;

	if (test_bit(WUC_DEV_DISCONNECTING, &priv->parent->flags))
		return -ENODEV;

	clear_bit(WUC_PRIV_SHUTDOWN, &priv->flags);

	if (debug_open)
		netdev_info(netdev,
			    "open requested: channel=%u cmd_ep=%u msg_ep=%u bitrate=%u timing_valid=%d\n",
			    priv->channel, priv->cmd_ep, priv->msg_ep,
			    priv->can.bittiming.bitrate, priv->timing_valid);

	if (!priv->timing_valid) {
		const struct wuc_timing *timing = wuc_find_timing(125000);

		priv->can.bittiming.bitrate = 125000;
		priv->timing0 = timing->timing0;
		priv->timing1 = timing->timing1;
		priv->timing_valid = true;
		netdev_info(netdev, "no bitrate configured; defaulting to 125000 for R-Net bench use\n");
	}

	if (autopm_on_open) {
		ret = usb_autopm_get_interface(priv->parent->intf);
		if (ret) {
			/*
			 * Some systems return -EACCES here for vendor-specific USB
			 * devices although the driver already disabled autosuspend in
			 * probe(). Runtime-PM is only an extra guard for this driver, so
			 * do not let it block SocketCAN open unless explicitly requested.
			 */
			netdev_warn(netdev,
				    "usb_autopm_get_interface failed: %d; continuing without PM hold\n",
				    ret);
			if (autopm_strict)
				return ret;
			priv->pm_held = false;
		} else {
			priv->pm_held = true;
		}
	}

	ret = open_candev(netdev);
	if (ret) {
		netdev_err(netdev, "open_candev failed: %d\n", ret);
		goto err_pm;
	}

	wuc_clear_channel_halts(priv);

	ret = wuc_send_init_with_endpoint_fallback(priv);
	if (ret) {
		netdev_err(netdev, "COMMAND_INIT failed on cmd_ep=%u: %d\n",
			   priv->cmd_ep, ret);
		goto err_close;
	}
	if (debug_open)
		netdev_info(netdev, "COMMAND_INIT ok\n");

	ret = wuc_send_simple_command(priv, WUC_CMD_CLEAR_RX);
	if (ret) {
		netdev_err(netdev, "COMMAND_CLEAR_RX failed on cmd_ep=%u: %d\n",
			   priv->cmd_ep, ret);
		goto err_close;
	}
	if (debug_open)
		netdev_info(netdev, "COMMAND_CLEAR_RX ok\n");

	ret = wuc_send_simple_command(priv, WUC_CMD_START);
	if (ret) {
		netdev_err(netdev, "COMMAND_START failed on cmd_ep=%u: %d\n",
			   priv->cmd_ep, ret);
		goto err_close;
	}
	if (debug_open)
		netdev_info(netdev, "COMMAND_START ok\n");

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	atomic_set(&priv->opened, 1);
	atomic_set(&priv->tx_busy, 0);
	netif_start_queue(netdev);
	schedule_delayed_work(&priv->rx_work, 0);
	if (debug_open)
		netdev_info(netdev, "interface started\n");
	return 0;

err_close:
	close_candev(netdev);
err_pm:
	if (priv->pm_held) {
		usb_autopm_put_interface(priv->parent->intf);
		priv->pm_held = false;
	}
	return ret;
}

/**
 * wuc_close() - Stop one SocketCAN channel.
 * @netdev: CAN network device.
 * Return: 0.
 */
static int wuc_close(struct net_device *netdev)
{
	struct wuc_priv *priv = netdev_priv(netdev);

	set_bit(WUC_PRIV_SHUTDOWN, &priv->flags);
	atomic_set(&priv->opened, 0);
	netif_stop_queue(netdev);
	cancel_delayed_work_sync(&priv->rx_work);
	usb_kill_anchored_urbs(&priv->tx_anchor);
	if (!test_bit(WUC_DEV_DISCONNECTING, &priv->parent->flags))
		wuc_send_simple_command(priv, WUC_CMD_STOP);
	priv->can.state = CAN_STATE_STOPPED;
	close_candev(netdev);
	if (priv->pm_held) {
		usb_autopm_put_interface(priv->parent->intf);
		priv->pm_held = false;
	}
	return 0;
}

/*
 * Linux 7.0/Fedora 44 no longer exposes can_change_mtu() to this
 * out-of-tree build environment in the same way older headers did.
 * This adapter is classical CAN only, so the only valid SocketCAN MTU is
 * CAN_MTU. Keeping the implementation local avoids depending on that
 * helper symbol while preserving the expected netdev behaviour.
 */
/**
 * wuc_change_mtu() - Restrict the netdevice to classical CAN MTU only.
 * @netdev: CAN network device.
 * @new_mtu: Requested MTU.
 * Return: 0 for CAN_MTU, otherwise -EINVAL.
 */
static int wuc_change_mtu(struct net_device *netdev, int new_mtu)
{
	if (new_mtu != CAN_MTU)
		return -EINVAL;

	netdev->mtu = new_mtu;
	return 0;
}

/** SocketCAN netdevice operations for each registered channel. */
static const struct net_device_ops wuc_netdev_ops = {
	.ndo_open = wuc_open,
	.ndo_stop = wuc_close,
	.ndo_start_xmit = wuc_start_xmit,
	.ndo_change_mtu = wuc_change_mtu,
};

/**
 * wuc_apply_endpoint_profile() - Select command/message endpoints per channel.
 * @intf: USB interface used for logging.
 * @wdev: Shared USB adapter state with discovered endpoint bitmaps.
 * Return: 0 on valid endpoint selection, otherwise negative errno.
 */
static int wuc_apply_endpoint_profile(struct usb_interface *intf, struct wuc_device *wdev)
{
	bool have_full;
	bool have_simple;
	const char *profile = endpoint_profile ? endpoint_profile : "auto";
	u8 cmd0 = 0, msg0 = 0, cmd1 = 0, msg1 = 0;

	have_full = wuc_ep_present(wdev->present_bulk_out, 1) &&
		    wuc_ep_present(wdev->present_bulk_in, 1) &&
		    wuc_ep_present(wdev->present_bulk_out, 2) &&
		    wuc_ep_present(wdev->present_bulk_in, 2) &&
		    wuc_ep_present(wdev->present_bulk_out, 3) &&
		    wuc_ep_present(wdev->present_bulk_in, 3) &&
		    wuc_ep_present(wdev->present_bulk_out, 4) &&
		    wuc_ep_present(wdev->present_bulk_in, 4);

	have_simple = wuc_ep_present(wdev->present_bulk_out, 2) &&
		      wuc_ep_present(wdev->present_bulk_in, 2) &&
		      wuc_ep_present(wdev->present_bulk_out, 3) &&
		      wuc_ep_present(wdev->present_bulk_in, 3);

	if (!strcmp(profile, "auto")) {
		if (have_full) {
			cmd0 = 2; msg0 = 1; cmd1 = 4; msg1 = 3;
		} else if (have_simple) {
			cmd0 = 2; msg0 = 2; cmd1 = 3; msg1 = 3;
		} else {
			dev_err(&intf->dev, "could not auto-select endpoint profile\n");
			return -ENODEV;
		}
	} else if (!strcmp(profile, "canalystii")) {
		cmd0 = 2; msg0 = 1; cmd1 = 4; msg1 = 3;
	} else if (!strcmp(profile, "waveshare")) {
		cmd0 = 2; msg0 = 2; cmd1 = 3; msg1 = 3;
	} else if (!strcmp(profile, "manual")) {
		/* Values are applied below from module params. */
	} else {
		dev_err(&intf->dev, "unknown endpoint_profile=%s\n", profile);
		return -EINVAL;
	}

	if (ch0_cmd_ep > 0) cmd0 = ch0_cmd_ep;
	if (ch0_msg_ep > 0) msg0 = ch0_msg_ep;
	if (ch1_cmd_ep > 0) cmd1 = ch1_cmd_ep;
	if (ch1_msg_ep > 0) msg1 = ch1_msg_ep;

	if (!cmd0 || !msg0 || !cmd1 || !msg1) {
		dev_err(&intf->dev, "manual endpoint profile requires ch0_cmd_ep/ch0_msg_ep/ch1_cmd_ep/ch1_msg_ep\n");
		return -EINVAL;
	}

	if (!wuc_ep_present(wdev->present_bulk_out, cmd0) ||
	    !wuc_ep_present(wdev->present_bulk_in, cmd0) ||
	    !wuc_ep_present(wdev->present_bulk_out, msg0) ||
	    !wuc_ep_present(wdev->present_bulk_in, msg0) ||
	    !wuc_ep_present(wdev->present_bulk_out, cmd1) ||
	    !wuc_ep_present(wdev->present_bulk_in, cmd1) ||
	    !wuc_ep_present(wdev->present_bulk_out, msg1) ||
	    !wuc_ep_present(wdev->present_bulk_in, msg1)) {
		dev_err(&intf->dev,
			"selected endpoints unavailable: ch0 cmd=%u msg=%u, ch1 cmd=%u msg=%u\n",
			cmd0, msg0, cmd1, msg1);
		return -ENODEV;
	}

	dev_info(&intf->dev,
		 "bulk endpoint map: IN=0x%lx OUT=0x%lx\n",
		 wdev->present_bulk_in, wdev->present_bulk_out);

	wdev->cmd_ep[0] = cmd0;
	wdev->msg_ep[0] = msg0;
	wdev->cmd_ep[1] = cmd1;
	wdev->msg_ep[1] = msg1;

	dev_info(&intf->dev,
		 "endpoint profile %s: ch0 cmd=%u msg=%u, ch1 cmd=%u msg=%u\n",
		 profile, cmd0, msg0, cmd1, msg1);
	return 0;
}

/**
 * wuc_scan_endpoints() - Discover all bulk endpoints on the claimed interface.
 * @intf: USB interface to inspect.
 * @wdev: Shared USB adapter state receiving endpoint bitmaps.
 */
static void wuc_scan_endpoints(struct usb_interface *intf, struct wuc_device *wdev)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	int i;

	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep = &alt->endpoint[i].desc;
		u8 addr = ep->bEndpointAddress;
		u8 num = usb_endpoint_num(ep);

		if (!usb_endpoint_xfer_bulk(ep))
			continue;

		if (usb_endpoint_dir_in(ep))
			set_bit(num, &wdev->present_bulk_in);
		else
			set_bit(num, &wdev->present_bulk_out);

		dev_info(&intf->dev, "bulk endpoint found: address=0x%02x num=%u %s\n",
			 addr, num, usb_endpoint_dir_in(ep) ? "IN" : "OUT");
	}
}

/**
 * wuc_register_channel() - Allocate and register one SocketCAN netdevice.
 * @wdev: Shared USB adapter state.
 * @channel: Channel index, 0 or 1.
 * Return: 0 on successful register_candev(), otherwise negative errno.
 */
static int wuc_register_channel(struct wuc_device *wdev, int channel)
{
	struct net_device *netdev;
	struct wuc_priv *priv;
	int ret;

	netdev = alloc_candev(sizeof(*priv), WUC_ECHO_SKB_MAX);
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->parent = wdev;
	priv->channel = channel;
	priv->cmd_ep = wdev->cmd_ep[channel];
	priv->msg_ep = wdev->msg_ep[channel];
	atomic_set(&priv->opened, 0);
	atomic_set(&priv->tx_busy, 0);
	priv->flags = 0;
	init_usb_anchor(&priv->tx_anchor);
	INIT_DELAYED_WORK(&priv->rx_work, wuc_rx_work);

	priv->can.clock.freq = 8000000;
	priv->can.bitrate_const = wuc_bitrate_const;
	priv->can.bitrate_const_cnt = ARRAY_SIZE(wuc_bitrate_const);
	/* Linux CAN core accepts either fixed bitrate_const or bittiming_const, not both.
	 * This adapter uses the firmware-supported fixed bitrate table. */
	priv->can.do_set_bittiming = wuc_set_bittiming;
	priv->can.ctrlmode_supported = 0;
	priv->can.state = CAN_STATE_STOPPED;

	netdev->netdev_ops = &wuc_netdev_ops;
	netdev->flags |= IFF_ECHO;
	SET_NETDEV_DEV(netdev, &wdev->intf->dev);

	ret = register_candev(netdev);
	if (ret) {
		netdev_err(netdev, "register_candev failed for channel %d: %d\n", channel, ret);
		free_candev(netdev);
		return ret;
	}

	wdev->netdev[channel] = netdev;
	netdev_info(netdev, "registered waveUSBCAN_b channel %d cmd_ep=%u msg_ep=%u\n",
		    channel, priv->cmd_ep, priv->msg_ep);
	return 0;
}

/**
 * wuc_unregister_channel() - Unregister and free one SocketCAN netdevice.
 * @wdev: Shared USB adapter state.
 * @channel: Channel index, 0 or 1.
 */
static void wuc_unregister_channel(struct wuc_device *wdev, int channel)
{
	struct net_device *netdev = wdev->netdev[channel];

	if (!netdev)
		return;

	{
		struct wuc_priv *priv = netdev_priv(netdev);

		set_bit(WUC_PRIV_SHUTDOWN, &priv->flags);
		atomic_set(&priv->opened, 0);
		netif_stop_queue(netdev);
		cancel_delayed_work_sync(&priv->rx_work);
		usb_kill_anchored_urbs(&priv->tx_anchor);
	}

	unregister_candev(netdev);
	free_candev(netdev);
	wdev->netdev[channel] = NULL;
}

static int wuc_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct wuc_device *wdev;
	int ret;
	int ch;

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;

	wdev->udev = usb_get_dev(interface_to_usbdev(intf));
	wdev->intf = intf;
	wdev->flags = 0;
	mutex_init(&wdev->usb_lock);
	usb_set_intfdata(intf, wdev);
	usb_disable_autosuspend(wdev->udev);

	wuc_scan_endpoints(intf, wdev);

	ret = wuc_apply_endpoint_profile(intf, wdev);
	if (ret)
		goto err_put;

	for (ch = 0; ch < WUC_CHANNELS; ch++) {
		ret = wuc_register_channel(wdev, ch);
		if (ret)
			goto err_unregister;
	}

	dev_info(&intf->dev, WUC_DRIVER_DESC " attached\n");
	return 0;

err_unregister:
	while (--ch >= 0)
		wuc_unregister_channel(wdev, ch);
err_put:
	usb_set_intfdata(intf, NULL);
	usb_enable_autosuspend(wdev->udev);
	usb_put_dev(wdev->udev);
	kfree(wdev);
	return ret;
}

/**
 * wuc_disconnect() - USB disconnect callback.
 * @intf: USB interface being removed.
 */
static void wuc_disconnect(struct usb_interface *intf)
{
	struct wuc_device *wdev = usb_get_intfdata(intf);
	int ch;

	usb_set_intfdata(intf, NULL);
	if (!wdev)
		return;

	set_bit(WUC_DEV_DISCONNECTING, &wdev->flags);

	for (ch = 0; ch < WUC_CHANNELS; ch++) {
		if (wdev->netdev[ch])
			netif_device_detach(wdev->netdev[ch]);
	}

	for (ch = 0; ch < WUC_CHANNELS; ch++)
		wuc_unregister_channel(wdev, ch);

	usb_enable_autosuspend(wdev->udev);
	usb_put_dev(wdev->udev);
	kfree(wdev);
	dev_info(&intf->dev, WUC_DRIVER_NAME " disconnected\n");
}

/** USB device ID table for supported adapters. */
static const struct usb_device_id wuc_table[] = {
	{ USB_DEVICE(WUC_VENDOR_ID, WUC_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, wuc_table);

static struct usb_driver wuc_driver = {
	.name = WUC_DRIVER_NAME,
	.probe = wuc_probe,
	.disconnect = wuc_disconnect,
	.id_table = wuc_table,
};

module_usb_driver(wuc_driver);

MODULE_AUTHOR("Juergen W. Sievers; OpenAI ChatGPT");
MODULE_DESCRIPTION(WUC_DRIVER_DESC);
MODULE_LICENSE("GPL");
