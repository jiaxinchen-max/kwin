#!/usr/bin/env bash
# Build only on the designated Linux builder, with explicit configured trees.
set -euo pipefail
if [ "$#" != 5 ]; then
    echo 'Usage: build-native.sh cmake kwin-configured-build qtbase-configured-build qt-ahb-configured-build jobs' >&2
    exit 64
fi
[ "$(uname -s)" = Linux ] || { echo 'Linux builder required' >&2; exit 64; }
cmake_path="$1"
kwin_build="$2"
qtbase_build="$3"
qt_ahb_build="$4"
jobs="$5"
[ -x "$cmake_path" ] || { echo 'explicit cmake executable missing' >&2; exit 64; }
case "$jobs" in ''|*[!0-9]*) echo 'jobs must be a positive integer' >&2; exit 64;; esac
[ "$jobs" -gt 0 ] || exit 64
for build in "$kwin_build" "$qtbase_build" "$qt_ahb_build"; do
    [ -f "$build/CMakeCache.txt" ] || { echo "configured build missing: $build" >&2; exit 64; }
done
"$cmake_path" --build "$kwin_build" --target kwin_wayland -j "$jobs"
"$cmake_path" --build "$qtbase_build" --target QWaylandXdgShellIntegrationPlugin -j "$jobs"
"$cmake_path" --build "$qt_ahb_build" -j "$jobs"
