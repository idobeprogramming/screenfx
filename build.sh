#!/usr/bin/env sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cmake --preset linux-release "$@"
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
printf 'Ready: %s/build/linux-release/screenfx\n' "$PWD"
