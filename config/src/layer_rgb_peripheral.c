#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static struct bt_uuid_128 lrgb_svc_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6c726762, 0x2d73, 0x7663, 0x2d00, 0x000000000000)
);
static struct bt_uuid_128 lrgb_chr_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6c726762, 0x2d63, 0x6872, 0x2d00, 0x000000000000)
);

/* Blink n times in given color, then go back off */
static void blink(uint16_t h, uint8_t s, uint8_t b, int times) {
    for (int i = 0; i < times; i++) {
        zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = h, .s = s, .b = b});
        zmk_rgb_underglow_on();
        k_sleep(K_MSEC(200));
        zmk_rgb_underglow_off();
        k_sleep(K_MSEC(200));
    }
}

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
                                uint16_t offset, uint8_t flags)
{
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    /* White blink = write arrived */
    blink(0, 0, 80, 1);

    apply_color(((const uint8_t *)buf)[0]);
    return (ssize_t)len;
}

BT_GATT_SERVICE_DEFINE(lrgb_peripheral_svc,
    BT_GATT_PRIMARY_SERVICE(&lrgb_svc_uuid),
    BT_GATT_CHARACTERISTIC(&lrgb_chr_uuid.uuid,
                           BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_layer_written, NULL),
);

/* Boot blink: 3x cyan = peripheral is alive and service is registered */
static int lrgb_periph_boot(void) {
    k_sleep(K_SECONDS(2)); /* wait for RGB to initialize */
    blink(180, 100, 80, 3);
    return 0;
}

static K_THREAD_DEFINE(lrgb_boot_tid, 512,
                       lrgb_periph_boot, NULL, NULL, NULL,
                       K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
