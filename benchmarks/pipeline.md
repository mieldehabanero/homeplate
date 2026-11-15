# Download-to-panel benchmark

This hardware benchmark runs the normal image pipeline on an Inkplate 10v2,
with the same timing hooks added to four historical source exports. Generated
sources, firmware, images, recovery backups, and UART captures stay under the
ignored `.pio/pipeline` directory. Release firmware does not include these hooks.

| Case | Source | PNG | Progress messages | Initial framebuffer clear |
|---|---|---|---|---|
| A | `7689bc8` | RGB, 8 bits/channel | On | Always |
| B | `7689bc8` | Grayscale, 4 bits/pixel | On | Always |
| C | `b140c23` | Same grayscale file | On | Always |
| D | `db51faf` | Same grayscale file | Off | Always |
| E | `64ab314` | Same grayscale file | Off | Skipped for this eligible image |

All images are opaque, noninterlaced, and exactly 1200 × 825 pixels. Dithering
and the update timestamp are off. The local HTTP server serves the supplied PNG
bytes unchanged from memory with `Content-Length`, `X-Dither: none`, and no cache
validators. Each observation starts with an invalidated image cache and must
complete one HTTP 200 request without retries.

Each case has two warmups followed by 20 measured observations. A and B alternate
order on each iteration; C, D, and E run in separate firmware installations.
Before each profile, the collector requests an untimed grayscale download from
the device and requires HTTP 200 with the expected byte count. Failed preflight
requests retry for up to five minutes before the controller restores the starting
application. A successful request from the Mac alone cannot verify device access
across separate WiFi networks or VLANs.
Record the RSSI, input and firmware hashes, and all stage durations. Comparison
of separate installations still includes WiFi variation and changes in panel
state. Median differences are measurements under these conditions, rather than
exact universal savings for every request or device.

## Timing boundaries and checks

The total starts on entry to `drawImageFromURL`, before its download message, and
ends after the final full panel refresh, the existing 250 ms settling delay, and
release of the display/I²C locks. It excludes boot, WiFi association, post-render
cleanup, CRC calculation, CSV reporting, and the 500 ms cooldown between runs.
Original application serial logging remains in place in every version.

Top-level spans cover download, progress messages, framebuffer clear, decode,
timestamp overlay, full refresh, settling, and other overhead. HTTP header and
body spans are subdivisions of download, so must not be added to it again.
Nested progress helpers count once, and a fallback clear inside decode is
subtracted from decode. With the timestamp disabled, overlay time is zero.
The two routine image status messages make four driver `partialUpdate()` calls.
The pinned Inkplate driver also promotes a call to a full monochrome update
after its default limit of ten partial passes. That normal behavior remains
enabled and its time belongs to the progress span, so progress latency can vary
even with identical input images. The CSV counter records driver calls.
Download includes client setup, allocation, cleanup, and any retry delays; the
HTTP header span includes connection setup and waiting for response headers.
The derived `non_download_us` column is `total_us - download_us`: progress,
rendering, panel work, and other overhead, including the progress message before
the HTTP request. This helps distinguish firmware changes from WiFi variation;
it does not replace the measured end-to-end total. Differences between medians
of individual spans need not add up to the difference between median totals.

The temporary runner disables MQTT, OTA, NTP, button monitoring, sleep, and low
battery overlays to prevent unrelated work from interrupting samples. It keeps
the normal WiFi connection/reconnection code and actual panel update routines.
Configuration overrides are in RAM; saved settings are not intentionally changed.

The collector requires 20 unique successful samples per case, matching byte
counts, expected clear/partial-update counts, and valid timing accounting. C–E
must print the successful grayscale decoder marker. Framebuffer CRCs must remain
stable within each case, and B–E must produce the same CRC across all versions.

The supplied files differ slightly after conversion to the panel's eight gray
levels: 11,256 of 990,000 pixels (1.14%), at most two levels apart. A→B therefore
measures the supplied encoding/conversion choice, including that quantization
difference. B→E uses exactly the same file. Input metadata:

| File | Bytes | SHA-256 |
|---|---:|---|
| `color.png` | 123,823 | `5163da85e5073740adda4e243271a96388b186514598818707b3049338ca32e6` |
| `grayscale.png` | 30,326 | `ce68ae086fc04bee0296e0f8876af6483ed6573988f5f4287bcfea908e6ae4ce` |

## Measured results

Measured on the connected Inkplate 10v2 on 2026-11-15, with 20 observations per
case after warmups. Times below are seconds and include the final panel refresh
and settling delay.

| Case | Median total | Min–max total | p95 total | Median total minus HTTP |
|---|---:|---:|---:|---:|
| A: original, color | 10.736 | 9.516–111.382 | 31.355 | 8.998 |
| B: original, 4-bit grayscale | 8.375 | 7.281–10.882 | 9.972 | 7.117 |
| C: grayscale fast path | 5.838 | 4.645–10.233 | 7.922 | 4.463 |
| D: progress off | 3.401 | 2.591–9.977 | 6.733 | 2.373 |
| E: eligible clear skipped | 2.596 | 2.504–3.236 | 2.782 | 2.334 |

The observed median total fell by **8.140 s (75.8%)** from A to E. WiFi transfer
times varied substantially; the slowest color request spent 102.384 s in the
download span. All successful observations, including that outlier, remain in the
dataset. A and B alternate, while C–E are separate installations. These cohort
total differences include network variation and cannot all be attributed to the
firmware changes. With HTTP time removed per observation, the median fell from
8.998 s to 2.334 s, a reduction of 6.664 s (74.1%).

The directly timed spans show each change's contribution more clearly:

| Change | Span | Before (ms) | After (ms) | Saved (ms) |
|---|---|---:|---:|---:|
| A→B: supplied grayscale conversion | Decode | 4,783.706 | 2,911.019 | 1,872.687 |
| B→C: grayscale fast path | Decode | 2,911.019 | 259.231 | 2,651.789 |
| C→D: progress off | Progress | 2,096.045 | 0.360 | 2,095.686 |
| D→E: eligible clear skipped | Clear | 35.006 | 0 | 35.006 |

The supplied grayscale file also reduces transfer bytes by 75.5%. Its A→B
comparison includes the small quantization difference described above. The
decoder shortcut removes about 91.1% of B's decode time. Progress painting takes
about 2.1 s in the median, with longer observations when the driver's partial
update limiter promotes a refresh. Skipping the clear saves about **35 ms**;
the 805 ms difference between D and E's median totals is mostly WiFi variation.
The final full refresh and settling still account for about 2.07 s per image.

All grayscale cases produced framebuffer CRC `2b875a39`; the supplied color file
produced `e65550fa`. B–E therefore agree byte for byte in the framebuffer across
both decoder versions and both clear policies. Recorded calls were one clear
and four partial-update calls per image for A–C, one clear and zero partial
calls for D, and neither call for E.

The committed [samples](inkplate10v2-pipeline.csv),
[stage summaries](inkplate10v2-pipeline-summary.csv),
[median comparisons](inkplate10v2-pipeline-comparisons.json), and
[provenance](inkplate10v2-pipeline-provenance.json) retain the timing spread,
RSSI, input/firmware hashes, run conditions, and restoration verification.

## Reproducing the run

Use the repository Docker setup from [the development guide](../developing.md).
Place the two files under `.pio/pipeline/images/`, and prepare the exports with
the LAN address reachable by the device:

```sh
sh scripts/benchmark_pipeline.sh --base-url http://HOST_LAN_IP:18080
```

Build each export inside the development container with PlatformIO environment
`benchmark_pipeline`. The export roots are `.pio/pipeline/builds/original`,
`fastpath`, `quiet`, and `no_clear`. Each produces
`.pio/build/benchmark_pipeline/firmware.bin` under its own root:

```sh
sh scripts/container.sh pio run -d .pio/pipeline/builds/original -e benchmark_pipeline
sh scripts/container.sh pio run -d .pio/pipeline/builds/fastpath -e benchmark_pipeline
sh scripts/container.sh pio run -d .pio/pipeline/builds/quiet -e benchmark_pipeline
sh scripts/container.sh pio run -d .pio/pipeline/builds/no_clear -e benchmark_pipeline
```

Start the image server in a container with port 18080 published on the host's LAN interface:

```sh
python3 scripts/benchmark_pipeline.py serve --port 18080
```

On hosts where Docker cannot open USB devices, the small `serial_bridge.py`
transport runs on the host using an existing pyserial installation. It provides
fixed 115200 baud sockets: 14421 resets into the bootloader, and 14422 boots the
application. Bind it to an interface Docker can reach. Keep these ports on a
trusted network; stop the bridge after the run. Firmware tools and capture
processing run inside Docker.

```sh
python scripts/pipeline_benchmark/serial_bridge.py /dev/cu.usbserial-1440 --bind 0.0.0.0
```

The following commands run inside the repository container using PlatformIO's
Python, which supplies the pinned esptool and pyserial:

```sh
/root/.platformio/penv/bin/python scripts/pipeline_benchmark/hardware.py backup \
  --durable-dir /path/under/mounted/workspace/outside/worktree/recovery
/root/.platformio/penv/bin/python scripts/pipeline_benchmark/hardware.py run
```

When direct host USB tooling is authorized, the same collector can use an
existing host PlatformIO Python and `--port /dev/cu.usbserial-1440
--application-port /dev/cu.usbserial-1440` before the `backup` or `run` subcommand.
The actual run used this arrangement after long reads through the Docker serial
bridge proved unreliable. Firmware transfers use 115200 baud; serial capture
also uses 115200. Builds and result analysis remain in Docker.

The collector requires a fresh validated 4 MiB backup copied outside the
implementation worktree before writing firmware. It identifies the active OTA
slot and writes only that application partition, preserving the bootloader,
partition table, OTA selection, and settings. Afterward it restores the exact
starting application partition, reads it back byte for byte, checks the initial
64 KiB boot/settings region against the backup, and boots the restored app.
Keep the recovery copy until restoration has been verified. An interrupted
process or lost USB connection can still require manual recovery from that copy.
Flash reads use separately digest-checked 64 KiB chunks, retaining the connection
between successful chunks and reconnecting before retrying a failed chunk.

Numeric results are written to `.pio/pipeline/results/`: `samples.csv`,
`summary.csv` (median, minimum, maximum, nearest-rank p95), and
`comparisons.json` (incremental and cumulative median total differences).
Generate these inside Docker after the collector has finished restoring:

```sh
sh scripts/container.sh python3 scripts/benchmark_pipeline.py summarize \
  .pio/pipeline/hardware/original.uart.log .pio/pipeline/hardware/fastpath.uart.log \
  .pio/pipeline/hardware/quiet.uart.log .pio/pipeline/hardware/no_clear.uart.log
```

Private logs and backups can contain saved credentials or URLs and must never
be committed.
