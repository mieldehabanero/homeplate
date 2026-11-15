"""Compare both PNG paths in the container; these are not ESP32 timings."""

import configparser
from pathlib import Path
import runpy
import subprocess

project = Path(__file__).resolve().parent.parent
config = configparser.ConfigParser(interpolation=None)
config.read(project / "platformio.ini")
revision = config["inkplate_defaults"]["inkplate_lib"].rsplit("#", 1)[1]
sources = project / ".pio" / "pngle-native" / revision
if not (sources / "pngle.c").exists():
    subprocess.run(["pio", "test", "-e", "native_png"], cwd=project, check=True)
fixtures = project / ".pio" / "png-fixtures-benchmark"
runpy.run_path(str(project / "scripts" / "png_fixtures.py"))["generate"](fixtures, True)
build = project / ".pio" / "build" / "png-benchmark"
build.mkdir(parents=True, exist_ok=True)
for name in ("miniz", "pngle"):
    subprocess.run(["gcc", "-O2", "-c", str(sources / f"{name}.c"),
                    "-o", str(build / f"{name}.o")], check=True)
subprocess.run([
    "g++", "-O2", "-std=c++11", "-DHOMEPLATE_NATIVE_PNG",
    "-I", str(sources), "-I", str(fixtures), "-I", str(project / "lib/libgrayscalepng/src"),
    str(project / "src_benchmark/main.cpp"), str(project / "lib/libgrayscalepng/src/grayscale_png.cpp"),
    str(build / "miniz.o"), str(build / "pngle.o"), "-lm", "-o", str(build / "benchmark"),
], check=True)
subprocess.run([str(build / "benchmark")], check=True)
