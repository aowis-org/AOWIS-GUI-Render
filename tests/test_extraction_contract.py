#!/usr/bin/env python3
"""Source-level extraction invariants; no Qt or initialized bgfx needed."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
cmake = (root / "CMakeLists.txt").read_text()
assert "add_library(aowis-render-device STATIC" in cmake
assert "add_library(aowis-render-core STATIC" in cmake
assert "TARGET bgfx" in cmake
assert "AOWIS_RENDER_BUILD_MAP_CORE" in cmake
assert "AOWIS::RenderDevice" in cmake
assert "AOWIS::RenderCore" in cmake
for source in [
    "bgfx_device_manager.cpp", "bgfx_frame_coordinator.cpp",
    "bgfx_native_target_registry.cpp", "map_bgfx_2d_imagery_gpu.cpp",
    "map_bgfx_hud_gpu.cpp", "map_bgfx_hud_text_cache.cpp",
]:
    assert (root / "src/map/bgfx" / source).is_file(), source
shader_names = ["vs_globe_surface", "fs_globe_surface", "vs_hud", "fs_hud"]
for family in ["glsl", "essl", "spirv", "metal", "dx11"]:
    for name in shader_names:
        assert (root / "src/map/bgfx/shaders/bin" / family / (name + ".bin")).is_file()
for source in ["map_bgfx_2d_projection.h", "map_bgfx_three_view_camera.h",
               "map_bgfx_globe_icon_index.h", "map_bgfx_globe_height_cache.h",
               "map_bgfx_globe_imagery.h", "map_bgfx_pan_reuse.h"]:
    assert (root / "src/map/bgfx" / source).is_file(), source
assert "AOWIS::RenderGeometry" in cmake
for path in (root / "src/map/bgfx").glob("*.cpp"):
    text = path.read_text()
    assert not re.search(r"\bauto\s+\w", text), path
print("Renderer extraction and shader-family contract: OK")
