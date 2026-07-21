/*
 * Keyball61 left nice!view status widget with a persistent boot/wake art cycle.
 * Based on ZMK v0.2's nice_view peripheral_status.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "peripheral_status.h"

LV_IMG_DECLARE(rtr_c2_folded_ribbon);
LV_IMG_DECLARE(rtr_cl1_carved_monolith);
LV_IMG_DECLARE(rtr_cl3_skeleton_stack);
LV_IMG_DECLARE(rtr_cl5_splitfield);
LV_IMG_DECLARE(rtr_track_totem);

static const lv_img_dsc_t *const art_images[] = {
    &rtr_c2_folded_ribbon,
    &rtr_cl1_carved_monolith,
    &rtr_cl3_skeleton_stack,
    &rtr_cl5_splitfield,
    &rtr_track_totem,
};

static uint8_t art_index;

#if IS_ENABLED(CONFIG_SETTINGS)

static int art_index_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                  void *cb_arg) {
    if (!settings_name_steq(name, "index", NULL)) {
        return -ENOENT;
    }
    if (len != sizeof(art_index)) {
        return -EINVAL;
    }

    int err = read_cb(cb_arg, &art_index, sizeof(art_index));
    if (err <= 0) {
        return err;
    }
    art_index %= ARRAY_SIZE(art_images);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(keyball61_art, "keyball61_art", NULL, art_index_settings_set, NULL,
                               NULL);

static void save_next_art_index(struct k_work *work) {
    uint8_t next = (art_index + 1) % ARRAY_SIZE(art_images);
    int err = settings_save_one("keyball61_art/index", &next, sizeof(next));
    if (err) {
        LOG_WRN("Failed to save next Keyball61 art index (%d)", err);
    }
}

static K_WORK_DELAYABLE_DEFINE(save_next_art_index_work, save_next_art_index);

static void schedule_next_art_index_save(void) {
    /* Match ZMK's normal delayed-settings policy to reduce flash wear. The
     * 10-minute sleep timeout leaves ample time for this one-byte save. */
    k_work_reschedule(&save_next_art_index_work,
                      K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
}

#else

static void schedule_next_art_index_save(void) {}

#endif /* CONFIG_SETTINGS */

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);
    draw_battery(canvas, state);
    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                        state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif
    widget->state.battery = state.level;
    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

static struct peripheral_status_state get_state(const zmk_event_t *eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;
    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    lv_obj_t *art = lv_img_create(widget->obj);
    lv_img_set_src(art, art_images[art_index]);
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 0, 0);
    schedule_next_art_index_save();

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
