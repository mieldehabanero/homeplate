"""Back up, run all four historical builds, and restore the starting application.

Use Docker and its serial bridge by default. Direct host USB is also supported
when authorized, using an existing PlatformIO Python installation.
"""

import argparse
from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import time
from urllib.request import urlopen
import zlib

import serial


PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / ".pio/pipeline"
HARDWARE = ROOT / "hardware"
TOOL = Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py"
PROFILES = ("original", "fastpath", "quiet", "no_clear")


def layout(flash):
    if len(flash) != 0x400000:
        raise ValueError("Expected a complete 4 MiB backup")
    partitions = []
    for offset in range(0x8000, 0x8c00, 32):
        entry = flash[offset:offset + 32]
        magic = int.from_bytes(entry[:2], "little")
        if magic == 0xebeb:
            if hashlib.md5(flash[0x8000:offset]).digest() != entry[16:32]:
                raise ValueError("Partition table checksum mismatch")
            break
        if magic == 0xffff:
            break
        if magic != 0x50aa:
            raise ValueError("Invalid partition table")
        _, kind, subtype, start, size, label, flags = struct.unpack("<HBBII16sI", entry)
        if start % 4096 or size % 4096 or start + size > len(flash):
            raise ValueError("Invalid partition bounds")
        partitions.append(dict(type=kind, subtype=subtype, offset=start, size=size,
                               label=label.split(b"\0")[0].decode(), flags=flags))
    ota = next((p for p in partitions if p["type"] == 1 and p["subtype"] == 0), None)
    apps = [p for p in partitions if p["type"] == 0 and 0x10 <= p["subtype"] < 0x20]
    valid = []
    if ota:
        for index in range(2):
            entry = flash[ota["offset"] + 4096 * index:ota["offset"] + 4096 * index + 32]
            sequence, state, crc = struct.unpack("<I20xII", entry)
            if sequence not in (0, 0xffffffff) and state not in (3, 4) and crc == zlib.crc32(entry[:4], 0xffffffff):
                valid.append((sequence, state))
    if valid:
        sequence, state = max(valid)
        if state == 1:
            raise ValueError("Active firmware is pending rollback verification")
        active = next(p for p in apps if p["subtype"] == 0x10 + (sequence - 1) % len(apps))
    else:
        active = next((p for p in partitions if p["type"] == 0 and p["subtype"] == 0), None)
        active = active or next(p for p in apps if p["subtype"] == 0x10)
    if active["flags"] & 1 or flash[active["offset"]] != 0xe9:
        raise ValueError("Active application is not readable plaintext")
    return dict(active=active, partitions=partitions)


def tool_environment():
    config = HARDWARE / "esptool.cfg"
    config.write_text("[esptool]\ncustom_reset_sequence = D0|R1|W0.1|D1|R0|W0.05|D0\n")
    return dict(os.environ, ESPTOOL_CFGFILE=str(config))


def command(port, arguments):
    remote = port.startswith("socket:")
    with (HARDWARE / "operations.log").open("ab", buffering=0) as log:
        subprocess.run([sys.executable, str(TOOL), "--chip", "esp32", "--port", port,
                        "--baud", "115200",
                        "--before", "no-reset" if remote else "default-reset",
                        "--after", "no-reset",
                        "--connect-attempts", "3"] + arguments,
                       stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900, env=tool_environment())


def read_flash(port, offset, size, path):
    # Limit each digest-checked transfer on a noisy serial link. Retry a failed
    # chunk after resetting into the loader; never accept partial bytes. Keep
    # the connection for successful chunks so every 64 KiB needn't reconnect.
    os.environ["ESPTOOL_CFGFILE"] = tool_environment()["ESPTOOL_CFGFILE"]
    sys.path.insert(0, str(TOOL.parent))
    from esptool.cmds import attach_flash, read_flash as read_region
    from esptool.targets.esp32 import ESP32ROM
    from esptool.util import FatalError

    data = bytearray()
    esp = None
    try:
        with (HARDWARE / "operations.log").open("a", buffering=1) as log:
            while len(data) < size:
                length = min(0x10000, size - len(data))
                for attempt in range(3):
                    try:
                        with redirect_stdout(log), redirect_stderr(log):
                            if esp is None:
                                esp = ESP32ROM(port, baud=115200)
                                esp.connect(mode="no-reset" if port.startswith("socket:") else "default-reset", attempts=3)
                                esp = esp.run_stub()
                                attach_flash(esp)
                            chunk = read_region(esp, offset + len(data), length, flash_size="4MB", no_progress=True)
                        if len(chunk) != length:
                            raise ValueError("Unexpected flash read length")
                        break
                    except (FatalError, OSError):
                        if esp is not None:
                            esp._port.close()
                            esp = None
                        if attempt == 2:
                            raise
                data.extend(chunk)
                print(f"Read {len(data)}/{size} flash bytes", flush=True)
    finally:
        if esp is not None:
            esp._port.close()
    path.write_bytes(data)


def backup(args):
    path = HARDWARE / "flash-backup.bin"
    if path.exists():
        raise ValueError("Backup already exists; refusing to replace the recovery image")
    read_flash(args.port, 0, 0x400000, path)
    flash = path.read_bytes()
    device = layout(flash)
    active = device["active"]
    (HARDWARE / "layout.json").write_text(json.dumps(device, indent=2) + "\n")
    (HARDWARE / "backup.sha256").write_text(hashlib.sha256(flash).hexdigest() + "\n")
    (HARDWARE / "original-active-app.bin").write_bytes(flash[active["offset"]:active["offset"] + active["size"]])
    args.durable_dir.mkdir(parents=True, exist_ok=False, mode=0o700)
    for name in ("flash-backup.bin", "layout.json", "backup.sha256", "original-active-app.bin"):
        shutil.copyfile(HARDWARE / name, args.durable_dir / name)
    print("Fresh backup validated and copied outside the implementation worktree", flush=True)


def capture(port, profile):
    connection = serial.serial_for_url(port, baudrate=115200, timeout=0.5, do_not_open=True)
    connection.dtr = False
    connection.rts = False
    connection.open()
    if not port.startswith("socket:"):
        connection.rts = True
        time.sleep(0.1)
        connection.rts = False
    path = HARDWARE / f"{profile}.uart.log"
    ready = False
    fastpath = False
    deadline = time.monotonic() + 120
    counts = {}
    with connection, path.open("wb", buffering=0) as output:
        while time.monotonic() < deadline:
            line = connection.readline()
            if not line:
                continue
            output.write(line)
            text = line.decode("utf-8", "replace").strip()
            if text == "[IMAGE] Grayscale PNG decoder: done":
                fastpath = True
            if text.startswith(f"PIPELINE_READY,{profile},"):
                if "cpu_mhz=240," not in text:
                    raise ValueError("Unexpected CPU frequency")
                ready = True
                connection.write(b"CHECK\n")
                deadline = time.monotonic() + 5 * 60
                print(f"Checking HTTP reachability from {profile}", flush=True)
            if text.startswith(f"PIPELINE_CHECK,{profile},"):
                fields = text.split(",")
                manifest = json.loads((ROOT / "manifest.json").read_text())
                expected = manifest["images"]["grayscale.png"]["bytes"]
                if len(fields) != 4 or fields[2] != "200" or int(fields[3]) != expected:
                    print("Device HTTP check failed; retrying before warmups", flush=True)
                    time.sleep(5)
                    connection.write(b"CHECK\n")
                    continue
                connection.write(b"RUN\n")
                deadline = time.monotonic() + 20 * 60
                print(f"Measuring {profile}", flush=True)
            if text.startswith("PIPELINE_SAMPLE,"):
                fields = text.split(",")
                if len(fields) != 24 or fields[1] != profile:
                    raise ValueError("Invalid or unexpected sample record")
                if fields[5] != "rendered" or fields[6] != "200":
                    raise ValueError(f"Benchmark sample failed; private capture: {path.name}")
                if profile != "original" and not fastpath:
                    raise ValueError("Grayscale fast path was not confirmed for this sample")
                fastpath = False
                if fields[4] == "0":
                    case = fields[2]
                    counts[case] = counts.get(case, 0) + 1
                    print(f"Case {case}: {counts[case]}/20", flush=True)
            if text == f"PIPELINE_DONE,{profile}":
                if not ready:
                    raise ValueError("Benchmark finished without the expected ready marker")
                cases = {"original": ("A", "B"), "fastpath": ("C",), "quiet": ("D",), "no_clear": ("E",)}
                if counts != dict.fromkeys(cases[profile], 20):
                    raise ValueError("Benchmark ended without all expected measured samples")
                return path
            if any(marker in text for marker in ("Guru Meditation", "assert failed", "CORRUPT HEAP")):
                raise ValueError(f"Firmware error; inspect private capture: {path.name}")
    raise TimeoutError(f"Timed out waiting for {profile}; see its private UART capture")


def run(args):
    flash = (HARDWARE / "flash-backup.bin").read_bytes()
    expected = (HARDWARE / "backup.sha256").read_text().strip()
    if hashlib.sha256(flash).hexdigest() != expected:
        raise ValueError("Recovery backup checksum mismatch")
    device = layout(flash)
    active = device["active"]
    original = HARDWARE / "original-active-app.bin"
    if original.read_bytes() != flash[active["offset"]:active["offset"] + active["size"]]:
        raise ValueError("Recovery application does not match the full backup")
    manifest = json.loads((ROOT / "manifest.json").read_text())
    for name, info in manifest["images"].items():
        with urlopen(manifest["base_url"] + "/" + name, timeout=10) as response:
            if response.status != 200 or hashlib.sha256(response.read()).hexdigest() != info["sha256"]:
                raise ValueError("Image server content changed")
    prefix = HARDWARE / "before-prefix.bin"
    read_flash(args.port, 0, 0x10000, prefix)
    if prefix.read_bytes() != flash[:0x10000]:
        raise ValueError("Boot/settings region changed since backup; no firmware was written")
    binaries = {}
    metadata = dict(manifest=manifest, firmware={}, started_utc=datetime.now(timezone.utc).isoformat(),
                    serial_transport="socket" if args.port.startswith("socket:") else "direct_usb", baud=115200)
    for profile in PROFILES:
        binary = ROOT / "builds" / profile / ".pio/build/benchmark_pipeline/firmware.bin"
        data = binary.read_bytes()
        if not data or data[0] != 0xe9 or len(data) > active["size"]:
            raise ValueError(f"Invalid or oversized application for {profile}")
        binaries[profile] = binary
        metadata["firmware"][profile] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        source = ROOT / "builds" / profile / "src"
        metadata["firmware"][profile]["source_sha256"] = {
            name: hashlib.sha256((source / name).read_bytes()).hexdigest()
            for name in ("image.cpp", "network.cpp", "main.cpp", "pipeline_runner.cpp", "pipeline_trace.h")
        }
    metadata["instrumentation_sha256"] = {
        name: hashlib.sha256((PROJECT / "scripts/pipeline_benchmark" / name).read_bytes()).hexdigest()
        for name in ("trace.h", "runner.cpp")
    }
    (HARDWARE / "run-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    changed = False
    status = dict(restored=False, settings_unchanged=False, application_unchanged=False)
    (HARDWARE / "restore-status.json").write_text(json.dumps(status, indent=2) + "\n")
    captures = []
    try:
        for profile, binary in binaries.items():
            changed = True  # A failed write can still partially replace the application.
            print(f"Installing application-only benchmark: {profile}", flush=True)
            command(args.port, ["write-flash", "--flash-size", "keep", "--no-progress",
                                hex(active["offset"]), str(binary)])
            captures.append(capture(args.application_port, profile))
    finally:
        if changed:
            print("Restoring the exact starting application", flush=True)
            for attempt in range(2):
                try:
                    command(args.port, ["write-flash", "--flash-size", "keep", "--no-progress",
                                        hex(active["offset"]), str(original)])
                    status["restored"] = True
                    break
                except subprocess.SubprocessError:
                    pass
            try:
                if status["restored"]:
                    try:
                        check = HARDWARE / "restored-app.bin"
                        read_flash(args.port, active["offset"], active["size"], check)
                        status["application_unchanged"] = check.read_bytes() == original.read_bytes()
                    finally:
                        check = HARDWARE / "after-prefix.bin"
                        read_flash(args.port, 0, 0x10000, check)
                        status["settings_unchanged"] = check.read_bytes() == flash[:0x10000]
            finally:
                (HARDWARE / "restore-status.json").write_text(json.dumps(status, indent=2) + "\n")
                # Opening the application endpoint performs a local normal reset.
                with serial.serial_for_url(args.application_port, baudrate=115200, timeout=1) as connection:
                    if not args.application_port.startswith("socket:"):
                        connection.dtr = False
                        connection.rts = True
                        time.sleep(0.1)
                        connection.rts = False
                    with (HARDWARE / "restored-boot.uart.log").open("wb") as output:
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            output.write(connection.read(connection.in_waiting or 1))
            if not all(status.values()):
                raise RuntimeError("Recovery verification failed; preserve backup and inspect private operations.log")
    print("Captures complete; run benchmark_pipeline.py summarize inside Docker", flush=True)


def interrupted(signum, frame):
    raise InterruptedError("Benchmark interrupted; restoring the starting application")


def main():
    os.umask(0o077)
    HARDWARE.mkdir(parents=True, exist_ok=True)
    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="socket://host.docker.internal:14421")
    parser.add_argument("--application-port", default="socket://host.docker.internal:14422")
    commands = parser.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("backup")
    prepare.add_argument("--durable-dir", required=True, type=Path)
    commands.add_parser("run")
    args = parser.parse_args()
    {"backup": backup, "run": run}[args.command](args)


if __name__ == "__main__":
    main()
