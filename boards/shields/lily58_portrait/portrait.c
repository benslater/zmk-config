/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/conn.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/split/central.h>

/* Draw upright into 32x128, then transpose into the OLED's native 128x32.
 * This avoids depending on SSD1306 hardware rotation support. */
static lv_color_t portrait_pixels[32 * 128];
static lv_color_t display_pixels[128 * 32];
static lv_obj_t *portrait_canvas;
static lv_obj_t *display_canvas;

static void check_split_connection(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    /* This Lily58 has one peripheral. Host connections use the other role. */
    if (bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE &&
        info.role == BT_CONN_ROLE_CENTRAL && info.state == BT_CONN_STATE_CONNECTED) {
        *(bool *)data = true;
    }
}

static void line(int y, const char *text) {
    lv_draw_label_dsc_t style;
    lv_draw_label_dsc_init(&style);
    style.color = lv_color_white();
    style.font = &lv_font_unscii_8;
    style.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(portrait_canvas, 0, y, 32, &style, text);
}

static void refresh(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    bool right_connected = false;
    bt_conn_foreach(BT_CONN_TYPE_LE, check_split_connection, &right_connected);
    uint8_t right_battery = 0;
    int result = zmk_split_central_get_peripheral_battery_level(0, &right_battery);
    char text[8];
    lv_canvas_fill_bg(portrait_canvas, lv_color_black(), LV_OPA_COVER);

    snprintf(text, sizeof(text), "BT%d", zmk_ble_active_profile_index() + 1);
    line(0, text);
    line(12, zmk_ble_active_profile_is_connected() ? "OK" :
             zmk_ble_active_profile_is_open() ? "PAIR" : "WAIT");

    line(32, "L");
    snprintf(text, sizeof(text), "%u%%", (unsigned)zmk_battery_state_of_charge());
    line(44, text);

    line(64, "R");
    if (right_connected && result == 0 && right_battery > 0) {
        snprintf(text, sizeof(text), "%u%%", (unsigned)right_battery);
        line(76, text);
    } else {
        /* ZMK uses zero for missing/reset peripheral battery data. */
        line(76, "--");
    }
    line(88, right_connected ? "LINK" : "OFF");
    line(112, zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB ? "USB" : "BLE");

    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 32; x++) {
            lv_color_t pixel = lv_canvas_get_px(portrait_canvas, x, y);
#if IS_ENABLED(CONFIG_LILY58_PORTRAIT_FLIP)
            lv_canvas_set_px(display_canvas, y, 31 - x, pixel);
#else
            lv_canvas_set_px(display_canvas, 127 - y, x, pixel);
#endif
        }
    }
    lv_obj_invalidate(display_canvas);
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    portrait_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(portrait_canvas, portrait_pixels, 32, 128, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(portrait_canvas, LV_OBJ_FLAG_HIDDEN);

    display_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(display_canvas, display_pixels, 128, 32, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(display_canvas, 0, 0);
    refresh(NULL);
    /* Display-thread timer: LVGL calls never run in Bluetooth callbacks. */
    lv_timer_create(refresh, 1000, NULL);
    return screen;
}
