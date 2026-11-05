#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for source in map_bgfx_2d_network_input.h map_bgfx_2d_scene.h \
    map_bgfx_2d_mesh.h map_bgfx_2d_gpu.cpp map_bgfx_2d_gpu.h \
    map_bgfx_2d_style_binding.h; do
    test -f "$root/src/map/bgfx/$source" || exit 1
done
if grep -Eq 'map_retained_network_scene.h|network_render_snapshot.h|common/_enums_structs.h|map_network_render_symbology.h' \
    "$root/src/map/bgfx/map_bgfx_2d_"{scene.h,mesh.h,gpu.h,gpu.cpp}; then
    echo 'ERROR: renderer includes GUI-owned network/model headers' >&2
    exit 1
fi
printf 'PASS: production 2D renderer source ownership and decoupled interface\n'
