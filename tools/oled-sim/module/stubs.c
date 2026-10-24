/*
 * What the dongle display links against but a native build without USB and
 * battery reporting doesn't have.
 */

#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#if !IS_ENABLED(CONFIG_ZMK_USB)
ZMK_EVENT_IMPL(zmk_usb_conn_state_changed);

bool zmk_usb_is_hid_ready(void) { return true; }
#endif

#if !IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
ZMK_EVENT_IMPL(zmk_battery_state_changed);
ZMK_EVENT_IMPL(zmk_peripheral_battery_state_changed);

uint8_t zmk_battery_state_of_charge(void) { return 0; }
#endif
