"""Generate full-screen fixtures for the optional Inkplate 10 v2 benchmark."""

from pathlib import Path
import runpy

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
fixtures = project / ".pio" / "png-fixtures-benchmark"
runpy.run_path(str(project / "scripts" / "png_fixtures.py"))["generate"](fixtures, True)
env.Append(CPPPATH=[str(fixtures)])
