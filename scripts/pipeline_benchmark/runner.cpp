// Copied into ignored historical source exports; never compiled into releases.
#include "homeplate.h"
#include "pipeline_trace.h"
#include "pipeline_settings.h"
#include <esp_timer.h>
#include <graphics/pngle/miniz.h>

namespace pipeline_benchmark {

Sample sample;
static bool active = false;
static TaskHandle_t owner = nullptr;
static unsigned depth[StageCount] = {};

static bool recording() { return active && owner == xTaskGetCurrentTaskHandle(); }
static uint64_t now() { return esp_timer_get_time(); }

Run::Run() {
    sample = Sample();
    for (auto &value : depth) value = 0;
    owner = xTaskGetCurrentTaskHandle();
    sample.started = now();
    active = true;
}

Run::~Run() {
    if (!sample.complete) {
        sample.total = now() - sample.started;
        sample.outcome = stopActivity() ? "cancelled" : "failed";
    }
    active = false;
}

Scope::Scope(Stage value) : stage(value), enabled(recording()), started(0), clearBefore(0) {
    if (enabled) {
        if (depth[stage]++ == 0) started = now();
        clearBefore = sample.elapsed[Clear];
    }
}

Scope::~Scope() {
    if (!enabled || --depth[stage]) return;
    uint64_t elapsed = now() - started;
    // The legacy fallback may clear inside the decode region. Count that once.
    if (stage == Decode) elapsed -= sample.elapsed[Clear] - clearBefore;
    sample.elapsed[stage] += elapsed;
}

void finish(bool rendered) {
    if (!recording()) return;
    sample.total = now() - sample.started;
    sample.outcome = rendered ? "rendered" : "failed";
    sample.complete = true;
}

void notModified() {
    finish(false);
    sample.outcome = "not_modified";
}

void attempt() { if (recording()) ++sample.attempts; }
void headers(uint64_t started, int status) {
    if (!recording()) return;
    sample.elapsed[Headers] += now() - started;
    sample.httpStatus = status;
}
void body(int bytes) { if (recording() && bytes >= 0) sample.bytes = bytes; }
void clearDisplay() {
    Scope scope(Clear);
    if (recording()) ++sample.clearCalls;
    display.clearDisplay();
}
void partialUpdate(bool forced) {
    if (recording()) ++sample.partialCalls;
    display.partialUpdate(forced);
}
void refresh() { Scope scope(Refresh); displayRefresh(); }

void configure() {
    // Runtime overrides only: retain the existing WiFi credentials and NVS.
    plateCfg.mqttHost[0] = '\0';
    plateCfg.enableOta = false;
    plateCfg.alwaysOn = true;
    plateCfg.displayLastUpdateTime = false;
    plateCfg.ditherKernel = 0;
    strlcpy(plateCfg.defaultActivityStr, "HomeAssistant", sizeof(plateCfg.defaultActivityStr));
    strlcpy(plateCfg.imageUrl, PIPELINE_BASE_URL "/grayscale.png", sizeof(plateCfg.imageUrl));
#if PIPELINE_HAS_PROGRESS_SETTING
    plateCfg.showProgressMessages = false;
#endif
}

static void emit(const char *name, unsigned run, int rssi, uint32_t crc) {
    uint64_t accounted = 0;
    for (auto stage : {Download, Progress, Clear, Decode, Overlay, Refresh, Settle})
        accounted += sample.elapsed[stage];
    uint64_t other = sample.total >= accounted ? sample.total - accounted : 0;
    Serial.printf("PIPELINE_SAMPLE,%s,%s,%u,%u,%s,%d,%u,%d,%llu",
        PIPELINE_PROFILE, name, run, unsigned(run < 2), sample.outcome,
        sample.httpStatus, sample.bytes, rssi, (unsigned long long)sample.total);
    for (auto value : sample.elapsed) Serial.printf(",%llu", (unsigned long long)value);
    Serial.printf(",%llu,%u,%u,%u,%08x\n", (unsigned long long)other,
        sample.attempts, sample.clearCalls, sample.partialCalls, crc);
}

static void runCase(const char *name, const char *file, unsigned run) {
    char url[320];
    snprintf(url, sizeof(url), "%s/%s", PIPELINE_BASE_URL, file);
    invalidateImageCache(); // Every observation must include a complete HTTP 200 body.
    setPendingDitherOverride(0);
    int rssi = WiFi.RSSI();
    drawImageFromURL(url);
    // Validation and reporting happen after the measured pipeline has finished.
    uint32_t crc = mz_crc32(MZ_CRC32_INIT, display.DMemory4Bit, E_INK_WIDTH * E_INK_HEIGHT / 2);
    emit(name, run, rssi, crc);
    delay(500);
}

static void run(void *) {
    Serial.printf("PIPELINE_READY,%s,cpu_mhz=%u,runs=20,warmups=2\n",
                  PIPELINE_PROFILE, unsigned(getCpuFrequencyMhz()));
    Serial.println("Send RUN followed by a newline to start.");
    while (true) {
        if (!Serial.available()) { delay(50); continue; }
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "CHECK") {
            // Transport diagnostics happen before warmups and outside all timers.
            std::map<String, String> headers;
            int32_t size = 0;
            int status = 0;
            uint8_t *body = httpGet(PIPELINE_BASE_URL "/grayscale.png", &headers, &size, 5, nullptr, &status);
            Serial.printf("PIPELINE_CHECK,%s,%d,%u\n", PIPELINE_PROFILE, status,
                          body ? unsigned(size) : 0);
            free(body);
            continue;
        }
        if (command != "RUN") continue;
        Serial.println("PIPELINE_HEADER,profile,case,run,warmup,outcome,http_status,bytes,rssi,total_us,download_us,headers_us,body_us,progress_us,clear_us,decode_us,overlay_us,refresh_us,settle_us,other_us,attempts,clear_calls,partial_calls,frame_crc");
        for (unsigned iteration = 0; iteration < 22; ++iteration) {
#if PIPELINE_ORIGINAL
            // Alternate the two encodings to balance server and WiFi warm-up effects.
            if (iteration & 1) {
                runCase("B", "grayscale.png", iteration);
                runCase("A", "color.png", iteration);
            } else {
                runCase("A", "color.png", iteration);
                runCase("B", "grayscale.png", iteration);
            }
#else
            runCase(PIPELINE_CASE, "grayscale.png", iteration);
#endif
        }
        Serial.printf("PIPELINE_DONE,%s\n", PIPELINE_PROFILE);
    }
}

void startTask() { xTaskCreate(run, "PIPELINE_BENCHMARK", 8192, nullptr, 4, nullptr); }

}
