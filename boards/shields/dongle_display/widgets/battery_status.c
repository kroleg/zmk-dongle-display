/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "battery_status.h"

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
    #define SOURCE_OFFSET 1
#else
    #define SOURCE_OFFSET 0
#endif

#ifndef ZMK_SPLIT_BLE_PERIPHERAL_COUNT
#  define ZMK_SPLIT_BLE_PERIPHERAL_COUNT 0
#endif

#ifndef CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_WIDTH
#  define CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_WIDTH 12
#endif

#ifndef CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_HEIGHT
#  define CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_HEIGHT 6
#endif

#define BATTERY_WIDTH  CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_WIDTH
#define BATTERY_HEIGHT CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_HEIGHT

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_state {
    uint8_t source;
    uint8_t level;
    bool usb_present;
};

struct battery_object {
    lv_obj_t *symbol;
    lv_obj_t *label;
} battery_objects[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];

static lv_color_t battery_image_buffer[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET][BATTERY_WIDTH * BATTERY_HEIGHT];

static void draw_battery(lv_obj_t *canvas, uint8_t level, bool usb_present) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    lv_draw_rect_dsc_t rect_fill_dsc;
    lv_draw_rect_dsc_init(&rect_fill_dsc);
    rect_fill_dsc.bg_color = lv_color_white();

    lv_draw_rect_dsc_t rect_outline_dsc;
    lv_draw_rect_dsc_init(&rect_outline_dsc);
    rect_outline_dsc.bg_opa = LV_OPA_TRANSP;
    rect_outline_dsc.border_color = lv_color_white();
    rect_outline_dsc.border_width = 1;

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_HORIZONTAL)
    // Horizontal battery: terminal on right side
    // Terminal nub: 2px wide, 1/3 height centered
    int term_height = BATTERY_HEIGHT / 3;
    int term_y = (BATTERY_HEIGHT - term_height) / 2;
    lv_canvas_draw_rect(canvas, BATTERY_WIDTH - 2, term_y, 2, term_height, &rect_fill_dsc);

    // Body: draw walls as filled rectangles
    int body_width = BATTERY_WIDTH - 2;
    lv_canvas_draw_rect(canvas, 0, 0, body_width, 1, &rect_fill_dsc);                    // top
    lv_canvas_draw_rect(canvas, 0, BATTERY_HEIGHT - 1, body_width, 1, &rect_fill_dsc);   // bottom
    lv_canvas_draw_rect(canvas, 0, 0, 1, BATTERY_HEIGHT, &rect_fill_dsc);                // left
    lv_canvas_draw_rect(canvas, body_width - 1, 0, 1, BATTERY_HEIGHT, &rect_fill_dsc);   // right

    // Fill area inside body (1px inset from walls)
    int fill_area_width = body_width - 2;
    int fill_area_height = BATTERY_HEIGHT - 2;

    // Calculate fill based on level (fills from right, showing empty space on left)
    int fill_width = (fill_area_width * (100 - level)) / 100;
    if (usb_present) {
        // When charging, draw outline instead of fill
        lv_canvas_draw_rect(canvas, 1 + (fill_area_width - fill_width), 1, fill_width, fill_area_height, &rect_outline_dsc);
    } else if (fill_width > 0) {
        lv_canvas_draw_rect(canvas, 1 + (fill_area_width - fill_width), 1, fill_width, fill_area_height, &rect_fill_dsc);
    }
#else
    // Vertical battery: terminal on top
    // Terminal nub: 1/3 width centered, 2px tall
    int term_width = BATTERY_WIDTH / 3;
    int term_x = (BATTERY_WIDTH - term_width) / 2;
    lv_canvas_draw_rect(canvas, term_x, 0, term_width, 2, &rect_fill_dsc);

    // Body: draw walls as filled rectangles
    int body_height = BATTERY_HEIGHT - 2;
    lv_canvas_draw_rect(canvas, 0, 2, BATTERY_WIDTH, 1, &rect_fill_dsc);                 // top
    lv_canvas_draw_rect(canvas, 0, BATTERY_HEIGHT - 1, BATTERY_WIDTH, 1, &rect_fill_dsc); // bottom
    lv_canvas_draw_rect(canvas, 0, 2, 1, body_height, &rect_fill_dsc);                   // left
    lv_canvas_draw_rect(canvas, BATTERY_WIDTH - 1, 2, 1, body_height, &rect_fill_dsc);   // right

    // Fill area inside body (1px inset from walls)
    int fill_area_width = BATTERY_WIDTH - 2;
    int fill_area_height = body_height - 2;

    // Calculate fill based on level (fills from top, showing empty space at bottom)
    int fill_height = (fill_area_height * (100 - level)) / 100;
    if (usb_present) {
        // When charging, draw outline instead of fill
        lv_canvas_draw_rect(canvas, 1, 3, fill_area_width, fill_height, &rect_outline_dsc);
    } else if (fill_height > 0) {
        lv_canvas_draw_rect(canvas, 1, 3, fill_area_width, fill_height, &rect_fill_dsc);
    }
#endif
}

static void set_battery_symbol(lv_obj_t *widget, struct battery_state state) {
    if (state.source >= ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET) {
        return;
    }
    LOG_DBG("source: %d, level: %d, usb: %d", state.source, state.level, state.usb_present);
    lv_obj_t *symbol = battery_objects[state.source].symbol;
    lv_obj_t *label = battery_objects[state.source].label;

    draw_battery(symbol, state.level, state.usb_present);

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SHOW_PERCENT)
    lv_label_set_text_fmt(label, "%4u%%", state.level);
#endif

    if (state.level > 0 || state.usb_present) {
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(symbol);
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SHOW_PERCENT)
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
#endif
    } else {
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SHOW_PERCENT)
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
#endif
    }
}

void battery_status_update_cb(struct battery_state state) {
    struct zmk_widget_dongle_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_symbol(widget->obj, state); }
}

static struct battery_state peripheral_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    return (struct battery_state){
        .source = ev->source + SOURCE_OFFSET,
        .level = ev->state_of_charge,
    };
}

static struct battery_state central_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_state) {
        .source = 0,
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

static struct battery_state battery_status_get_state(const zmk_event_t *eh) { 
    if (as_zmk_peripheral_battery_state_changed(eh) != NULL) {
        return peripheral_battery_status_get_state(eh);
    } else {
        return central_battery_status_get_state(eh);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status, struct battery_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_peripheral_battery_state_changed);

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
#endif /* !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
#endif /* IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY) */

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);

    lv_obj_set_size(widget->obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    int row_height = BATTERY_HEIGHT + 2;

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; i++) {
        lv_obj_t *image_canvas = lv_canvas_create(widget->obj);
        lv_obj_t *battery_label = lv_label_create(widget->obj);

        lv_canvas_set_buffer(image_canvas, battery_image_buffer[i], BATTERY_WIDTH, BATTERY_HEIGHT, LV_IMG_CF_TRUE_COLOR);

        lv_obj_align(image_canvas, LV_ALIGN_TOP_RIGHT, 0, i * row_height);
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SHOW_PERCENT)
        lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -(BATTERY_WIDTH + 10), i * row_height);
#endif

        lv_obj_add_flag(image_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);

        battery_objects[i] = (struct battery_object){
            .symbol = image_canvas,
            .label = battery_label,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}
