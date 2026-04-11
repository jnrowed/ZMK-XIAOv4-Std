/*
 * layer_rgb_central.c — skreecustom left (central) side
 *
 * Fixes vs previous version:
 *  1. Filter on_connected by BLE role: only run discovery when we are the
 *     BLE central/master (= connection to the split peripheral half).
 *     Host connections make us the BLE peripheral/slave — skip those.
 *  2. Use bt_gatt_write_without_response() to match the peripheral's
 *     WRITE_WITHOUT_RESP characteristic and avoid write_pending deadlock.
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

/* ── UUIDs — must match layer_rgb_peripheral.c ──────────────────────────── */

static struct bt_uuid_128 lrgb_svc_uuid = BT_UUID_INIT_128(
    0x73, 0x6b, 0x72, 0x65, 0x65, 0x2d, 0x6c, 0x72,
    0x67, 0x62, 0x2d, 0x73, 0x76, 0x63, 0x00, 0x00
);
static struct bt_uuid_128 lrgb_chr_uuid = BT_UUID_INIT_128(
    0x73, 0x6b, 0x72, 0x65, 0x65, 0x2d, 0x6c, 0x72,
    0x67, 0x62, 0x2d, 0x63, 0x68, 0x72, 0x00, 0x00
);

/* ── forward declaration ─────────────────────────────────────────────────── */

static void lrgb_disc_work_fn(struct k_work *work);

/* ── state ───────────────────────────────────────────────────────────────── */

static struct bt_conn              *periph_conn   = NULL;
static uint16_t                     periph_handle = 0;
static struct bt_conn              *disc_target_conn = NULL;

static struct bt_gatt_discover_params disc_params;
static K_WORK_DELAYABLE_DEFINE(lrgb_disc_work, lrgb_disc_work_fn);

/* ── color helper ────────────────────────────────────────────────────────── */

static void apply_color_local(uint8_t layer)
{
    switch (layer) {
        case 1: /* Functions — yellow */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 60,  .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        case 2: /* Arrows — green */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 120, .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        case 3: /* Numpads — blue */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 240, .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        case 4: /* Symbols — white dim */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 0,   .b = 25});
            zmk_rgb_underglow_on();  break;
        case 5: /* donotpress — red */
        case 6: /* bluetooth  — red */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        default: /* layer 0 — off */
            zmk_rgb_underglow_off(); break;
    }
}

/* ── GATT write ──────────────────────────────────────────────────────────── */

static void send_layer_to_peripheral(uint8_t layer)
{
    if (!periph_conn || !periph_handle) return;

    static uint8_t layer_buf;
    layer_buf = layer;

    int err = bt_gatt_write_without_response(periph_conn, periph_handle,
                                              &layer_buf, sizeof(layer_buf),
                                              false);
    if (err) {
        LOG_WRN("layer_rgb: write_without_response err %d", err);
    }
}

/* ── GATT discovery callbacks ────────────────────────────────────────────── */

static uint8_t discover_chr_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) return BT_GATT_ITER_STOP;

    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
    periph_handle = chrc->value_handle;
    LOG_DBG("layer_rgb: chr handle 0x%04x", periph_handle);

    /* Sync current layer immediately so peripheral isn't stuck on boot color */
    send_layer_to_peripheral(zmk_keymap_highest_layer_active());
    return BT_GATT_ITER_STOP;
}

static uint8_t discover_svc_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) {
        /* Should not happen: we only run discovery on master connections,
         * which are always the split peripheral — but guard anyway. */
        bt_conn_unref(disc_target_conn);
        disc_target_conn = NULL;
        return BT_GATT_ITER_STOP;
    }

    periph_conn      = disc_target_conn; /* transfer ref ownership */
    disc_target_conn = NULL;
    LOG_DBG("layer_rgb: peripheral svc found, discovering chr");

    disc_params.uuid         = &lrgb_chr_uuid.uuid;
    disc_params.func         = discover_chr_cb;
    disc_params.start_handle = attr->handle + 1;
    disc_params.end_handle   = 0xffff;
    disc_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(periph_conn, &disc_params);
    if (err) LOG_ERR("layer_rgb: chr discover err %d", err);
    return BT_GATT_ITER_STOP;
}

/* ── delayed discovery work item ─────────────────────────────────────────── */

static void lrgb_disc_work_fn(struct k_work *work)
{
    if (!disc_target_conn) return;

    disc_params.uuid         = &lrgb_svc_uuid.uuid;
    disc_params.func         = discover_svc_cb;
    disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    disc_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    disc_params.type         = BT_GATT_DISCOVER_PRIMARY;

    int err = bt_gatt_discover(disc_target_conn, &disc_params);
    if (err == -EBUSY) {
        LOG_DBG("layer_rgb: GATT busy, retrying in 1s");
        k_work_schedule(&lrgb_disc_work, K_SECONDS(1));
        return;
    }
    if (err) {
        LOG_ERR("layer_rgb: svc discover err %d", err);
        bt_conn_unref(disc_target_conn);
        disc_target_conn = NULL;
    }
}

/* ── BLE connection tracking ─────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err || periph_conn || disc_target_conn) return;

    /* KEY FIX: only run discovery on connections where we are the BLE master.
     * BT_CONN_ROLE_CENTRAL = we initiated the connection = split peripheral.
     * BT_CONN_ROLE_PERIPHERAL = host connected to us = skip entirely. */
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0) return;
    if (info.role != BT_CONN_ROLE_CENTRAL) {
        LOG_DBG("layer_rgb: skipping slave-role connection (host)");
        return;
    }

    disc_target_conn = bt_conn_ref(conn);
    k_work_schedule(&lrgb_disc_work, K_SECONDS(3));
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (conn == periph_conn) {
        bt_conn_unref(periph_conn);
        periph_conn   = NULL;
        periph_handle = 0;
        LOG_DBG("layer_rgb: peripheral disconnected (reason %d)", reason);
    }
    if (conn == disc_target_conn) {
        k_work_cancel_delayable(&lrgb_disc_work);
        bt_conn_unref(disc_target_conn);
        disc_target_conn = NULL;
    }
}

BT_CONN_CB_DEFINE(lrgb_conn_cb) = {
    .connected    = on_connected,
    .disconnected = on_disconnected,
};

/* ── ZMK layer listener ──────────────────────────────────────────────────── */

static int layer_rgb_listener(const zmk_event_t *eh)
{
    uint8_t layer = zmk_keymap_highest_layer_active();
    apply_color_local(layer);
    send_layer_to_peripheral(layer);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_rgb, layer_rgb_listener);
ZMK_SUBSCRIPTION(layer_rgb, zmk_layer_state_changed);
