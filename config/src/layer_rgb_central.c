/*
 * layer_rgb_central.c — skreecustom left (central) side
 *
 * Fix: GATT discovery is delayed via k_work_delayable to avoid colliding
 * with ZMK's own split GATT setup which runs immediately on connection.
 * If bt_gatt_discover() returns -EBUSY we reschedule and retry.
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

/* ── state ───────────────────────────────────────────────────────────────── */

static struct bt_conn              *periph_conn   = NULL;
static uint16_t                     periph_handle = 0;
static bool                         write_pending = false;
static uint8_t                      pending_layer = 0;

static struct bt_gatt_discover_params disc_params;
static struct bt_gatt_write_params    write_params;

/* Work item for delayed / retried GATT discovery */
static struct k_work_delayable lrgb_disc_work;
static struct bt_conn         *disc_target_conn = NULL; /* conn under discovery */

/* ── color helper ────────────────────────────────────────────────────────── */

static void apply_color_local(uint8_t layer)
{
    switch (layer) {
        case 1:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 60,  .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        case 2:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 120, .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        case 3:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 240, .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        case 4:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 0,   .b = 25});
            zmk_rgb_underglow_on();  break;
        case 5:
        case 6:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 100, .b = 50});
            zmk_rgb_underglow_on();  break;
        default:
            zmk_rgb_underglow_off(); break;
    }
}

/* ── GATT write ──────────────────────────────────────────────────────────── */

static void write_cb(struct bt_conn *conn, uint8_t err,
                     struct bt_gatt_write_params *params)
{
    write_pending = false;
    if (err) {
        LOG_WRN("layer_rgb: write err %d", err);
    }
}

static void send_layer_to_peripheral(uint8_t layer)
{
    if (!periph_conn || !periph_handle) return;

    if (write_pending) {
        pending_layer = layer;
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

/* ── GATT discovery callbacks ────────────────────────────────────────────── */

static uint8_t discover_chr_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) return BT_GATT_ITER_STOP;

    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
    periph_handle = chrc->value_handle;
    LOG_DBG("layer_rgb: chr handle 0x%04x — syncing current layer", periph_handle);
    send_layer_to_peripheral(zmk_keymap_highest_layer_active());
    return BT_GATT_ITER_STOP;
}

static uint8_t discover_svc_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) {
        /* Not the peripheral — host connection. Release the ref we took. */
        bt_conn_unref(disc_target_conn);
        disc_target_conn = NULL;
        return BT_GATT_ITER_STOP;
    }

    periph_conn      = disc_target_conn; /* transfer ownership of the ref */
    disc_target_conn = NULL;
    LOG_DBG("layer_rgb: peripheral svc found");

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
    if (!disc_target_conn) return; /* connection was dropped before we ran */

    disc_params.uuid         = &lrgb_svc_uuid.uuid;
    disc_params.func         = discover_svc_cb;
    disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    disc_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    disc_params.type         = BT_GATT_DISCOVER_PRIMARY;

    int err = bt_gatt_discover(disc_target_conn, &disc_params);
    if (err == -EBUSY) {
        /* ZMK's split GATT setup still in progress — retry in 1 s */
        LOG_DBG("layer_rgb: GATT busy, retrying in 1s");
        k_work_schedule(&lrgb_disc_work, K_SECONDS(1));
        return;
    }
    if (err) {
        LOG_ERR("layer_rgb: svc discover err %d", err);
        bt_conn_unref(disc_target_conn);
        disc_target_conn = NULL;
    }
    /* on success, disc_target_conn ownership passes to discover_svc_cb */
}

/* ── BLE connection tracking ─────────────────────────────────────────────── */

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err || periph_conn || disc_target_conn) {
        return; /* failed, or already tracking a connection */
    }

    /* Hold a ref across the async discovery.
     * Released in discover_svc_cb (either on miss or on success). */
    disc_target_conn = bt_conn_ref(conn);

    /* Delay 3 s to let ZMK's own split GATT discovery finish first.
     * The work fn retries automatically if it's still busy. */
    k_work_schedule(&lrgb_disc_work, K_SECONDS(3));
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
    if (conn == disc_target_conn) {
        /* Dropped before discovery completed */
        k_work_cancel_delayable(&lrgb_disc_work);
        bt_conn_unref(disc_target_conn);
        disc_target_conn = NULL;
    }
}

BT_CONN_CB_DEFINE(lrgb_conn_cb) = {
    .connected    = on_connected,
    .disconnected = on_disconnected,
};

/* ── init (register work item) ───────────────────────────────────────────── */

static int lrgb_init(void)
{
    k_work_init_delayable(&lrgb_disc_work, lrgb_disc_work_fn);
    return 0;
}
SYS_INIT(lrgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

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
