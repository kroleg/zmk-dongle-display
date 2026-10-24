# OLED simulator

Renders the dongle screen on your computer, without flashing anything, into PNGs:

```sh
tools/oled-sim/render.sh
# frames in tools/oled-sim/out/: boot.png, battery-100-5.png, layer-0.png, ..., all.png
```

Extra Kconfig options go on the command line, to see how they look:

```sh
tools/oled-sim/render.sh CONFIG_ZMK_DONGLE_DISPLAY_MAC_MODIFIERS=y CONFIG_ZMK_DONGLE_DISPLAY_WPM=y
```

Needs Docker. The first run downloads ZMK into `tools/oled-sim/.workspace` (a few minutes),
after that a run takes about 10 seconds.

## How it works

It is a normal ZMK build of this shield for `native_sim` (ZMK's host test board), so the
widgets, LVGL and Zephyr's display code are the same as on the dongle. Only the ends are
replaced:

- `module/oled_sim.c`: a display driver that reports the same format as the SSD1306/SH1106
  driver (128x64, 1 bit, vertically tiled), keeps the pixels in memory and prints them on request
- `module/scenario.c`: puts the screen through some states (boot, peripheral batteries,
  every layer) and prints a frame after each, then exits
- `config/native_sim.keymap`: a few named layers; the screen only shows the layer name
- `module/stubs.c`: USB and battery events the widgets link against, which a host build
  without USB and battery reporting doesn't have
- `render.py`: turns the printed frames into PNGs (lit pixels white)

The host build has no BLE, so the battery widget is sized for two peripherals with a define
in `module/CMakeLists.txt`, and the scenario raises their battery events itself.

## Variants

`tools/oled-sim/render-variants.sh` renders every set of options in `variants.txt` into
`out/<name>/`. Add a line there to cover a new option.

## On GitHub

`.github/workflows/oled-sim.yml` runs `render-variants.sh` on every push and pull request that
touches the shield, and uploads `out/` as the **oled-preview** artifact of the run.
