/*
 * Puts the screen through the states worth looking at and dumps a frame for
 * each: boot, peripheral batteries, every layer. Then exits.
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zmk/events/battery_state_changed.h>
#include <zmk/keymap.h>

#include "oled_sim.h"

// long enough for the display thread to redraw
#define SETTLE K_MSEC(500)

static void snap(const char *name) {
    k_sleep(SETTLE);
    oled_sim_dump(name);
}

static void batteries(uint8_t first, uint8_t second) {
    raise_zmk_peripheral_battery_state_changed(
        (struct zmk_peripheral_battery_state_changed){.source = 0, .state_of_charge = first});
    raise_zmk_peripheral_battery_state_changed(
        (struct zmk_peripheral_battery_state_changed){.source = 1, .state_of_charge = second});
}

static void run(void) {
    snap("boot");

    batteries(100, 5);
    snap("battery-100-5");

    for (zmk_keymap_layer_id_t layer = 0; layer < ZMK_KEYMAP_LAYERS_LEN; layer++) {
        char name[64];
        zmk_keymap_layer_to(layer, false);
        snprintk(name, sizeof(name), "layer-%d", layer);
        snap(name);
    }

    printk("OLED-END\n");
    exit(0);
}

K_THREAD_DEFINE(oled_sim_scenario, 2048, run, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO,
                0, 1000);
