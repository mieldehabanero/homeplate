// Optional benchmark application, separate from the normal firmware activities.
#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grayscale_png.h"
#include "png_fixtures.h"

#ifdef HOMEPLATE_NATIVE_PNG
#include <chrono>
#include "pngle.h"
#else
#include <Inkplate.h>
#include <esp_timer.h>
static Inkplate display(INKPLATE_3BIT);
#endif

static constexpr unsigned width = 1200, height = 825, runs = 20;
static constexpr size_t framebufferSize = width * height / 2;
static uint8_t *framebuffer;
static size_t workspaceBytes;

static void report(const char *format, ...) {
    char line[320];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
#ifdef HOMEPLATE_NATIVE_PNG
    fputs(line, stdout);
#else
    Serial.print(line);
#endif
}

static uint64_t now() {
#ifdef HOMEPLATE_NATIVE_PNG
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    return esp_timer_get_time();
#endif
}

static void *imageMemory(size_t size) {
#ifdef HOMEPLATE_NATIVE_PNG
    return malloc(size);
#else
    return ps_malloc(size);
#endif
}

static void *workspaceMemory(size_t size) {
    workspaceBytes = size;
    return malloc(size);
}

#ifdef HOMEPLATE_NATIVE_PNG
static void referenceDraw(pngle_t *, uint32_t x, uint32_t y, uint32_t, uint32_t, const uint8_t rgba[4]) {
    uint8_t gray = (54u * rgba[0] + 183u * rgba[1] + 19u * rgba[2]) >> 13;
    uint8_t &pixel = framebuffer[y * width / 2 + x / 2];
    pixel = x & 1 ? (pixel & 0xf0) | gray : (pixel & 0x0f) | (gray << 4);
}
#endif

static bool decode(bool fast, uint8_t *png, size_t size) {
    if (fast) {
        grayscale_png::Target target = {framebuffer, framebufferSize, width, height, width / 2, 0, 0};
        grayscale_png::Options options;
        options.allocate = workspaceMemory;
        options.release = free;
        return grayscale_png::decode(png, size, target, options) == grayscale_png::Result::Rendered;
    }
#ifdef HOMEPLATE_NATIVE_PNG
    pngle_t *decoder = pngle_new();
    if (!decoder) return false;
    pngle_set_draw_callback(decoder, referenceDraw);
    int consumed = pngle_feed(decoder, png, size);
    pngle_destroy(decoder);
    return consumed >= 0 && size_t(consumed) == size;
#else
    return display.image.drawPngFromBuffer(png, size, 0, 0, false, false);
#endif
}

static int benchmark() {
    size_t maximumPngSize = 0;
    for (const auto &fixture : pngFixtures)
        if (fixture.size > maximumPngSize) maximumPngSize = fixture.size;
    auto *input = static_cast<uint8_t *>(imageMemory(maximumPngSize));
    auto *reference = static_cast<uint8_t *>(imageMemory(framebufferSize));
    if (!input || !reference || !framebuffer) {
        free(input);
        free(reference);
        report("FAIL: cannot allocate benchmark buffers\n");
        return 1;
    }
#ifdef HOMEPLATE_NATIVE_PNG
    report("Container CPU benchmark; original pngle with a reference pixel writer, not ESP32 timings.\n");
#else
    report("Inkplate 10 v2; input and framebuffers in PSRAM; two warmups, 20 measured runs.\n");
#endif
    report("fixture,depth,png_bytes,legacy_median_us,fast_median_us,speedup,legacy_min_us,legacy_max_us,fast_min_us,fast_max_us,workspace_bytes\n");
    int failed = 0;
    for (const auto &fixture : pngFixtures) {
        memcpy(input, fixture.data, fixture.size);
        memset(framebuffer, 0xff, framebufferSize);
        if (!decode(false, input, fixture.size)) { failed = 1; break; }
        memcpy(reference, framebuffer, framebufferSize);
        uint64_t times[2][runs];
        for (unsigned run = 0; run < runs + 2 && !failed; ++run) {
            for (unsigned order = 0; order < 2; ++order) {
                unsigned backend = (order + run) % 2;
                memset(framebuffer, 0xff, framebufferSize);
                uint64_t start = now();
                bool good = decode(backend == 1, input, fixture.size);
                uint64_t elapsed = now() - start;
                if (!good || memcmp(reference, framebuffer, framebufferSize)) {
                    report("FAIL: %s backend=%u decode=%d framebuffer mismatch\n", fixture.name, backend, good);
                    failed = 1;
                    break;
                }
                if (run >= 2) times[backend][run - 2] = elapsed;
            }
        }
        if (failed) break;
        for (auto &backend : times) std::sort(backend, backend + runs);
        uint64_t legacy = (times[0][9] + times[0][10]) / 2;
        uint64_t fast = (times[1][9] + times[1][10]) / 2;
        report("%s,%u,%u,%llu,%llu,%.2f,%llu,%llu,%llu,%llu,%u\n", fixture.name,
               fixture.depth, unsigned(fixture.size), (unsigned long long)legacy,
               (unsigned long long)fast, double(legacy) / double(fast ? fast : 1),
               (unsigned long long)times[0][0], (unsigned long long)times[0][runs - 1],
               (unsigned long long)times[1][0], (unsigned long long)times[1][runs - 1],
               unsigned(workspaceBytes));
#if defined(HOMEPLATE_BENCHMARK_REFRESH) && !defined(HOMEPLATE_NATIVE_PNG)
        for (bool fastBackend : {false, true}) {
            memset(framebuffer, 0xff, framebufferSize);
            uint64_t start = now();
            bool good = decode(fastBackend, input, fixture.size);
            if (good) { display.display(); delay(250); }
            report("refresh,%s,%s,%llu,decode_ok=%d\n", fixture.name,
                   fastBackend ? "fast" : "legacy", (unsigned long long)(now() - start), good);
        }
#endif
    }
    free(input);
    free(reference);
    report(failed ? "BENCHMARK FAILED\n" : "BENCHMARK PASSED: every framebuffer matched\n");
    return failed;
}

#ifdef HOMEPLATE_NATIVE_PNG
int main() {
    framebuffer = static_cast<uint8_t *>(malloc(framebufferSize));
    int result = benchmark();
    free(framebuffer);
    return result;
}
#else
void setup() {
    Serial.begin(115200);
    delay(1000);
    display.begin();
    framebuffer = display.DMemory4Bit;
    benchmark();
    report("Minimum free heap observed: %u bytes\n", ESP.getMinFreeHeap());
}
void loop() { delay(1000); }
#endif
