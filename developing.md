# HomePlate Development Guide

This guide covers building, flashing, and debugging HomePlate using [PlatformIO](https://platformio.org/). For general installation and setup, see the [README](README.md) and [Setup Guide](setup.md).

## Building

Build and test in Docker; the image pins [PlatformIO](https://platformio.org/) to the same version as CI:

```shell
docker build -t homeplate-grayscale-dev .
sh scripts/container.sh pio run -e inkplate10v2
```

The wrapper mounts the repository's common parent so Git worktrees resolve correctly,
and shares the `homeplate-platformio` Docker volume for toolchains and downloaded
packages. Project dependencies and build tools are installed inside the container.
The wrapper also passes the host's Git version label into the build, including
when the host uses a newer worktree format than the container's Git supports.
Invoke it from the selected worktree. The PlatformIO examples below show the
commands to pass to this wrapper; USB flashing additionally needs access to the
serial device from the container host.

Build for your board variant. You must always specify `-e <board>`:

```shell
pio run -e inkplate5       # Inkplate 5
pio run -e inkplate5v2     # Inkplate 5v2
pio run -e inkplate10      # Inkplate 10 (original with touchpads)
pio run -e inkplate10v2    # Inkplate 10v2 (without touchpads)
pio run -e inkplate6       # Inkplate 6 (original with touchpads)
pio run -e inkplate6v2     # Inkplate 6v2 (without touchpads)
pio run -e inkplate6plus   # Inkplate 6 Plus
pio run -e inkplate6plusv2 # Inkplate 6 Plus v2
pio run -e inkplate6flick  # Inkplate 6 Flick
pio run -e inkplate6color  # Inkplate 6 COLOR (7-color ACeP panel)
pio run -e inkplate13spectra # Inkplate 13 Spectra (13.3" 6-color Spectra panel, ESP32-S3)
```

> **Note:** Running `pio run` without `-e` builds all supported Inkplate board variants (the special-purpose `ota`, `debug`, `vcom`, `waveform_eeprom`, and `native` envs are excluded from the default set and must be invoked explicitly with `-e <env>`). `pio run` only compiles — it will not upload to a connected device. To flash, see the next section.

## Flashing via USB

The first flash must be done over USB. Connect your Inkplate via USB and run:

```shell
pio run -e inkplate10 -t upload -t monitor  # build, flash, and open serial monitor
```

Replace `inkplate10` with your board variant. The `-t upload -t monitor` flags chain the upload and serial monitor steps so they run together in one command. Drop `-t monitor` if you don't want to attach the serial monitor afterward.

## Flashing via OTA (PlatformIO)

After the initial USB flash, you can update over WiFi using PlatformIO OTA. First, set your board in the `[user]` section of `platformio.ini` (see [Selecting your board for special-purpose envs](#selecting-your-board-for-special-purpose-envs) below) and edit `[env:ota]` to set your device hostname/IP, then run:

```shell
pio run -e ota
```

> **Note:** OTA must be enabled on the device (see the **Enable OTA** setting in [setup.md](setup.md#display--ota)), and the device must be awake (not sleeping) when you initiate the OTA flash. With [Always On](setup.md#always-on) enabled the device is always reachable, so there is no wake window to catch.

## Selecting your board for special-purpose envs

The default board envs (`[env:inkplate5]`, `[env:inkplate10]`, etc.) are self-contained and need no configuration — just pick the one matching your hardware: `pio run -e inkplate10 -t upload -t monitor`.

The **special-purpose envs** — `debug`, `ota`, `vcom`, and `waveform_eeprom` — are board-agnostic and read their target board from a `[user]` section near the top of [platformio.ini](platformio.ini). If you only ever build the regular `inkplate*` envs, you can ignore this section entirely. If you want to use any of the special-purpose envs, set `board_flag` to match your hardware:

```ini
[user]
board_flag = -DARDUINO_INKPLATE10
```

`board_flag` values for each variant:

| Board               | `board_flag`                   |
|---------------------|--------------------------------|
| `inkplate5`         | `-DARDUINO_INKPLATE5`          |
| `inkplate5v2`       | `-DARDUINO_INKPLATE5V2`        |
| `inkplate6`         | `-DARDUINO_INKPLATE6`          |
| `inkplate6v2`       | `-DARDUINO_INKPLATE6V2`        |
| `inkplate6plus`     | `-DARDUINO_INKPLATE6PLUS`      |
| `inkplate6plusv2`   | `-DARDUINO_INKPLATE6PLUSV2`    |
| `inkplate6flick`    | `-DARDUINO_INKPLATE6FLICK`     |
| `inkplate10`        | `-DARDUINO_INKPLATE10`         |
| `inkplate10v2`      | `-DARDUINO_INKPLATE10V2`       |
| `inkplate6color`    | `-DARDUINO_INKPLATECOLOR`      |
| `inkplate13spectra` | `-DARDUINO_INKPLATE13SPECTRA`  |

This setting only affects the four special-purpose envs above. The default board envs and CI builds are unaffected.

> **Note:** The special-purpose envs assume the classic-ESP32 boards (`board = esp32dev`). The Inkplate 13 Spectra is ESP32-S3 based, so to use a special-purpose env with it you must also copy the `board` and `board_build.partitions` lines from `[env:inkplate13spectra]` into that env and change its build flags to use `${inkplate_defaults.build_flags_common}` (the classic envs' `-mfix-esp32-psram-cache-issue` flag does not apply to the S3).

## Serial Monitoring

To monitor serial output without re-flashing:

```shell
pio device monitor
```

## Updating PlatformIO & Dependencies

After pulling new changes, update your PlatformIO environment:

```shell
git pull
pio upgrade
pio pkg update
pio run --target clean
```

## Resetting All Settings

To clear all saved settings and start fresh, erase the flash before re-flashing:

```shell
pio run -e <board> --target erase
pio run -e <board>
```

> **Tip:** A first-time install via the [Web Installer](https://lanrat.github.io/homeplate/) also erases all settings.

## Debugging

### Touchpad Sensitivity

On some devices, the touchpads can be overly sensitive. This can cause lots of phantom touch events preventing the HomePlate from going into sleep and using up a lot of power.

Sometimes running `pio run --target=clean` can resolve this before you build & flash the firmware.

The touchpad sensitivity is set in hardware by resistors, but the touch sensors are calibrated on bootup when the device first gets power. I have found that USB power can mess with this calibration. If you are using battery power, restarting the HomePlate (by using the power switch on the side of the PCB) without USB power attached is enough to fix the sensitivity.

Alternatively, the touchpads can be completely disabled by adding `#define TOUCHPAD_ENABLE false` in a `src/config.h` file (this is a compile-time only setting and cannot be changed via the WiFi portal). Touchpads are automatically disabled when building for boards without touchpads.

### Waveform

If you get the following error while booting your Inkplate, run the [Inkplate_Wavefrom_EEPROM_Programming](https://github.com/SolderedElectronics/Inkplate-Arduino-library/tree/master/examples/Inkplate10/Diagnostics/Inkplate10_Wavefrom_EEPROM_Programming) example to update your Inkplate's waveform.

```text
Waveform load failed! Upload new waveform in EEPROM. Using default waveform.
```

Older Inkplates don't appear to ship with an updated waveform. I found waveform 5 looks the best for mine.

## Tests

Run the existing unit tests, the grayscale PNG suite, and its sanitizer build in the container:

```shell
sh scripts/container.sh pio test -e native -e native_png -e native_png_sanitize
```

`native_png` downloads only the portable pngle/miniz sources from the exact Inkplate
revision in `platformio.ini`. It compares the shortcut with the original pngle
decoder and exercises packing, every PNG filter, split IDAT chunks, invalid data,
fallback, cancellation, and allocation failures. `native_png_sanitize` runs the
same suite with address and undefined-behavior sanitizers. Generated fixtures and
dependency sources stay under `.pio/`.

## Grayscale PNG decoder

The Inkplate 10v2 automatically uses a row-based shortcut for opaque grayscale
PNGs (PNG color type 0) with 1-, 2-, 4-, or 8-bit samples, no interlacing, and
dithering off. The image must fit within the display in rotation 0. Both centered
and top-left placement work, including odd widths and offsets.

RGB, palette, grayscale with alpha, `tRNS` transparency, animated, interlaced,
16-bit, oversized, and rotated images use the existing Inkplate decoder. Other
boards also keep that decoder. Dither configuration and HTTP/MQTT overrides still
determine whether the shortcut is eligible. No new setting is required.

The shortcut reuses the Inkplate library's miniz inflater and keeps two original
scanlines for PNG filtering. It writes two output pixels per framebuffer byte,
without expanding samples into RGB or calling `drawPixel()` per pixel. Input PNG
chunks retain CRC checking, and the compressed stream retains Adler checking.
Malformed eligible images fail the render; they are not retried through fallback.
Allocation failure falls back before any pixels are written.

Output matches the current non-dithered decoder: 1-bit samples map to 0/7, 2-bit
samples to 0/2/5/7, 4-bit samples use `sample >> 1`, and 8-bit samples use
`sample >> 5`. Gamma metadata retains the current decoder's treatment; this
shortcut does not introduce color management. Export an actual grayscale PNG to
use it: an RGB PNG whose channels happen to be equal still follows the general
decoder.

### Measuring PNG performance

Run the portable full-screen comparison inside Docker:

```shell
sh scripts/container.sh python3 scripts/benchmark_png_native.py
```

This generates twelve 1200×825 fixtures: dashboard patterns, gradients, and
detailed patterns at each supported bit depth. It alternates the decoder order,
discards two warmups, measures twenty runs per decoder, and checks the entire
framebuffer after every decode. CSV output includes median/minimum/maximum times,
speedup, compressed size, and shortcut workspace bytes. The original pngle path
uses a reference pixel writer in this container benchmark; these results are not
ESP32 timings or measurements of panel refresh speed.

For measurements on the Inkplate 10v2, build the separate benchmark application:

```shell
sh scripts/container.sh pio run -e benchmark_grayscale_png
```

The binary is `.pio/build/benchmark_grayscale_png/firmware.bin`. Flash it using
your normal firmware flashing procedure and capture serial output at 115200 baud.
It runs the same comparisons against the actual `display.image.drawPngFromBuffer()`
implementation, with compressed input and framebuffers in PSRAM. It reports the
minimum free heap observed as well as workspace size. The benchmark does not
connect to WiFi or run the normal dashboard application; restore the normal
firmware after testing.

To also time decoding plus full panel refresh and the normal 250 ms settling
delay, add `-DHOMEPLATE_BENCHMARK_REFRESH` to the benchmark environment's build
flags before building. This performs a full refresh for each decoder and fixture.
The benchmark environment is excluded from default builds and release artifacts.

Hardware timings and visual confirmation on an Inkplate 10v2 remain pending.

## Advanced: Compile-Time Configuration

For advanced users, you can optionally create a `src/config.h` file to set compile-time defaults. These values are used as fallbacks when NVS has no saved value. Any setting configured through the WiFi portal will override compile-time defaults.

### Compile-time only settings

Some settings can only be changed at compile time:

| Setting           | Description                                                             |
|-------------------|-------------------------------------------------------------------------|
| `TOUCHPAD_ENABLE` | Enable/disable touchpads (must be `false` for boards without touchpads) |
| `CONFIG_CPP`      | Enable custom sleep schedules via `config.cpp`                          |

### Variable sleep intervals

If you want your Inkplate to sleep with different intervals based on time of day, create a `config.h` with `#define CONFIG_CPP` and implement `sleepSchedule[]` in `config.cpp` (see `config_example.cpp` for reference).
