# Host tests

The portable core is tested on the host with `gcc`; no Pico SDK is needed.

```bash
make -C tests run
```

The type and sprite tables are generated automatically into
`assets/generated/`. A healthy run ends with `126 checks, 0 failures`.

The tests cover timezone rules, ranking and filtering, jsmn feed parsing, HTTP
parsing, DNS and NTP wire formats, config CRC/serialisation, CLI redaction,
formatting and wrapping, brightness/palette hysteresis, demo cards and sprite
family selection.

## Optional LVGL render probe

`ui_probe.c` renders the clock and demo cards through the same `src/ui.c`,
generated fonts and generated sprites used on the device. It needs a host LVGL
9.3 build:

```bash
LVGL_PATH=${LVGL_PATH:-$HOME/lvgl}
cmake -S "$LVGL_PATH" -B /tmp/opencode/lvgl-host -G Ninja \
  -DLV_BUILD_CONF_PATH="$PWD/lv_conf.h"
cmake --build /tmp/opencode/lvgl-host --target lvgl -j 8

gcc -std=gnu11 -D_GNU_SOURCE -DFC_HOST_TEST -O2 -g \
  -I"$LVGL_PATH" -Iinclude -Iassets/generated -Itests \
  -DLV_CONF_PATH="\"$PWD/lv_conf.h\"" \
  -o /tmp/opencode/ui_probe \
  tests/ui_probe.c src/ui.c src/clock.c src/timezone.c src/format.c \
  src/types.c src/sprite.c src/demo.c src/log.c \
  assets/generated/fc_fonts.c assets/generated/fc_types.c \
  assets/generated/fc_sprites.c \
  /tmp/opencode/lvgl-host/lib/liblvgl.a -lm

/tmp/opencode/ui_probe
```

The probe prints font metrics, text widths and non-black pixel counts, and
exits non-zero if font mapping or rendering is broken. It writes PPM frames to
`/tmp/opencode/`.
