"""Resolve VERSION, including host metadata for newer Git worktree formats."""

import os
import subprocess

version = os.environ.get("HOMEPLATE_BUILD_VERSION")
if not version:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--always", "--dirty"], text=True
    ).strip()
print(version.removeprefix("v"))
