/*
 * layer_rgb_central.c — skreecustom left (central) side
 *
 * Two jobs:
 *   1. React to layer changes and set local RGB (original logic, unchanged).
 *   2. Sync the active layer index to the right (peripheral) half via a
 *      one-byte GATT WRITE-WITHOUT-RESPONSE so its LEDs match.
 *
 * The peripheral half exposes a tiny custom GATT service defined in
 * layer_rgb_peripheral.c with the same UUIDs below.  On every new BLE
 * connection we run a GATT primary-service discovery; if our custom UUID
 * is found we know that connection is the split peripheral (not the host)
 * and save it.  The host connection silently fails discovery and is ignored.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/keymap.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── custom GATT UUIDs — must match layer_rgb_peripheral.c exactly ──────── */

static struct bt_uuid_128 lrgb_svc_uuid = BT_UUID_INIT_128(
    0x73, 0x6b, 0x72, 0x65, 0x65, 0x2d, 0x6c, 0x72,
    0x67, 0x62, 0x2d, 0x73, 0x76, 0x63, 0x00, 0x00
);
static struct bt_uuid_128 lrgb_chr_uuid = BT_UUID_INIT_128(
    0x73, 0x6b, 0x72, 0x65, 0x65, 0x2d, 0x6c, 0x72,
    0x67, 0x62, 0x2d, 0x63, 0x68, 0x72, 0x00, 0x00
);

/* ── peripheral connection state ─────────────────────────────────────────── */

static struct bt_conn              *periph_conn   = NULL;
static uint16_t                     periph_handle = 0;  /* 0 = not yet found */
static bool                         write_pending = false;
static uint8_t                      pending_layer = 0;

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_write_params    write_params;

/* ── local color helper ───────────────────────────────────────────────────── */

static void apply_color_local(uint8_t layer)
{
    switch (layer) {
        case 1: /* Functions — yellow */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 60,  .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            break;
        case 2: /* Arrows — green */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 120, .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            break;
        case 3: /* Numpads — blue */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 240, .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            break;
        case 4: /* Symbols — white dim */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 0,   .b = 25});
            zmk_rgb_underglow_on();
            break;
        case 5: /* donotpress — red */
        case 6: /* bluetooth  — red */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            break;
        default: /* layer 0 — LEDs off */
            zmk_rgb_underglow_off();
            break;
    }
}

/* ── GATT write to peripheral ─────────────────────────────────────────────── */

static void write_cb(struct bt_conn *conn, uint8_t err,
                     struct bt_gatt_write_params *params)
{
    write_pending = false;
    if (err) {
        LOG_WRN("layer_rgb: peripheral write err %d", err);
    }
}

static void send_layer_to_peripheral(uint8_t layer)
{
    if (!periph_conn || !periph_handle) {
        return;
    }
    if (write_pending) {
        pending_layer = layer;  /* queued; sent when current write completes */
        return;
    }

    pending_layer           = layer;
    write_pending           = true;
    write_params.handle     = periph_handle;
    write_params.offset     = 0;
    write_params.data       = &pending_layer;
    write_params.length     = sizeof(pending_layer);
    write_params.func       = write_cb;

    int err = bt_gatt_write(periph_conn, &write_params);
    if (err) {
        LOG_WRN("layer_rgb: bt_gatt_write err %d", err);
        write_pending = false;
    }
}

/* ── GATT discovery callbacks ─────────────────────────────────────────────── */

static uint8_t discover_chr_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) {
        return BT_GATT_ITER_STOP;
    }
    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
    periph_handle = chrc->value_handle;
    LOG_DBG("layer_rgb: chr handle 0x%04x", periph_handle);

    /* Immediately sync current layer so peripheral catches up after reconnect */
    send_layer_to_peripheral(zmk_keymap_highest_layer_active());
    return BT_GATT_ITER_STOP;
}

static uint8_t discover_svc_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) {
        /* Service not found — this is the host connection, not the peripheral */
        return BT_GATT_ITER_STOP;
    }

    periph_conn = bt_conn_ref(conn);
    LOG_DBG("layer_rgb: peripheral svc found, discovering chr");

    disc_params.uuid         = &lrgb_chr_uuid.uuid;
    disc_params.func         = discover_chr_cb;
    disc_params.start_handle = attr->handle + 1;
    disc_params.end_handle   = 0xffff;
    disc_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(conn, &disc_params);
    if (err) {
        LOG_ERR("layer_rgb: chr discovery err %d", err);
    }
    return BT_GATT_ITER_STOP;
}

/* ── BLE connection tracking ──────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err || periph_conn) {
        return;
    }

    disc_params.uuid         = &lrgb_svc_uuid.uuid;
    disc_params.func         = discover_svc_cb;
    disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    disc_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    disc_params.type         = BT_GATT_DISCOVER_PRIMARY;

    int ret = bt_gatt_discover(conn, &disc_params);
    if (ret) {
        LOG_ERR("layer_rgb: svc discovery err %d", ret);
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (conn == periph_conn) {
        bt_conn_unref(periph_conn);
        periph_conn   = NULL;
        periph_handle = 0;
        write_pending = false;
        LOG_DBG("layer_rgb: peripheral disconnected (reason %d)", reason);
    }
}

BT_CONN_CB_DEFINE(lrgb_conn_cb) = {
    .connected    = on_connected,
    .disconnected = on_disconnected,
};

/* ── ZMK layer change listener ────────────────────────────────────────────── */

static int layer_rgb_listener(const zmk_event_t *eh)
{
    uint8_t layer = zmk_keymap_highest_layer_active();
    apply_color_local(layer);
    send_layer_to_peripheral(layer);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_rgb, layer_rgb_listener);
ZMK_SUBSCRIPTION(layer_rgb, zmk_layer_state_changed);
