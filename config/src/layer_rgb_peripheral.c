#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/rgb_underglow.h>
#include <zmk/event_manager.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zephyr/logging/log.h>
#include "layer_rgb_uuid.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static void apply_color(uint8_t layer) {
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

static ssize_t on_layer_written(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags) {
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    apply_color(((const uint8_t *)buf)[0]);
    return (ssize_t)len;
}

BT_GATT_SERVICE_DEFINE(lrgb_peripheral_svc,
    BT_GATT_PRIMARY_SERVICE(LRGB_SVC_UUID),
    BT_GATT_CHARACTERISTIC(LRGB_CHR_UUID,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_layer_written, NULL),
);

static int periph_status_listener(const zmk_event_t *eh) {
    zmk_rgb_underglow_off();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(lrgb_periph_status, periph_status_listener);
ZMK_SUBSCRIPTION(lrgb_periph_status, zmk_split_peripheral_status_changed);
