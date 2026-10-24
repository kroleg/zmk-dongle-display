#!/bin/bash
# Renders every set of options in variants.txt into tools/oled-sim/out/<name>/.
# Usage: tools/oled-sim/render-variants.sh
set -e

SIM="$(cd "$(dirname "$0")" && pwd)"
rm -rf "$SIM/out"
while read -r name options; do
  case "$name" in ''|'#'*) continue ;; esac
  echo "== $name ${options:-(defaults)}"
  # word splitting only, so quotes in string values reach Kconfig
  OLED_SIM_NAME="$name" "$SIM/render.sh" $options
done < "$SIM/variants.txt"
