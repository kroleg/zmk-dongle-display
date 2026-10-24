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

#define SLOTS (ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET)

#define ICON_CLASSIC IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_ICON_CLASSIC)
#define ICON_HORIZONTAL IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_ICON_HORIZONTAL)
#define SHOW_ICON (ICON_CLASSIC || ICON_HORIZONTAL)
#define PERCENT_INSIDE IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_PERCENT_INSIDE)
#define SHOW_LABEL                                                                                 \
    (!SHOW_ICON || (IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SHOW_PERCENT) && !PERCENT_INSIDE))
#define SHOW_DISCONNECTED IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SHOW_DISCONNECTED)

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_ICON_HORIZONTAL)
#define ICON_WIDTH CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_WIDTH
#define ICON_HEIGHT CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_HEIGHT
#else
#define ICON_WIDTH 5
#define ICON_HEIGHT 8
#endif

#define LABEL_GAP 4 // between the percentage and the icon
#define ROW_GAP 1
#define CELL_GAP 4

#define BUFFER_SIZE LV_CANVAS_BUF_SIZE(ICON_WIDTH, ICON_HEIGHT, LV_COLOR_FORMAT_GET_BPP(LV_COLOR_FORMAT_L8), LV_DRAW_BUF_STRIDE_ALIGN)

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_state {
    uint8_t source;
    uint8_t level;
    bool usb_present;
};

struct battery_object {
    lv_obj_t *symbol;
    lv_obj_t *label;
} battery_objects[SLOTS];
    
static uint8_t battery_image_buffer[SLOTS][BUFFER_SIZE];

// x, y, width, height -> LVGL's inclusive area
static void draw_rect(lv_layer_t *layer, const lv_draw_rect_dsc_t *dsc, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    lv_area_t coords = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, dsc, &coords);
}

static void draw_classic(lv_obj_t *canvas, lv_layer_t *layer, uint8_t level, bool usb_present) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    lv_draw_rect_dsc_t rect_fill_dsc;
    lv_draw_rect_dsc_init(&rect_fill_dsc);
    rect_fill_dsc.bg_color = lv_color_white();

    if (usb_present) {
        rect_fill_dsc.bg_opa = LV_OPA_TRANSP;
        rect_fill_dsc.border_color = lv_color_white();
        rect_fill_dsc.border_width = 1;
    }

    lv_canvas_set_px(canvas, 0, 0, lv_color_white(), LV_OPA_COVER);
    lv_canvas_set_px(canvas, 4, 0, lv_color_white(), LV_OPA_COVER);

    lv_area_t rect_coords;
    bool rect_draw = true;
    
    if (level <= 10 || usb_present) {
        rect_coords = (lv_area_t){1, 2, 3, 6};
    } else if (level <= 30) {
        rect_coords = (lv_area_t){1, 2, 3, 5};
    } else if (level <= 50) {
        rect_coords = (lv_area_t){1, 2, 3, 4};
    } else if (level <= 70) {
        rect_coords = (lv_area_t){1, 2, 3, 3};
    } else if (level <= 90) {
        rect_coords = (lv_area_t){1, 2, 3, 2};
    } else {
        rect_draw = false;
    }

    if (rect_draw) {
        lv_draw_rect(layer, &rect_fill_dsc, &rect_coords);
    }
}

// Black is a lit pixel (light theme on a display with inversion-on), so this
// icon is drawn in black on white, like the text.
static void draw_horizontal(lv_obj_t *canvas, lv_layer_t *layer, uint8_t level, bool usb_present,
                            char *text, size_t text_size) {
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    lv_draw_rect_dsc_t fill_dsc;
    lv_draw_rect_dsc_init(&fill_dsc);
    fill_dsc.bg_color = lv_color_black();

    lv_draw_rect_dsc_t outline_dsc;
    lv_draw_rect_dsc_init(&outline_dsc);
    outline_dsc.bg_opa = LV_OPA_TRANSP;
    outline_dsc.border_color = lv_color_black();
    outline_dsc.border_width = 1;

    // terminal on the right: 2px wide, 1/3 of the height
    int term_height = MAX(ICON_HEIGHT / 3, 1);
    draw_rect(layer, &fill_dsc, ICON_WIDTH - 2, (ICON_HEIGHT - term_height) / 2, 2, term_height);

    int body_width = ICON_WIDTH - 2;
    draw_rect(layer, &outline_dsc, 0, 0, body_width, ICON_HEIGHT);

    // inside the outline
    int inner_width = body_width - 2;
    int inner_height = ICON_HEIGHT - 2;
    const lv_draw_rect_dsc_t *dsc = usb_present ? &outline_dsc : &fill_dsc;

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_PERCENT_INSIDE)
    ARG_UNUSED(dsc);
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_black();
    label_dsc.font = &lv_font_unscii_8;
    label_dsc.letter_space = 1;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    snprintk(text, text_size, "%u", level);
    label_dsc.text = text;
    int font_height = lv_font_get_line_height(label_dsc.font);
    lv_area_t text_coords = {1, (ICON_HEIGHT - font_height) / 2, body_width - 2,
                             (ICON_HEIGHT - font_height) / 2 + font_height - 1};
    lv_draw_label(layer, &label_dsc, &text_coords);
#elif CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SEGMENTS > 0
    // sections 1px apart and 1px off the outline; any charge lights at least one
    const int segments = CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SEGMENTS;
    int space = inner_width - 2;
    int lit = (level * segments + 99) / 100;
    for (int i = 0; i < lit; i++) {
        int x1 = i * (space + 1) / segments;
        int x2 = (i + 1) * (space + 1) / segments - 1;
        draw_rect(layer, dsc, 2 + x1, 2, x2 - x1, inner_height - 2);
    }
#else
    // charge fills from the left
    draw_rect(layer, dsc, 1, 1, (inner_width * level + 50) / 100, inner_height);
#endif
}

// a peripheral that isn't connected: an X in the middle of the icon's place
static void draw_disconnected(lv_obj_t *canvas) {
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);
    int side = MIN(ICON_WIDTH, ICON_HEIGHT);
    int x0 = (ICON_WIDTH - side) / 2, y0 = (ICON_HEIGHT - side) / 2;
    for (int k = 0; k < side; k++) {
        lv_canvas_set_px(canvas, x0 + k, y0 + k, lv_color_black(), LV_OPA_COVER);
        lv_canvas_set_px(canvas, x0 + side - 1 - k, y0 + k, lv_color_black(), LV_OPA_COVER);
    }
}

static void draw_battery(lv_obj_t *canvas, uint8_t level, bool usb_present, bool disconnected) {
    if (disconnected) {
        draw_disconnected(canvas);
        return;
    }

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    char text[4]; // drawn when the layer is finished, so it lives until then
    if (ICON_CLASSIC) {
        draw_classic(canvas, &layer, level, usb_present);
    } else {
        draw_horizontal(canvas, &layer, level, usb_present, text, sizeof(text));
    }
    lv_canvas_finish_layer(canvas, &layer);
}

static void set_battery_symbol(lv_obj_t *widget, struct battery_state state) {
    if (state.source >= SLOTS) {
        return;
    }
    LOG_DBG("source: %d, level: %d, usb: %d", state.source, state.level, state.usb_present);
    lv_obj_t *symbol = battery_objects[state.source].symbol;
    lv_obj_t *label = battery_objects[state.source].label;

    // peripherals report level 0 when they disconnect, and nothing yet at boot
    bool disconnected = SHOW_DISCONNECTED && state.source >= SOURCE_OFFSET && state.level == 0 &&
                        !state.usb_present;
    bool visible = state.level > 0 || state.usb_present || disconnected;

    if (SHOW_ICON) {
        draw_battery(symbol, state.level, state.usb_present, disconnected);
    }
    if (SHOW_LABEL) {
        if (disconnected) {
            lv_label_set_text(label, SHOW_ICON ? "" : "X");
        } else {
            lv_label_set_text_fmt(label, "%u%%", state.level);
        }
    }

    if (SHOW_ICON && visible) {
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(symbol);
    } else {
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
    }
    if (SHOW_LABEL && visible) {
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    } else {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
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
    const struct zmk_battery_state_changed *ev = eh ? as_zmk_battery_state_changed(eh) : NULL;
    return (struct battery_state) {
        // without a dongle battery slot 0 is a peripheral: don't show the dongle's level there
        .source = SOURCE_OFFSET ? 0 : UINT8_MAX,
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

static struct battery_state battery_status_get_state(const zmk_event_t *eh) { 
    // eh is NULL on the initial refresh, and as_zmk_*() dereferences it
    if (eh != NULL && as_zmk_peripheral_battery_state_changed(eh) != NULL) {
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

    // the label is as wide as "100%", right-aligned against the icon
    const lv_font_t *font = lv_obj_get_style_text_font(parent, LV_PART_MAIN);
    int letter_space = lv_obj_get_style_text_letter_space(parent, LV_PART_MAIN);
    lv_point_t text_size;
    lv_text_get_size(&text_size, "100%", font, letter_space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int label_width = SHOW_LABEL ? text_size.x : 0;
    int icon_width = SHOW_ICON ? ICON_WIDTH : 0;
    int row_height = MAX(SHOW_ICON ? ICON_HEIGHT : 0, SHOW_LABEL ? text_size.y : 0);
    int gap = SHOW_LABEL && SHOW_ICON ? LABEL_GAP : 0;
    int cell_width = label_width + gap + icon_width;

    // fixed size: the cells are placed from the right edge
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SIDE_BY_SIDE)
    lv_obj_set_size(widget->obj, MAX(SLOTS * (cell_width + CELL_GAP) - CELL_GAP, 1), row_height);
#else
    lv_obj_set_size(widget->obj, cell_width, MAX(SLOTS * (row_height + ROW_GAP) - ROW_GAP, 1));
#endif

    for (int i = 0; i < SLOTS; i++) {
        lv_obj_t *image_canvas = lv_canvas_create(widget->obj);
        lv_obj_t *battery_label = lv_label_create(widget->obj);

        lv_canvas_set_buffer(image_canvas, battery_image_buffer[i], ICON_WIDTH, ICON_HEIGHT, LV_COLOR_FORMAT_L8);

        // place of this slot: the dongle first, then the peripherals in pairing order
        int pos = i;
        if (IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SWAP_SIDES) && i >= SOURCE_OFFSET) {
            pos = SLOTS - 1 - (i - SOURCE_OFFSET);
        }
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_BATTERY_SIDE_BY_SIDE)
        int x = -(SLOTS - 1 - pos) * (cell_width + CELL_GAP), y = 0;
#else
        int x = 0, y = pos * (row_height + ROW_GAP);
#endif
        lv_obj_align(image_canvas, LV_ALIGN_TOP_RIGHT, x, y + (row_height - ICON_HEIGHT) / 2);
        lv_obj_set_width(battery_label, label_width);
        lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, x - icon_width - gap, y + (row_height - text_size.y) / 2);

        lv_obj_add_flag(image_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
        
        battery_objects[i] = (struct battery_object){
            .symbol = image_canvas,
            .label = battery_label,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    // peripherals show as not connected until they report
    for (int i = SOURCE_OFFSET; i < SLOTS; i++) {
        set_battery_symbol(widget->obj, (struct battery_state){.source = i, .level = 0});
    }

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}
