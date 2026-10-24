#!/bin/bash
# Renders the dongle screen on the host (no hardware) into tools/oled-sim/out/*.png.
#
# Usage: tools/oled-sim/render.sh [CONFIG_...=y ...]
#   Extra Kconfig options are added to the build, e.g.
#   tools/oled-sim/render.sh CONFIG_ZMK_DONGLE_DISPLAY_MAC_MODIFIERS=y
#   With OLED_SIM_NAME=<name> the frames go to tools/oled-sim/out/<name>/.
#
# Needs Docker. The first run sets up a ZMK workspace in tools/oled-sim/.workspace.
set -e

SIM="$(cd "$(dirname "$0")" && pwd)"
MODULE="$(cd "$SIM/../.." && pwd)"
ZMK_IMAGE=zmkfirmware/zmk-dev-arm:4.1-branch
ZMK_REVISION=main

conf="$SIM/.workspace/oled-sim.conf"
out="out/${OLED_SIM_NAME}"
mkdir -p "$SIM/.workspace" "$SIM/$out"
: > "$conf"  # config/native_sim.conf is picked up by ZMK itself
for opt in "$@"; do echo "$opt" >> "$conf"; done

docker run --rm \
  -v "$MODULE:/zmk-dongle-display" \
  -w /zmk-dongle-display/tools/oled-sim/.workspace \
  "$ZMK_IMAGE" \
  /bin/bash -c "
    set -e
    if [ ! -d .west ]; then
      west init -m https://github.com/zmkfirmware/zmk --mr $ZMK_REVISION --mf app/west.yml .
      west update --narrow --fetch-opt=--depth=1
    fi
    west zephyr-export >/dev/null
    west build -p -b native_sim//zmk_test_mock -s zmk/app -d build -- \
      -DSHIELD=dongle_display \
      -DZMK_CONFIG=/zmk-dongle-display/tools/oled-sim/config \
      -DEXTRA_CONF_FILE=/zmk-dongle-display/tools/oled-sim/.workspace/oled-sim.conf \
      '-DZMK_EXTRA_MODULES=/zmk-dongle-display;/zmk-dongle-display/tools/oled-sim/module'
    timeout 60 build/zephyr/zmk.exe > build/frames.txt
    python3 /zmk-dongle-display/tools/oled-sim/render.py build/frames.txt /zmk-dongle-display/tools/oled-sim/$out
  "
echo "Frames: tools/oled-sim/$out (all.png has every frame)"
