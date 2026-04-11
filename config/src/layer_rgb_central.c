/*
 * layer_rgb_central.c — skreecustom left (central) side
 *
 * Uses zmk_split_bt_invoke_behavior() to invoke rgb_ug directly on the
 * peripheral over ZMK's existing split BLE pipe — no custom GATT needed.
 * Also listens for peripheral reconnection to re-sync the current layer.
 */

#include <zephyr/kernel.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/split/bluetooth/central.h>
#include <dt-bindings/zmk/rgb.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* HSB encoding matching ZMK's RGB_COLOR_HSB_VAL: h=9bit, s=7bit, b=7bit */
#define HSB_ENC(h, s, b) \
    ((uint32_t)((h) & 0x1FFU) | ((uint32_t)((s) & 0x7FU) << 9) | ((uint32_t)((b) & 0x7FU) << 16))

/* Invoke an rgb_ug command on peripheral 0 via ZMK's split pipe */
static void periph_rgb(uint32_t cmd, uint32_t param) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = "RGB_UNDERGLOW",
        .param1 = cmd,
        .param2 = param,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_split_bt_invoke_behavior(0, &binding, event, true);
}

static void apply_layer(uint8_t layer) {
    switch (layer) {
        case 1: /* Functions — yellow */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 60,  .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            periph_rgb(RGB_COLOR_HSB_CMD, HSB_ENC(60,  100, 50));
            periph_rgb(RGB_ON, 0);
            break;
        case 2: /* Arrows — green */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 120, .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            periph_rgb(RGB_COLOR_HSB_CMD, HSB_ENC(120, 100, 50));
            periph_rgb(RGB_ON, 0);
            break;
        case 3: /* Numpads — blue */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 240, .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            periph_rgb(RGB_COLOR_HSB_CMD, HSB_ENC(240, 100, 50));
            periph_rgb(RGB_ON, 0);
            break;
        case 4: /* Symbols — white dim */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 0,   .b = 25});
            zmk_rgb_underglow_on();
            periph_rgb(RGB_COLOR_HSB_CMD, HSB_ENC(0, 0, 25));
            periph_rgb(RGB_ON, 0);
            break;
        case 5: /* donotpress — red */
        case 6: /* bluetooth  — red */
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 100, .b = 50});
            zmk_rgb_underglow_on();
            periph_rgb(RGB_COLOR_HSB_CMD, HSB_ENC(0, 100, 50));
            periph_rgb(RGB_ON, 0);
            break;
        default: /* layer 0 — off */
            zmk_rgb_underglow_off();
            periph_rgb(RGB_OFF, 0);
            break;
    }
}

static int layer_rgb_listener(const zmk_event_t *eh) {
    apply_layer(zmk_keymap_highest_layer_active());
    return ZMK_EV_EVENT_BUBBLE;
}

static int periph_status_listener(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(eh);
    if (ev && ev->connected) {
        /* Peripheral just (re)connected — push current layer immediately */
        apply_layer(zmk_keymap_highest_layer_active());
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_rgb, layer_rgb_listener);
ZMK_SUBSCRIPTION(layer_rgb, zmk_layer_state_changed);

ZMK_LISTENER(layer_rgb_periph, periph_status_listener);
ZMK_SUBSCRIPTION(layer_rgb_periph, zmk_split_peripheral_status_changed);
