/*
 * layer_rgb_peripheral.c — skreecustom right (peripheral) side
 *
 * Exposes a single-characteristic GATT service. The central (left) half
 * writes one byte — the active layer index — whenever the layer changes.
 * We apply the matching color locally so both halves always show the same
 * underglow.
 *
 * UUIDs must match layer_rgb_central.c exactly.
 * Color mapping mirrors apply_color_local() in layer_rgb_central.c.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── custom GATT UUIDs — must match layer_rgb_central.c exactly ─────────── */

static struct bt_uuid_128 lrgb_svc_uuid = BT_UUID_INIT_128(
    0x73, 0x6b, 0x72, 0x65, 0x65, 0x2d, 0x6c, 0x72,
    0x67, 0x62, 0x2d, 0x73, 0x76, 0x63, 0x00, 0x00
);
static struct bt_uuid_128 lrgb_chr_uuid = BT_UUID_INIT_128(
    0x73, 0x6b, 0x72, 0x65, 0x65, 0x2d, 0x6c, 0x72,
    0x67, 0x62, 0x2d, 0x63, 0x68, 0x72, 0x00, 0x00
);

/* ── color application — mirrors central's apply_color_local() ───────────── */

static void apply_color(uint8_t layer)
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

/* ── GATT write handler ───────────────────────────────────────────────────── */

static ssize_t on_layer_written(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags)
{
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t layer = ((const uint8_t *)buf)[0];
    LOG_DBG("layer_rgb: peripheral received layer %u", layer);
    apply_color(layer);
    return (ssize_t)len;
}

/* ── GATT service definition ──────────────────────────────────────────────── */

BT_GATT_SERVICE_DEFINE(lrgb_peripheral_svc,
    BT_GATT_PRIMARY_SERVICE(&lrgb_svc_uuid),
    BT_GATT_CHARACTERISTIC(&lrgb_chr_uuid.uuid,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_layer_written, NULL),
);
