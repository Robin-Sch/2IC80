/*
 * Copyright (c) 2021-2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

#define TIMEOUT_SYNC_CREATE K_SECONDS(10)
#define NAME_LEN            30

// Scanning for Alice
#define BT_LE_SCAN_CUSTOM BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
                       BT_LE_SCAN_OPT_NONE, \
                       BT_GAP_SCAN_FAST_INTERVAL, \
                       BT_GAP_SCAN_FAST_WINDOW)

#define PA_RETRY_COUNT 6

#define BIS_ISO_CHAN_COUNT 2

static bool         per_adv_found;
static bool         per_adv_lost;
static bt_addr_le_t per_addr;
static uint8_t      per_sid;
static uint32_t     per_interval_us;
static uint32_t     iso_recv_count;

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
    { .rx = &iso_rx_qos[1], },
};

static struct bt_iso_chan bis_iso_chan[] = {
    { .ops = &iso_ops, .qos = &bis_iso_qos[0], },
    { .ops = &iso_ops, .qos = &bis_iso_qos[1], },
};

static struct bt_iso_chan *bis[] = {
    &bis_iso_chan[0],
    &bis_iso_chan[1],
};

static struct bt_iso_big_sync_param big_sync_param = {
    .bis_channels = bis,
    .num_bis = BIS_ISO_CHAN_COUNT,
    .bis_bitfield = (BIT_MASK(BIS_ISO_CHAN_COUNT)),
    .mse = BT_ISO_SYNC_MSE_ANY,
    .sync_timeout = 300,
};

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

static const char *phy2str(uint8_t phy)
{
    switch (phy) {
    case 0: return "No packets";
    case BT_GAP_LE_PHY_1M: return "LE 1M";
    case BT_GAP_LE_PHY_2M: return "LE 2M";
    case BT_GAP_LE_PHY_CODED: return "LE Coded";
    default: return "Unknown";
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
        printk("[MALLORY] FOUND ALICE! Syncing...\n");
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
    printk("[MALLORY] Synced to Alice PA\n");
    k_sem_give(&sem_per_sync);
}

static void term_cb(struct bt_le_per_adv_sync *sync,
            const struct bt_le_per_adv_sync_term_info *info)
{
    printk("[MALLORY] Lost Alice PA Sync\n");
    per_adv_lost = true;
    k_sem_give(&sem_per_sync_lost);
}

static void recv_cb(struct bt_le_per_adv_sync *sync,
            const struct bt_le_per_adv_sync_recv_info *info,
            struct net_buf_simple *buf)
{
    // Periodic advertising data from Alice (not ISO data)
    printk("(dot)\n");
}

static void biginfo_cb(struct bt_le_per_adv_sync *sync,
               const struct bt_iso_biginfo *biginfo)
{
    printk("[MALLORY] Received Alice BIG Info\n");
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
    if (!(info->flags & BT_ISO_FLAGS_VALID)) {
        return;
    }
    if (buf->len == 0) {
        return;
    }

    // Log what we receive from Alice for debugging
    if (buf->len >= 4) {
        uint32_t count = sys_get_le32(buf->data);
        printk("[MALLORY RX] Received from Alice: %u\n", count);
        iso_recv_count++;
    }
}

static void iso_connected(struct bt_iso_chan *chan)
{
    printk("ISO Channel %p connected\n", chan);

    if (chan == &bis_iso_chan[0] || chan == &bis_iso_chan[1]) {
        k_sem_give(&sem_big_sync);
    }
}

static void iso_disconnected(struct bt_iso_chan *chan, uint8_t reason)
{
    printk("ISO Channel %p disconnected (reason 0x%02x)\n", chan, reason);
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

int main(void)
{
    // USB Setup
    if (usb_enable(NULL)) return 0;
    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint32_t dtr = 0;
    while (!dtr) {
        uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        k_sleep(K_MSEC(100));
    }
    k_sleep(K_SECONDS(1));
    printk("=== MALLORY (BISON ATTACKER) STARTED ===\n");

    int err;

    // Initialize Bluetooth
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    // Register callbacks for scanning and sync
    bt_le_scan_cb_register(&scan_callbacks);
    bt_le_per_adv_sync_cb_register(&sync_callbacks);

    struct bt_le_per_adv_sync_param sync_create_param;
    struct bt_le_per_adv_sync *sync;
    struct bt_iso_big *big_rx;

    do {
        reset_semaphores();
        per_adv_lost = false;

        // Start Scanning for Alice
        per_adv_found = false;
        printk("[MALLORY] Scanning for Alice...\n");
        err = bt_le_scan_start(BT_LE_SCAN_CUSTOM, NULL);
        if (err) {
            printk("Scan start failed (err %d)\n", err);
            return 0;
        }

        // Wait for Alice
        err = k_sem_take(&sem_per_adv, K_FOREVER);
        if (err) return 0;

        bt_le_scan_stop();
        printk("[MALLORY] Found Alice. Creating PA Sync...\n");

        // Sync with Alice's Periodic Advertising
        bt_addr_le_copy(&sync_create_param.addr, &per_addr);
        sync_create_param.options = 0;
        sync_create_param.sid = per_sid;
        sync_create_param.skip = 0;
        sync_create_param.timeout = 0x190; 

        err = bt_le_per_adv_sync_create(&sync_create_param, &sync);
        if (err) {
            printk("Failed to create sync (%d)\n", err);
            continue;
        }

        // Wait for PA Sync Established
        err = k_sem_take(&sem_per_sync, K_SECONDS(2));
        if (err) {
            printk("Sync timeout. Retrying...\n");
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        // Wait for BIG Info
        err = k_sem_take(&sem_per_big_info, K_SECONDS(2));
        if (err) {
            printk("BIG Info timeout. Retrying...\n");
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        printk("[MALLORY] Syncing to Alice's BIG (driver injection will activate)...\n");
        err = bt_iso_big_sync(sync, &big_sync_param, &big_rx);
        if (err) {
            printk("Failed to sync BIG (%d)\n", err);
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        // Wait for BIS channels to connect
        for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
            k_sem_take(&sem_big_sync, TIMEOUT_SYNC_CREATE);
        }
        
        printk("[MALLORY] BIG Sync Established!\n");
        printk("[MALLORY] Driver-level injection is now ACTIVE.\n");

        // Wait until we lose sync
        k_sem_take(&sem_per_sync_lost, K_FOREVER);

        printk("[MALLORY] Lost Sync with Alice. Cleaning up...\n");
        bt_iso_big_terminate(big_rx);
        bt_le_per_adv_sync_delete(sync);
        k_sleep(K_MSEC(1000));

    } while (true);
}