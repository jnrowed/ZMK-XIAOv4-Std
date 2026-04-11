#include <zephyr/kernel.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/rgb.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static void invoke_rgb(uint32_t cmd, uint32_t color) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = "RGB_UG",
        .param1 = cmd,
        .param2 = color,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
}

static int layer_rgb_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (!ev) return ZMK_EV_EVENT_BUBBLE;

    uint8_t layer = zmk_keymap_highest_layer_active();

    switch (layer) {
        case 1:
            invoke_rgb(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB(60,  100, 50)); // Yellow
            break;
        case 2:
            invoke_rgb(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB(120, 100, 50)); // Green
            break;
        case 3:
            invoke_rgb(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB(240, 100, 50)); // Blue
            break;
        case 4:
            invoke_rgb(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB(0,   0,   25)); // Dim white
            break;
        case 5:
        case 6:
            invoke_rgb(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB(0,   100, 50)); // Red
            break;
        default:
            invoke_rgb(RGB_TOG_CMD, 0); // Layer 0 = off
            break;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_rgb, layer_rgb_listener);
ZMK_SUBSCRIPTION(layer_rgb, zmk_layer_state_changed);
