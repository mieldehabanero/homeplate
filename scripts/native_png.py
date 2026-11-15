"""Build the pinned Inkplate library's portable decoder without board drivers."""

from pathlib import Path
from urllib.request import urlopen
import runpy

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
dependency = env.GetProjectConfig().get("inkplate_defaults", "inkplate_lib")
repository, revision = dependency.rsplit("#", 1)
base = repository.removesuffix(".git").replace("github.com", "raw.githubusercontent.com")
cache = project / ".pio" / "pngle-native" / revision
cache.mkdir(parents=True, exist_ok=True)
for name in ("miniz.c", "miniz.h", "pngle.c", "pngle.h"):
    target = cache / name
    if not target.exists():
        with urlopen(f"{base}/{revision}/src/graphics/pngle/{name}", timeout=60) as response:
            contents = response.read()
        target.write_bytes(contents)

env.Append(CPPPATH=[str(cache)])
fixtures = project / ".pio" / "png-fixtures-native"
runpy.run_path(str(project / "scripts" / "png_fixtures.py"))["generate"](fixtures)
env.Append(CPPPATH=[str(fixtures)])
env.BuildSources(str(Path(env.subst("$BUILD_DIR")) / "pngle"), str(cache),
                 src_filter="+<miniz.c> +<pngle.c>")
