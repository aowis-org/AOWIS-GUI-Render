#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
for name in map_globe_render_instances.h map_globe_vertical_transform.h \
            map_globe_surface_vertex.h map_globe_tank_model.h \
            map_globe_tank_model.cpp map_globe_reservoir_model.h \
            map_globe_reservoir_model.cpp; do
    test -f "$root/src/map/render/$name" || { echo "Missing Render globe source: $name" >&2; exit 1; }
done
if grep -En '"(map/core/|map/data/|network/|common/|config/|aowis/model/)' \
    "$root/src/map/render/map_globe_"{tank_model,reservoir_model,render_instances,vertical_transform,surface_vertex}.*; then
    echo 'Render globe geometry must not depend on GUI or MODEL' >&2
    exit 1
fi
if grep -Eq ':/map-3d/|QFile\(' "$root/src/map/render/map_globe_"{tank_model,reservoir_model}.cpp; then
    echo 'GUI material resources leaked into the standalone globe geometry library' >&2
    exit 1
fi
printf 'PASS: shared globe mesh geometry is renderer-owned and host-independent\n'

if ! test -f "$root/src/map/bgfx/map_bgfx_globe_vertex_layouts.cpp"; then
    echo 'Shared GPU vertex layout implementation missing from Render' >&2
    exit 1
fi
if grep -E '"(network/|common/|map/core/|map/data/)' \
    "$root/src/map/bgfx/map_bgfx_globe_vertex_layouts."{h,cpp}; then
    echo 'GUI or MODEL dependency found in Render vertex layouts' >&2
    exit 1
fi
