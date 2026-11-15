#!/bin/sh
# Git export uses the host Git, as does container.sh's version resolution.
set -eu
cd "$(dirname "$0")/.."
mkdir -p .pio/pipeline/archives
for revision in 7689bc8 b140c23 db51faf 64ab314; do
    git archive --format=tar --output=".pio/pipeline/archives/$revision.tar" "$revision"
done
exec sh scripts/container.sh python3 scripts/benchmark_pipeline.py prepare "$@"
