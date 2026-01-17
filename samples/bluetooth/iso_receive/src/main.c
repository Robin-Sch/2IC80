/*
 * Copyright (c) 2021-2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

// Debug markers
#define DEBUG_MARKER_SCAN     0xAA
#define DEBUG_MARKER_FOUND    0xBB
#define DEBUG_MARKER_SYNC     0xCC
#define DEBUG_MARKER_BIG      0xDD
#define DEBUG_MARKER_AUDIO    0xEE

#define TIMEOUT_SYNC_CREATE K_SECONDS(10)
#define NAME_LEN            30

#define BT_LE_SCAN_CUSTOM BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
					   BT_LE_SCAN_OPT_NONE, \
					   BT_GAP_SCAN_FAST_INTERVAL, \
					   BT_GAP_SCAN_FAST_WINDOW)

#define PA_RETRY_COUNT 6
#define BIS_ISO_CHAN_COUNT 1

static bool         per_adv_found;
static bool         per_adv_lost;
static bt_addr_le_t per_addr;
static uint8_t      per_sid;
static uint32_t     per_interval_us;

static K_SEM_DEFINE(sem_per_adv, 0, 1);
static K_SEM_DEFINE(sem_per_sync, 0, 1);
static K_SEM_DEFINE(sem_per_sync_lost, 0, 1);
static K_SEM_DEFINE(sem_per_big_info, 0, 1);
static K_SEM_DEFINE(sem_big_sync, 0, BIS_ISO_CHAN_COUNT);
static K_SEM_DEFINE(sem_big_sync_lost, 0, BIS_ISO_CHAN_COUNT);

static const struct device *uart_dev;

// Send debug marker byte
static void debug_marker(uint8_t marker)
{
    uart_poll_out(uart_dev, marker);
    uart_poll_out(uart_dev, marker);
    uart_poll_out(uart_dev, marker);
    uart_poll_out(uart_dev, marker);
}

static bool data_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, NAME_LEN - 1);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	default:
		return true;
	}
}

static void scan_recv(const struct bt_le_scan_recv_info *info,
		      struct net_buf_simple *buf)
{
	char name[NAME_LEN];

	(void)memset(name, 0, sizeof(name));
	bt_data_parse(buf, data_cb, name);

	// Look for Alice
	if (!per_adv_found && info->interval && strcmp(name, "AliceISO") == 0) {
		debug_marker(DEBUG_MARKER_FOUND);  // Found Alice!
		per_adv_found = true;
		per_sid = info->sid;
		per_interval_us = BT_CONN_INTERVAL_TO_US(info->interval);
		bt_addr_le_copy(&per_addr, info->addr);
		k_sem_give(&sem_per_adv);
	}
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};

static void sync_cb(struct bt_le_per_adv_sync *sync,
		    struct bt_le_per_adv_sync_synced_info *info)
{
	debug_marker(DEBUG_MARKER_SYNC);  // PA Sync established
	k_sem_give(&sem_per_sync);
}

static void term_cb(struct bt_le_per_adv_sync *sync,
		    const struct bt_le_per_adv_sync_term_info *info)
{
	per_adv_lost = true;
	k_sem_give(&sem_per_sync_lost);
}

static void recv_cb(struct bt_le_per_adv_sync *sync,
		    const struct bt_le_per_adv_sync_recv_info *info,
		    struct net_buf_simple *buf)
{
	//
}

static void biginfo_cb(struct bt_le_per_adv_sync *sync,
		       const struct bt_iso_biginfo *biginfo)
{
	debug_marker(DEBUG_MARKER_BIG); // BIG Info received
	k_sem_give(&sem_per_big_info);
}

static struct bt_le_per_adv_sync_cb sync_callbacks = {
	.synced = sync_cb,
	.term = term_cb,
	.recv = recv_cb,
	.biginfo = biginfo_cb,
};

// ISO receive callback - output audio to UART
static void iso_recv(struct bt_iso_chan *chan, const struct bt_iso_recv_info *info,
		struct net_buf *buf)
{
	// Skip invalid packets
	if (!(info->flags & BT_ISO_FLAGS_VALID)) {
		return;
	}

	if (buf->len == 0) {
		return;
	}

	// Output raw audio bytes to UART
	for (uint16_t i = 0; i < buf->len; i++) {
		uart_poll_out(uart_dev, buf->data[i]);
	}
}

static void iso_connected(struct bt_iso_chan *chan)
{
	const struct bt_iso_chan_path hci_path = {
		.pid = BT_ISO_DATA_PATH_HCI,
		.format = BT_HCI_CODING_FORMAT_TRANSPARENT,
	};

	debug_marker(DEBUG_MARKER_AUDIO);  // ISO connected, audio starting
	bt_iso_setup_data_path(chan, BT_HCI_DATAPATH_DIR_CTLR_TO_HOST, &hci_path);
	k_sem_give(&sem_big_sync);
}

static void iso_disconnected(struct bt_iso_chan *chan, uint8_t reason)
{
	if (reason != BT_HCI_ERR_OP_CANCELLED_BY_HOST) {
		k_sem_give(&sem_big_sync_lost);
	}
}

static struct bt_iso_chan_ops iso_ops = {
	.recv		= iso_recv,
	.connected	= iso_connected,
	.disconnected	= iso_disconnected,
};

static struct bt_iso_chan_io_qos iso_rx_qos[BIS_ISO_CHAN_COUNT];

static struct bt_iso_chan_qos bis_iso_qos[] = {
	{ .rx = &iso_rx_qos[0], },
};

static struct bt_iso_chan bis_iso_chan[] = {
	{ .ops = &iso_ops, .qos = &bis_iso_qos[0], },
};

static struct bt_iso_chan *bis[] = {
	&bis_iso_chan[0],
};

static struct bt_iso_big_sync_param big_sync_param = {
	.bis_channels = bis,
	.num_bis = BIS_ISO_CHAN_COUNT,
	.bis_bitfield = (BIT_MASK(BIS_ISO_CHAN_COUNT)),
	.mse = BT_ISO_SYNC_MSE_ANY,
	.sync_timeout = 100,
};

static void reset_semaphores(void)
{
	k_sem_reset(&sem_per_adv);
	k_sem_reset(&sem_per_sync);
	k_sem_reset(&sem_per_sync_lost);
	k_sem_reset(&sem_per_big_info);
	k_sem_reset(&sem_big_sync);
	k_sem_reset(&sem_big_sync_lost);
}

int main(void)
{
	// Enable USB
	if (usb_enable(NULL)) {
		return 0;
	}

	// Get UART device
	uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (!device_is_ready(uart_dev)) {
		return 0;
	}

	// Wait for USB DTR
	uint32_t dtr = 0;
	while (!dtr) {
		uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}
	k_sleep(K_SECONDS(1));

	// Send startup marker
	debug_marker(DEBUG_MARKER_SCAN);  // Starting scan

	struct bt_le_per_adv_sync_param sync_create_param;
	struct bt_le_per_adv_sync *sync;
	struct bt_iso_big *big;
	uint32_t sem_timeout_us;
	int err;

	// Initialize Bluetooth
	err = bt_enable(NULL);
	if (err) {
		return 0;
	}

	bt_le_scan_cb_register(&scan_callbacks);
	bt_le_per_adv_sync_cb_register(&sync_callbacks);

	do {
		reset_semaphores();
		per_adv_lost = false;

		// Start scanning
		per_adv_found = false;
		err = bt_le_scan_start(BT_LE_SCAN_CUSTOM, NULL);
		if (err) {
			return 0;
		}

		// Wait for Alice
		err = k_sem_take(&sem_per_adv, K_FOREVER);
		if (err) {
			return 0;
		}

		bt_le_scan_stop();

		// Create PA sync
		bt_addr_le_copy(&sync_create_param.addr, &per_addr);
		sync_create_param.options = 0;
		sync_create_param.sid = per_sid;
		sync_create_param.skip = 0;
		sync_create_param.timeout = (per_interval_us * PA_RETRY_COUNT) /
						(10 * USEC_PER_MSEC);
		sem_timeout_us = per_interval_us * PA_RETRY_COUNT;
		
		err = bt_le_per_adv_sync_create(&sync_create_param, &sync);
		if (err) {
			continue;
		}

		// Wait for PA sync
		err = k_sem_take(&sem_per_sync, K_SECONDS(2));
		if (err) {
			bt_le_per_adv_sync_delete(sync);
			continue;
		}

		// Wait for BIG info
		err = k_sem_take(&sem_per_big_info, K_USEC(sem_timeout_us));
		if (err) {
			if (per_adv_lost) {
				continue;
			}
			bt_le_per_adv_sync_delete(sync);
			continue;
		}

big_sync_create:
		// Create BIG sync
		err = bt_iso_big_sync(sync, &big_sync_param, &big);
		if (err) {
			return 0;
		}

		for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
			err = k_sem_take(&sem_big_sync, TIMEOUT_SYNC_CREATE);
			if (err) {
				break;
			}
		}
		
		if (err) {
			bt_iso_big_terminate(big);
			goto per_sync_lost_check;
		}

		// Wait for sync lost
		for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
			k_sem_take(&sem_big_sync_lost, K_FOREVER);
		}

per_sync_lost_check:
		err = k_sem_take(&sem_per_sync_lost, K_NO_WAIT);
		if (err) {
			goto big_sync_create;
		}
	} while (true);
}
