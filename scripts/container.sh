#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
git_common=$(git -C "$project_dir" rev-parse --path-format=absolute --git-common-dir)
workspace_dir=$(dirname -- "$git_common")
project_version=$(git -C "$project_dir" describe --tags --always --dirty)

exec docker run --rm \
    --mount "type=bind,source=$workspace_dir,target=$workspace_dir" \
    --mount type=volume,source=homeplate-platformio,target=/root/.platformio \
    --env "HOMEPLATE_BUILD_VERSION=$project_version" \
    --workdir "$project_dir" \
    homeplate-grayscale-dev "$@"
