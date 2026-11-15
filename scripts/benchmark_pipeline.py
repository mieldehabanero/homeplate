"""Prepare real historical firmware, serve unchanged PNGs, and summarize UART captures."""

import argparse
import configparser
import csv
import hashlib
import http.server
import io
import json
import math
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import tarfile
from urllib.parse import urlsplit
import zlib


PROJECT = Path(__file__).resolve().parent.parent
ROOT = PROJECT / ".pio/pipeline"
SUPPORT = PROJECT / "scripts/pipeline_benchmark"
PROFILES = {
    "original": ("7689bc8", ("A", "B")),
    "fastpath": ("b140c23", ("C",)),
    "quiet": ("db51faf", ("D",)),
    "no_clear": ("64ab314", ("E",)),
}


def png_info(path, expected_depth, expected_type):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path.name}: not a PNG")
    ihdr = struct.unpack(">IIBBBBB", data[16:29])
    if ihdr != (1200, 825, expected_depth, expected_type, 0, 0, 0):
        raise ValueError(f"{path.name}: unexpected IHDR {ihdr}")
    offset, ended = 8, False
    while offset + 12 <= len(data):
        size = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        chunk = data[offset + 4:offset + 8 + size]
        end = offset + size + 12
        if end > len(data) or zlib.crc32(chunk) != int.from_bytes(data[end - 4:end], "big"):
            raise ValueError(f"{path.name}: invalid PNG chunk")
        if kind in (b"tRNS", b"acTL"):
            raise ValueError(f"{path.name}: transparent or animated input")
        offset = end
        if kind == b"IEND":
            ended = offset == len(data) and size == 0
            break
    if not ended:
        raise ValueError(f"{path.name}: incomplete PNG")
    return dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                width=1200, height=825, depth=expected_depth, color_type=expected_type)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"Historical instrumentation anchor is not unique: {old[:90]!r}")
    return text.replace(old, new, 1)


def instrument(destination):
    path = destination / "src/image.cpp"
    image = '#include "pipeline_trace.h"\n' + path.read_text()
    image = replace_once(image, 'bool drawImageFromURL(const char *url) {',
                         'bool drawImageFromURL(const char *url) {\n    pipeline_benchmark::Run benchmarkRun;')
    image = replace_once(image, 'void displayStatusMessage(const char *format, ...)\n{',
                         'void displayStatusMessage(const char *format, ...)\n{\n    pipeline_benchmark::Scope benchmarkProgress(pipeline_benchmark::Progress);')
    if 'void displayProgressMessage(' in image:
        image = replace_once(image, 'void displayProgressMessage(const char *format, ...)\n{',
                             'void displayProgressMessage(const char *format, ...)\n{\n    pipeline_benchmark::Scope benchmarkProgress(pipeline_benchmark::Progress);')
    image = replace_once(image, 'void displayStats()\n{',
                         'void displayStats()\n{\n    pipeline_benchmark::Scope benchmarkOverlay(pipeline_benchmark::Overlay);')
    image = replace_once(image, '        // display the image\n        displayStart();',
                         '        // display the image\n        {\n        pipeline_benchmark::Scope benchmarkDecode(pipeline_benchmark::Decode);\n        displayStart();')
    image = replace_once(image, '        displayEnd();\n    }\n    \n    if (good)',
                         '        displayEnd();\n        }\n    }\n    \n    if (good)')
    image = image.replace('display.clearDisplay();', 'pipeline_benchmark::clearDisplay();')
    image = image.replace('display.partialUpdate(sleepBoot);', 'pipeline_benchmark::partialUpdate(sleepBoot);')
    image = replace_once(image, '    displayRefresh();', '    pipeline_benchmark::refresh();')
    image = replace_once(image, '    vTaskDelay(0.25 * SECOND/portTICK_PERIOD_MS);',
                         '    {\n        pipeline_benchmark::Scope benchmarkSettle(pipeline_benchmark::Settle);\n        vTaskDelay(0.25 * SECOND/portTICK_PERIOD_MS);\n    }')
    image = replace_once(image, '    Serial.println("[IMAGE] displaying done.");',
                         '    pipeline_benchmark::finish(good);\n    Serial.println("[IMAGE] displaying done.");')
    image = replace_once(image, '            Serial.println("[IMAGE] unchanged (304), skipping render");',
                         '            pipeline_benchmark::notModified();\n            Serial.println("[IMAGE] unchanged (304), skipping render");')
    path.write_text(image)

    path = destination / "src/network.cpp"
    network = '#include "pipeline_trace.h"\n#include <esp_timer.h>\n' + path.read_text()
    signature = 'uint8_t* httpGetRetry('
    start = network.index(signature)
    brace = network.index('{', start)
    network = network[:brace + 1] + '\n    pipeline_benchmark::Scope benchmarkDownload(pipeline_benchmark::Download);' + network[brace + 1:]
    network = replace_once(network, '        ret = httpGet(url,',
                           '        pipeline_benchmark::attempt();\n        ret = httpGet(url,')
    start = network.index('uint8_t* httpGet(')
    before, body = network[:start], network[start:]
    body = replace_once(body, '    http.begin(url);',
                        '    uint64_t benchmarkHeadersStarted = esp_timer_get_time();\n    http.begin(url);')
    body = replace_once(body, '    int httpCode = http.GET();',
                        '    int httpCode = http.GET();\n    pipeline_benchmark::headers(benchmarkHeadersStarted, httpCode);')
    body = replace_once(body, '    int written = http.writeToStream(&bs);',
                        '    int written;\n    {\n        pipeline_benchmark::Scope benchmarkBody(pipeline_benchmark::Body);\n        written = http.writeToStream(&bs);\n    }\n    pipeline_benchmark::body(written);')
    path.write_text(before + body)

    path = destination / "src/main.cpp"
    main = '#include "pipeline_trace.h"\n' + path.read_text()
    main = replace_once(main, '    loadConfig();', '    loadConfig();\n    pipeline_benchmark::configure();')
    main = main.replace('consumePortalSavedFlag()', 'false').replace('consumeForcePortalFlag()', 'false')
    main = replace_once(main, '    startActivity(activityFromString(plateCfg.defaultActivityStr));',
                         '    // Benchmark runner calls the normal image pipeline directly.')
    main = replace_once(main, '    startMonitoringButtonsTask();', '    // No input activity can interrupt a benchmark sample.')
    main = replace_once(main, '    setupTimeAndSyncTask();', '    // Time synchronization is outside this controlled image benchmark.')
    main = replace_once(main, '    sleepTask();', '    // Sleeping is disabled in this temporary benchmark build.')
    main = replace_once(main, '    startActivitiesTask();', '    pipeline_benchmark::startTask();')
    main = replace_once(main, '    lowBatteryCheck();', '    // Battery overlays are outside the measured pipeline.')
    path.write_text(main)


def prepare(args):
    base = urlsplit(args.base_url)
    if base.scheme != "http" or not base.hostname or base.query or base.fragment or base.username or base.password:
        raise ValueError("Use the local HTTP server base URL, without credentials or query parameters")
    url = args.base_url.rstrip("/")
    if len(url) > 200:
        raise ValueError("Server URL is too long")
    images = {"color.png": png_info(ROOT / "images/color.png", 8, 2),
              "grayscale.png": png_info(ROOT / "images/grayscale.png", 4, 0)}
    manifest = dict(images=images, profiles=PROFILES, warmups=2, measured_runs=20,
                    base_url=url, timestamp=False, dither=False, cpu_mhz=240)
    for name, (revision, cases) in PROFILES.items():
        destination = ROOT / "builds" / name
        destination.mkdir(parents=True, exist_ok=True)
        with tarfile.open(ROOT / "archives" / f"{revision}.tar") as archive:
            archive.extractall(destination, filter="data")
        instrument(destination)
        shutil.copyfile(SUPPORT / "trace.h", destination / "src/pipeline_trace.h")
        shutil.copyfile(SUPPORT / "runner.cpp", destination / "src/pipeline_runner.cpp")
        (destination / "src/pipeline_settings.h").write_text(
            '#pragma once\n' +
            f'#define PIPELINE_PROFILE {json.dumps(name)}\n' +
            f'#define PIPELINE_BASE_URL {json.dumps(url)}\n' +
            f'#define PIPELINE_ORIGINAL {int(name == "original")}\n' +
            f'#define PIPELINE_HAS_PROGRESS_SETTING {int(name in ("quiet", "no_clear"))}\n' +
            f'#define PIPELINE_CASE {json.dumps(cases[0])}\n')
        config = configparser.ConfigParser(interpolation=None)
        ini = destination / "platformio.ini"
        config.read(ini)

        def expand(value):
            return re.sub(r"\$\{([^}]+)\}", lambda m: expand(config.get(*m[1].split(".", 1))), value)

        flags = []
        for line in expand(config["env:inkplate10v2"]["build_flags"]).splitlines():
            line = line.strip()
            if not line or line.startswith(";"):
                continue
            if line.startswith("!"):
                if "VERSION" not in line:
                    raise ValueError("Unexpected dynamic build flag")
                continue
            flags.append(line)
        flags.append('-DVERSION=\\"pipeline-benchmark\\"')
        with ini.open("a") as output:
            output.write('\n[env:benchmark_pipeline]\nextends = env:inkplate10v2\nbuild_flags =\n')
            output.writelines(f'  {line}\n' for line in flags)
        print(f"Prepared {name}: {revision}, cases {','.join(cases)}")
    (ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def serve(args):
    images = {name: (ROOT / "images" / name).read_bytes() for name in ("color.png", "grayscale.png")}

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            name = urlsplit(self.path).path.removeprefix("/")
            if name not in images:
                self.send_error(404)
                return
            data = images[name]
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("X-Dither", "none")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(data)
            self.close_connection = True

        def log_message(self, fmt, *values):
            # Do not log arbitrary query strings or request headers.
            print(f"{self.client_address[0]} {urlsplit(self.path).path}", flush=True)

    print(f"Serving two fixed PNGs on port {args.port}; validators disabled", flush=True)
    http.server.ThreadingHTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


def summarize(args):
    manifest = json.loads((ROOT / "manifest.json").read_text())
    hardware = ROOT / "hardware"
    metadata = json.loads((hardware / "run-metadata.json").read_text())
    restored = json.loads((hardware / "restore-status.json").read_text())
    if metadata["manifest"] != manifest:
        raise ValueError("Prepared inputs differ from the recorded hardware run")
    if any(restored.get(key) is not True for key in ("restored", "settings_unchanged", "application_unchanged")):
        raise ValueError("Verify restoration before publishing benchmark results")
    header, rows = None, []
    for log in args.logs:
        for line in log.read_text(errors="replace").splitlines():
            if line.startswith("PIPELINE_HEADER,"):
                candidate = next(csv.reader([line.split(",", 1)[1]]))
                if header is not None and header != candidate:
                    raise ValueError("CSV headers differ between captures")
                header = candidate
            if line.startswith("PIPELINE_SAMPLE,"):
                if header is None:
                    raise ValueError("Missing CSV header")
                values = next(csv.reader([line.split(",", 1)[1]]))
                if len(values) != len(header):
                    raise ValueError("Incomplete serial sample")
                row = dict(zip(header, values))
                if row["warmup"] == "0":
                    rows.append(row)
    output = ROOT / "results"
    output.mkdir(exist_ok=True)
    groups = {key: [] for key in "ABCDE"}
    for row in rows:
        key = row["case"]
        if key not in groups or row["profile"] != next(name for name, (_, cases) in PROFILES.items() if key in cases):
            raise ValueError("Unexpected benchmark case or profile")
        stages = ("download", "progress", "clear", "decode", "overlay", "refresh", "settle", "other")
        if any(int(row[name]) < 0 for name in header if name.endswith("_us")):
            raise ValueError(f"Case {key}: negative timing")
        if sum(int(row[f"{stage}_us"]) for stage in stages) != int(row["total_us"]):
            raise ValueError(f"Case {key}: stage timings do not add up to total")
        if int(row["headers_us"]) + int(row["body_us"]) > int(row["download_us"]):
            raise ValueError(f"Case {key}: HTTP subspans exceed download duration")
        expected = manifest["images"]["color.png" if key == "A" else "grayscale.png"]["bytes"]
        if row["outcome"] != "rendered" or int(row["http_status"]) != 200 or int(row["attempts"]) != 1:
            raise ValueError(f"Case {key}: failed, cached, or retried sample; inspect the private capture")
        if int(row["bytes"]) != expected:
            raise ValueError(f"Case {key}: unexpected response length")
        if int(row["clear_calls"]) != (0 if key == "E" else 1):
            raise ValueError(f"Case {key}: unexpected clear count")
        if int(row["partial_calls"]) != (4 if key in "ABC" else 0):
            raise ValueError(f"Case {key}: unexpected progress update count")
        groups[key].append(row)
    for key, samples in groups.items():
        if len(samples) != 20 or {int(row["run"]) for row in samples} != set(range(2, 22)):
            raise ValueError(f"Case {key}: expected 20 unique measured samples, got {len(samples)}")
        if len({row["frame_crc"] for row in samples}) != 1:
            raise ValueError(f"Case {key}: output pixels changed during the run")
    if len({row["frame_crc"] for key in "BCDE" for row in groups[key]}) != 1:
        raise ValueError("Grayscale outputs differ across historical versions")
    for row in rows:
        row["non_download_us"] = int(row["total_us"]) - int(row["download_us"])
    header = header + ["non_download_us"]
    with (output / "samples.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=header, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    timing = [name for name in header if name.endswith("_us")]
    summary = []
    for key, samples in groups.items():
        for metric in timing:
            values = sorted(int(row[metric]) for row in samples)
            summary.append(dict(case=key, metric=metric, median_us=statistics.median(values),
                                min_us=values[0], max_us=values[-1], p95_us=values[math.ceil(.95 * len(values)) - 1]))
    with (output / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=summary[0].keys(), lineterminator="\n")
        writer.writeheader()
        writer.writerows(summary)
    totals = {row["case"]: row["median_us"] for row in summary if row["metric"] == "total_us"}
    non_download = {row["case"]: row["median_us"] for row in summary if row["metric"] == "non_download_us"}
    deltas = [dict(comparison=f"{before}->{after}", saved_ms=(totals[before] - totals[after]) / 1000,
                   saved_percent=100 * (totals[before] - totals[after]) / totals[before],
                   non_download_saved_ms=(non_download[before] - non_download[after]) / 1000)
              for before, after in (("A", "B"), ("B", "C"), ("C", "D"), ("D", "E"), ("A", "E"))]
    (output / "comparisons.json").write_text(json.dumps(deltas, indent=2) + "\n")
    provenance = {key: metadata[key] for key in ("firmware", "instrumentation_sha256", "started_utc", "serial_transport", "baud")}
    provenance["conditions"] = {key: value for key, value in manifest.items() if key != "base_url"}
    provenance["restore_status"] = restored
    (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(json.dumps(dict(median_total_us=totals, comparisons=deltas), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    prepare_parser = commands.add_parser("prepare")
    prepare_parser.add_argument("--base-url", required=True)
    server_parser = commands.add_parser("serve")
    server_parser.add_argument("--port", type=int, default=18080)
    summary_parser = commands.add_parser("summarize")
    summary_parser.add_argument("logs", type=Path, nargs="+")
    args = parser.parse_args()
    {"prepare": prepare, "serve": serve, "summarize": summarize}[args.command](args)


if __name__ == "__main__":
    main()
