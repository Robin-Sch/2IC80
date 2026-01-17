/*
 * Copyright (c) 2021-2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

// Audio format 8kHz, 16-bit mono, 160 bytes per 10ms
#define AUDIO_PACKET_SIZE    160
#define BIG_SDU_INTERVAL_US  10000
#define BUF_ALLOC_TIMEOUT_US (BIG_SDU_INTERVAL_US * 2U)

// Ring buffer for UART audio input (holds ~100ms of audio)
#define AUDIO_RING_BUF_SIZE  1600
RING_BUF_DECLARE(audio_ring_buf, AUDIO_RING_BUF_SIZE);

// UART RX buffer for interrupt-driven reception
#define UART_RX_BUF_SIZE 256
static uint8_t uart_rx_buf[UART_RX_BUF_SIZE];

#define BIS_ISO_CHAN_COUNT 1
NET_BUF_POOL_FIXED_DEFINE(bis_tx_pool, BIS_ISO_CHAN_COUNT,
			  BT_ISO_SDU_BUF_SIZE(AUDIO_PACKET_SIZE),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);

static K_SEM_DEFINE(sem_big_cmplt, 0, BIS_ISO_CHAN_COUNT);
static K_SEM_DEFINE(sem_big_term, 0, BIS_ISO_CHAN_COUNT);
static K_SEM_DEFINE(sem_iso_data, CONFIG_BT_ISO_TX_BUF_COUNT,
				   CONFIG_BT_ISO_TX_BUF_COUNT);

static uint16_t seq_num;
static const struct device *uart_dev;

static void uart_rx_callback(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			int recv_len = uart_fifo_read(dev, uart_rx_buf, UART_RX_BUF_SIZE);
			if (recv_len > 0) {
				ring_buf_put(&audio_ring_buf, uart_rx_buf, recv_len);
			}
		}
	}
}

static void iso_connected(struct bt_iso_chan *chan)
{
	const struct bt_iso_chan_path hci_path = {
		.pid = BT_ISO_DATA_PATH_HCI,
		.format = BT_HCI_CODING_FORMAT_TRANSPARENT,
	};
	int err;

	seq_num = 0U;

	err = bt_iso_setup_data_path(chan, BT_HCI_DATAPATH_DIR_HOST_TO_CTLR, &hci_path);
	if (err != 0) {
		// logging disabled
	}

	k_sem_give(&sem_big_cmplt);
}

static void iso_disconnected(struct bt_iso_chan *chan, uint8_t reason)
{
	k_sem_give(&sem_big_term);
}

static void iso_sent(struct bt_iso_chan *chan)
{
	k_sem_give(&sem_iso_data);
}

static struct bt_iso_chan_ops iso_ops = {
	.connected	= iso_connected,
	.disconnected	= iso_disconnected,
	.sent           = iso_sent,
};

static struct bt_iso_chan_io_qos iso_tx_qos = {
	.sdu = AUDIO_PACKET_SIZE,
	.rtn = 1,
	.phy = BT_GAP_LE_PHY_2M,
};

static struct bt_iso_chan_qos bis_iso_qos = {
	.tx = &iso_tx_qos,
};

static struct bt_iso_chan bis_iso_chan[] = {
	{ .ops = &iso_ops, .qos = &bis_iso_qos, },
};

static struct bt_iso_chan *bis[] = {
	&bis_iso_chan[0],
};

static struct bt_iso_big_create_param big_create_param = {
	.num_bis = BIS_ISO_CHAN_COUNT,
	.bis_channels = bis,
	.interval = BIG_SDU_INTERVAL_US,
	.latency = 10,
	.packing = BT_ISO_PACKING_SEQUENTIAL,
	.framing = 0,
};

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, "AliceISO", 8),
};

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

	// Wait for USB DTR (host connected)
	uint32_t dtr = 0;
	while (!dtr) {
		uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
		k_sleep(K_MSEC(100));
	}
	k_sleep(K_SECONDS(1));

	// Setup UART interrupt-driven RX
	uart_irq_callback_set(uart_dev, uart_rx_callback);
	uart_irq_rx_enable(uart_dev);

	const uint16_t adv_interval_ms = 60U;
	const uint16_t ext_adv_interval_ms = adv_interval_ms - 10U;
	const struct bt_le_adv_param *ext_adv_param = BT_LE_ADV_PARAM(
		BT_LE_ADV_OPT_EXT_ADV, BT_GAP_MS_TO_ADV_INTERVAL(ext_adv_interval_ms),
		BT_GAP_MS_TO_ADV_INTERVAL(ext_adv_interval_ms), NULL);
	const struct bt_le_per_adv_param *per_adv_param = BT_LE_PER_ADV_PARAM(
		BT_GAP_MS_TO_PER_ADV_INTERVAL(adv_interval_ms),
		BT_GAP_MS_TO_PER_ADV_INTERVAL(adv_interval_ms), BT_LE_PER_ADV_OPT_NONE);
	struct bt_le_ext_adv *adv;
	struct bt_iso_big *big;
	int err;

	// Audio packet buffer
	uint8_t audio_data[AUDIO_PACKET_SIZE];

	// Initialize Bluetooth
	err = bt_enable(NULL);
	if (err) {
		return 0;
	}

	// Create advertising set
	err = bt_le_ext_adv_create(ext_adv_param, NULL, &adv);
	if (err) {
		return 0;
	}

	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		return 0;
	}

	err = bt_le_per_adv_set_param(adv, per_adv_param);
	if (err) {
		return 0;
	}

	err = bt_le_per_adv_start(adv);
	if (err) {
		return 0;
	}

	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		return 0;
	}

	/* Create BIG */
	err = bt_iso_big_create(adv, &big_create_param, &big);
	if (err) {
		return 0;
	}

	for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
		err = k_sem_take(&sem_big_cmplt, K_FOREVER);
		if (err) {
			return 0;
		}
	}

	// Main audio transmit loop
	while (true) {
		for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
			struct net_buf *buf;
			int ret;

			buf = net_buf_alloc(&bis_tx_pool, K_USEC(BUF_ALLOC_TIMEOUT_US));
			if (!buf) {
				return 0;
			}

			ret = k_sem_take(&sem_iso_data, K_USEC(BUF_ALLOC_TIMEOUT_US));
			if (ret) {
				net_buf_unref(buf);
				return 0;
			}

			net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);

			// Get audio data from ring buffer
			uint32_t bytes_read = ring_buf_get(&audio_ring_buf, audio_data, AUDIO_PACKET_SIZE);
			
			// If not enough audio data, pad with silence
			if (bytes_read < AUDIO_PACKET_SIZE) {
				memset(&audio_data[bytes_read], 0, AUDIO_PACKET_SIZE - bytes_read);
			}

			net_buf_add_mem(buf, audio_data, AUDIO_PACKET_SIZE);
			ret = bt_iso_chan_send(&bis_iso_chan[chan], buf, seq_num);
			if (ret < 0) {
				net_buf_unref(buf);
				return 0;
			}
		}

		seq_num++;
	}
}