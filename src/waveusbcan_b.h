// SPDX-License-Identifier: GPL-2.0-only
/**
 * @file waveusbcan_b.h
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
 * @author Jürgen W. Sievers <project owner, protocol reverse engineering,
 * hardware testing, R-Net bench use>
 * @author OpenAI ChatGPT <driver generation, documentation, regression tooling>
 * @copyright Copyright (C) 2026 Jürgen W. Sievers and OpenAI ChatGPT
 * @license GPL-2.0-only
 */

#define WUC_DRIVER_NAME "waveusbcan_b"
#define WUC_DRIVER_DESC "SocketCAN driver prototype for Waveshare USB-CAN-B / CANalyst-II"
#define WUC_VENDOR_ID 0x04d8
#define WUC_PRODUCT_ID 0x0053

#define WUC_CHANNELS 2
#define WUC_USB_PACKET_SIZE 64
#define WUC_CAN_MSG_SIZE 21
#define WUC_MSGS_PER_PACKET 3
#define WUC_ECHO_SKB_MAX 1

#define WUC_DEFAULT_USB_TIMEOUT_MS 1000
#define WUC_DEFAULT_POLL_MS 20
#define WUC_MAX_RX_PACKETS 64

typedef enum : u32 {
  WUC_CMD_INIT = 0x01,
  WUC_CMD_START = 0x02,
  WUC_CMD_STOP = 0x03,
  WUC_CMD_CLEAR_RX = 0x05,
  WUC_CMD_DEVICE_RESET = 0x07,
  WUC_CMD_MSG_STATUS = 0x0a,
  WUC_CMD_CAN_STATUS = 0x0b
} wuc_cmd_t;

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
