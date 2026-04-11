#include <zephyr/kernel.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int layer_rgb_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (!ev || !ev->state) return ZMK_EV_EVENT_BUBBLE;

    switch (ev->layer) {
        case 1:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 60,  .s = 100, .b = 50}); // Yellow
            zmk_rgb_underglow_on();
            break;
        case 2:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 120, .s = 100, .b = 50}); // Green
            zmk_rgb_underglow_on();
            break;
        case 3:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 240, .s = 100, .b = 50}); // Blue
            zmk_rgb_underglow_on();
            break;
        case 4:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 0,   .b = 25}); // Dim white
            zmk_rgb_underglow_on();
            break;
        case 5:
        case 6:
            zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0,   .s = 100, .b = 50}); // Red
            zmk_rgb_underglow_on();
            break;
        default:
            zmk_rgb_underglow_off(); // Layer 0 = off
            break;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_rgb, layer_rgb_listener);
ZMK_SUBSCRIPTION(layer_rgb, zmk_layer_state_changed);
