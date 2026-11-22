# Flightcard

A small always-on display that shows the most interesting aircraft flying near
you, plus a clock. It runs on a Raspberry Pi Pico 2 W with a Pimoroni Pico
Display Pack 2.8 and talks directly to the public [adsb.fi](https://adsb.fi)
feed over WiFi — no API key or account required.

This repository contains the C / FreeRTOS firmware. Two tasks share the two
RP2350 cores: the display task owns the panel, clock, sensors and USB CLI, and
the network task owns WiFi, NTP, DNS, TLS/HTTPS, feed parsing and enrichment.
The display therefore keeps ticking while the network is slow or offline.

Features:

- 320 × 240 ST7789 display driven over SPI with DMA, rendered with LVGL 9.
- Clock plus a route-band card for the best current flight (callsign, type,
  route, altitude, speed, distance).
- Ranks nearby aircraft by apparent size, with altitude/radius/speed filters.
- Optional BH1750 ambient-light sensor for automatic brightness and a
  day/night palette.
- Optional DS3231 or PCF8563 RTC so the clock survives a power cut.
- WiFi credentials and monitoring location can be baked in at build time or
  provisioned over the USB serial CLI.
- Hardware watchdog, bounded network timeouts and separate core affinities so
  a stuck socket cannot stop the clock.

## Hardware

| Part | Required | Notes |
|---|---|---|
| Raspberry Pi Pico 2 W | Yes | RP2350 with 2.4 GHz WiFi. The non-W Pico 2 has no radio. |
| Pimoroni Pico Display Pack 2.8 (PIM715) | Yes | 320 × 240 ST7789 over SPI, plugs onto the Pico headers. |
| Micro-USB **data** cable | Yes | Many charge-only cables will not enumerate. |
| 5 V USB supply, ≥ 1 A | Yes | A marginal supply can make WiFi association unreliable. |
| BH1750 light sensor | Optional | I2C, address `0x23`. Enables auto brightness and palette switching. |
| DS3231 or PCF8563 RTC | Optional | I2C, addresses `0x68` or `0x51`; keeps time without WiFi. |
| 4-pin JST-SH / Qw/ST cable | Optional | For the I2C sensors. |
| Enclosure or stand | Optional | The display pack is fine on a desk as-is. |

### Wiring

The display pack plugs directly into the Pico 2 W headers. The firmware uses
these display pins (the PIM715 defaults):

| Signal | GPIO |
|---|---|
| SPI SCK | 18 |
| SPI MOSI / TX | 19 |
| CS | 17 |
| DC | 16 |
| Backlight | 20 |

Optional I2C devices go on **I2C0**:

| Signal | GPIO |
|---|---|
| SDA | 4 |
| SCL | 5 |

Connect sensors to `3V3` and `GND` as well. BH1750 is detected at `0x23`, a
DS3231 at `0x68`, and a PCF8563 at `0x51`; absent devices are simply skipped.

## Build

### Dependencies

- CMake 3.13 or newer, Ninja, and the Arm GNU toolchain used by the Pico SDK.
- Python 3 (the asset converter is standard-library only).
- [Pico SDK](https://github.com/raspberrypi/pico-sdk) **2.2.0 or 2.3.1**
  (2.3.1 is the current test baseline).
- [FreeRTOS-Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel) **V11.2.0**
  including the community-supported RP2350 port.
- [LVGL](https://github.com/lvgl/lvgl) **v9.3.0**.

Clone the dependencies outside this repository:

```bash
git clone --branch 2.3.1 --recurse-submodules --shallow-submodules \
  https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk

git clone --branch V11.2.0 \
  https://github.com/FreeRTOS/FreeRTOS-Kernel.git ~/FreeRTOS-Kernel
git -C ~/FreeRTOS-Kernel submodule update --init --depth 1 \
  portable/ThirdParty/Community-Supported-Ports

git clone --branch v9.3.0 https://github.com/lvgl/lvgl.git ~/lvgl
```

If you use the Pico SDK's `pico_setup.sh`/VS Code extension, make sure
`arm-none-eabi-gcc` is on `PATH`.

### Configure and compile

```bash
cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH="$HOME/pico-sdk" \
  -DFREERTOS_KERNEL_PATH="$HOME/FreeRTOS-Kernel" \
  -DLVGL_PATH="$HOME/lvgl"

cmake --build build -j 8
```

The result is `build/flightcard.uf2` (plus `.elf` and `.bin`). The first
configure may build `picotool` from source if it is not installed; that is
normal.

### Build-time WiFi and location

The firmware can use a compiled-in WiFi network and monitoring position as
its **first-boot defaults**. These are optional: you can also provision the
board over USB after flashing (see below).

| CMake / environment variable | Meaning |
|---|---|
| `FC_WIFI_SSID` | 2.4 GHz WiFi network name (≤ 32 bytes). |
| `FC_WIFI_PASSWORD` | WiFi password (≤ 64 bytes). |
| `FC_HOME_LAT` | Monitoring latitude in decimal degrees, −90 to 90. |
| `FC_HOME_LON` | Monitoring longitude in decimal degrees, −180 to 180. |
| `FC_TIMEZONE` | `UTC`, `GMT`, `Europe/London`, `Europe/Dublin`, `Europe/Lisbon`, or a fixed offset such as `UTC+01:00`. Default `UTC`. |
| `FC_RADIUS_KM` | Initial search radius, 0.1–500. Default `12`. |
| `FC_MAX_ALT_FT` | Initial altitude ceiling in feet, 0–60000. Default `8000`. |

Set `FC_HOME_LAT` and `FC_HOME_LON` together, or leave both unset. Values are
copied into `build/generated/fc_build_config.h` at configure time. Use them
only as defaults for a blank board: a board that has already saved a config
keeps the stored values, and `set ...` + `save` in the CLI overrides them.

The recommended way to pass credentials is with environment variables, which
are not written to `CMakeCache.txt`:

```bash
export FC_WIFI_SSID='MyNetwork'
read -rsp 'WiFi password: ' FC_WIFI_PASSWORD; echo; export FC_WIFI_PASSWORD
export FC_HOME_LAT=40.7128          # replace with your latitude
export FC_HOME_LON=-74.0060         # replace with your longitude
export FC_TIMEZONE=UTC

cmake -S . -B build -G Ninja \
  -DPICO_SDK_PATH="$HOME/pico-sdk" \
  -DFREERTOS_KERNEL_PATH="$HOME/FreeRTOS-Kernel" \
  -DLVGL_PATH="$HOME/lvgl"
cmake --build build -j 8
unset FC_WIFI_PASSWORD
```

You can also pass the same names with `-DFC_...=value`, but those values are
stored in plain text in `build/CMakeCache.txt` and may appear in your shell
history.

> **Keep build artefacts private.** The generated header and the compiled
> UF2 contain the credentials and location in plain text. `build/` is
> git-ignored, but do not publish, upload or archive a build directory or UF2
> made with real credentials. Delete `build/` before sharing a checkout.

The firmware redacts the WiFi password in its `show` CLI output. It never
prints credentials or the configured location to the boot log.

## Flash

1. Disconnect the Pico.
2. Hold the **BOOTSEL** button while plugging in USB.
3. A mass-storage drive appears (`RPI-RP2` on RP2350 boards).
4. Copy `build/flightcard.uf2` to that drive. The board reboots into the
   firmware automatically.

With `picotool` installed, the same thing is:

```bash
picotool load -x build/flightcard.uf2
```

The firmware image is placed below the two flash sectors used for
configuration, so reflashing the application preserves provisioned settings.

## Provision over USB

Connect the board normally (not in BOOTSEL mode) and open the USB CDC serial
port, for example:

```bash
screen /dev/ttyACM0 115200
```

Then use the built-in CLI:

```text
help
set wifi <ssid> <password>
set home <latitude> <longitude>
set radius 12
set ceiling 8000
set poll 10
set tz UTC
set palette auto
save
reboot
```

Useful commands:

| Command | Effect |
|---|---|
| `show` | Print the current config (password redacted) and network state. |
| `tasks` | Print FreeRTOS task states and the async-lock holder. |
| `set ssid <name>` / `set password <password>` | Set fields containing spaces without splitting them. |
| `set home <lat> <lon>` | Set the monitoring position. |
| `set radius <km>` / `set ceiling <ft>` | Set filter limits. |
| `set poll <seconds>` | Feed refresh interval (1–3600). |
| `set tz <name>` | `UTC`, `GMT`, `Europe/London`, `Europe/Dublin`, `Europe/Lisbon`, or a fixed offset. |
| `set palette <auto\|cabin\|amber>` | `auto` switches on ambient light if a BH1750 is fitted. |
| `set brightness <0..1>` | Fixed backlight level. |
| `set auto_brightness <0\|1>` | Use the BH1750 for brightness. |
| `set night <lux>` | Light threshold for the auto palette. |
| `set wifi_power_save <0\|1>` | Enable CYW43 power save on a marginal supply. |
| `set demo <0\|1>` | Demo cards, no WiFi or feed. |
| `save` | Commit the config to flash. |
| `reboot` | Restart the firmware. |

For a first boot with no WiFi, the helper script prompts for the SSID and
password without putting the password in shell history:

```bash
python3 tools/provision.py --port /dev/ttyACM0 \
  --lat 40.7128 --lon -74.0060 --tz UTC
```

Use `python3 tools/provision.py --show` to read the current config without
changing it.

### Demo mode

To test the display with no network:

```text
set demo 1
save
reboot
```

The clock shows a fixed demo time and five demo cards cycle. Return to live
mode with `set demo 0`, `save`, `reboot`.

## Host tests

The portable logic (parsing, ranking, timezones, config, CLI, formatting,
brightness policy, demo cards) has host unit tests. They need no Pico SDK:

```bash
make -C tests run
```

The test build regenerates `assets/generated/` from the checked-in fonts,
sprites, type table and certificates. A healthy run ends with
`126 checks, 0 failures`. See [`tests/README.md`](tests/README.md) for the
optional LVGL render probe.

## How it works

- **Display task (core 0):** ST7789 init/rotation, SPI DMA flush, LVGL clock
  and card, BH1750 sampling, optional RTC, backlight PWM, USB CLI polling and
  the hardware watchdog.
- **Network task (core 1):** CYW43 WiFi, NTP, bounded DNS with cache, TLS 1.2
  with certificate verification, the adsb.fi v3 feed, ranking, and adsbdb
  type/route enrichment. Cards are published through a length-1 queue.
- **Supervisor (core 0):** checks task heartbeats and reports heap/stack
  high-water marks.

Asset C files are generated at build time by `tools/convert_assets.py` from
`assets/fonts/*.bin`, `assets/sprites/*.bmp`, `assets/types.json` and the
certificate PEMs. Nothing generated is committed.

## Troubleshooting

- **No WiFi / join timeout:** the Pico 2 W is 2.4 GHz only. Check the SSID is
  a 2.4 GHz network and that the password is correct. `show` reports the WiFi
  state; a marginal 5 V supply can also cause association failures.
- **Display shifted or clipped:** the driver assumes the PIM715's 320 × 240
  landscape panel with offsets of zero. Check `ST7789_X_OFFSET` /
  `ST7789_Y_OFFSET` and `ST7789_ROTATION_MADCTL` in `src/display.c` if a panel
  revision differs.
- **Torn or truncated edge:** keep the SPI shifter-drain wait in the DMA IRQ.
  If it is a signal-integrity problem, lower `SPI_HZ` from 40 MHz to 31.25 or
  25 MHz.
- **Isolating display problems:** configure with `-DFC_DISPLAY_DMA=OFF` for a
  blocking-SPI build. `-DFC_SMP=OFF` builds a single-core variant, which is
  experimental; the SMP build is the supported path.
- **Sensors not detected:** verify I2C0 on GPIO 4/5, 3.3 V power and the
  addresses above. The firmware logs what it finds at boot.
- **Credentials changed but the board still uses the old ones:** saved config
  in flash wins over build defaults. Use the CLI (`set ...`, `save`,
  `reboot`) or erase the config sectors.

## Data and attribution

- ADS-B data from [adsb.fi](https://adsb.fi), for personal, non-commercial
  use.
- Aircraft type and route enrichment from [adsbdb](https://adsbdb.com).
- B612 and B612 Mono fonts by the B612 Project Authors, licensed under the
  SIL Open Font License 1.1; see
  [`assets/fonts/OFL.txt`](assets/fonts/OFL.txt).

## License

MIT. See [`LICENSE`](LICENSE).
