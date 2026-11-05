#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
"${CXX:-c++}" -std=c++20 -Wall -Wextra -pedantic -I"$root/src" \
    "$root/tests/bgfx_frame_coordinator_test.cpp" \
    "$root/src/map/bgfx/bgfx_frame_coordinator.cpp" \
    -o "$work/frame-coordination-tests"
"$work/frame-coordination-tests"
