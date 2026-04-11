#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/keymap.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static struct bt_uuid_128 lrgb_svc_uuid = {
    .uuid = { BT_UUID_TYPE_128 },
    .val = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x2d, 0x63, 0x76, 0x73, 0x2d,
             0x62, 0x67, 0x72, 0x6c }
};
static struct bt_uuid_128 lrgb_chr_uuid = {
    .uuid = { BT_UUID_TYPE_128 },
    .val = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x2d, 0x72, 0x68, 0x63, 0x2d,
             0x62, 0x67, 0x72, 0x6c }
};

static void lrgb_disc_work_fn(struct k_work *work);

static struct bt_conn               *periph_conn      = NULL;
static uint16_t                      periph_handle    = 0;
static struct bt_conn               *disc_target_conn = NULL;
static struct bt_gatt_discover_params disc_params;
static K_WORK_DELAYABLE_DEFINE(lrgb_disc_work, lrgb_disc_work_fn);
static uint8_t                       disc_attempts    = 0;

static void apply_color_local(uint8_t layer) {
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

static struct bt_gatt_write_params write_params;
static uint8_t pending_layer = 0;

static void send_layer_to_peripheral(uint8_t layer) {
    if (!periph_conn || !periph_handle) return;

    pending_layer           = layer;
    write_params.handle     = periph_handle;
    write_params.offset     = 0;
    write_params.data       = &pending_layer;
    write_params.length     = sizeof(pending_layer);
    write_params.func       = NULL;

    bt_gatt_write(periph_conn, &write_params);
}

static uint8_t discover_chr_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params) {
    if (!attr) {
        send_layer_to_peripheral(zmk_keymap_highest_layer_active());
        return BT_GATT_ITER_STOP;
    }
    struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
    periph_handle = chrc->value_handle;
    return BT_GATT_ITER_STOP;
}

static uint8_t discover_svc_cb(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params) {
    if (!attr) {
        /* Service not found this attempt — retry */
        k_work_schedule(&lrgb_disc_work, K_SECONDS(2));
        return BT_GATT_ITER_STOP;
    }

    periph_conn      = disc_target_conn;
    disc_target_conn = NULL;

    disc_params.uuid         = &lrgb_chr_uuid.uuid;
    disc_params.func         = discover_chr_cb;
    disc_params.start_handle = attr->handle + 1;
    disc_params.end_handle   = 0xffff;
    disc_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;

    bt_gatt_discover(periph_conn, &disc_params);
    return BT_GATT_ITER_STOP;
}

static void lrgb_disc_work_fn(struct k_work *work) {
    if (!disc_target_conn) {
        disc_attempts = 0;
        return;
    }

    if (disc_attempts >= 15) {
        disc_attempts = 0;
        bt_conn_unref(disc_target_conn);
        disc_target_conn = NULL;
        return;
    }

    disc_attempts++;

    disc_params.uuid         = &lrgb_svc_uuid.uuid;
    disc_params.func         = discover_svc_cb;
    disc_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    disc_params.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    disc_params.type         = BT_GATT_DISCOVER_PRIMARY;

    int err = bt_gatt_discover(disc_target_conn, &disc_params);
    if (err) {
        k_work_schedule(&lrgb_disc_work, K_SECONDS(2));
    }
}

static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err || periph_conn || disc_target_conn) return;

    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0) return;
    if (info.role != BT_CONN_ROLE_CENTRAL) return;

    disc_attempts    = 0;
    disc_target_conn = bt_conn_ref(conn);
    k_work_schedule(&lrgb_disc_work, K_SECONDS(1));
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (conn == periph_conn) {
        bt_conn_unref(periph_conn);
        periph_conn   = NULL;
        periph_handle = 0;
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

static int layer_rgb_listener(const zmk_event_t *eh) {
    uint8_t layer = zmk_keymap_highest_layer_active();
    apply_color_local(layer);
    send_layer_to_peripheral(layer);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_rgb, layer_rgb_listener);
ZMK_SUBSCRIPTION(layer_rgb, zmk_layer_state_changed);
