/*
 * Display driver for native_posix renders of the dongle screen.
 * Reports the same format as the SSD1306/SH1106 driver (MONO10, vertically tiled)
 * so LVGL renders exactly as on the dongle, keeps the pixels in memory and
 * prints them on oled_sim_dump(); render.py turns that output into PNGs.
 */

#define DT_DRV_COMPAT zmk_oled_sim

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>

#include "oled_sim.h"

#define WIDTH DT_INST_PROP(0, width)
#define HEIGHT DT_INST_PROP(0, height)
#define PAGES (HEIGHT / 8)

// like the controller's RAM: one byte = 8 vertical pixels, bit 0 on top
static uint8_t ram[PAGES][WIDTH];
static bool blanked = true;

static int sim_write(const struct device *dev, const uint16_t x, const uint16_t y,
                     const struct display_buffer_descriptor *desc, const void *buf) {
    const uint8_t *src = buf;
    for (int page = 0; page < desc->height / 8 && y / 8 + page < PAGES; page++) {
        int w = MIN(desc->width, WIDTH - x);
        memcpy(&ram[y / 8 + page][x], src + page * desc->pitch, w);
    }
    return 0;
}

static int sim_blanking_on(const struct device *dev) {
    blanked = true;
    return 0;
}

static int sim_blanking_off(const struct device *dev) {
    blanked = false;
    return 0;
}

static void sim_get_capabilities(const struct device *dev, struct display_capabilities *caps) {
    memset(caps, 0, sizeof(*caps));
    caps->x_resolution = WIDTH;
    caps->y_resolution = HEIGHT;
    caps->supported_pixel_formats = PIXEL_FORMAT_MONO10;
    caps->current_pixel_format = PIXEL_FORMAT_MONO10;
    caps->screen_info = SCREEN_INFO_MONO_VTILED;
}

static int sim_set_pixel_format(const struct device *dev, const enum display_pixel_format pf) {
    return pf == PIXEL_FORMAT_MONO10 ? 0 : -ENOTSUP;
}

// "OLED <name> <width> <height> <blanked>", then one hex line per page
void oled_sim_dump(const char *name) {
    printk("OLED %s %d %d %d\n", name, WIDTH, HEIGHT, blanked);
    for (int page = 0; page < PAGES; page++) {
        for (int x = 0; x < WIDTH; x++) {
            printk("%02x", ram[page][x]);
        }
        printk("\n");
    }
}

static DEVICE_API(display, sim_api) = {
    .blanking_on = sim_blanking_on,
    .blanking_off = sim_blanking_off,
    .write = sim_write,
    .get_capabilities = sim_get_capabilities,
    .set_pixel_format = sim_set_pixel_format,
};

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,
                      &sim_api);
