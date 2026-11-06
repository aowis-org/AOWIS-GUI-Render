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
    "map_bgfx_globe_buffer_pool.cpp",
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
# Geodesy and terrain data: Render-owned, using only Qt, GeographicLib and
# MODEL's geodetic value types; never the GUI's headers or services.
assert "add_library(aowis-render-geo STATIC" in cmake
assert "add_library(aowis-render-terrain-data STATIC" in cmake
assert "AOWIS::RenderGeo" in cmake and "AOWIS::RenderTerrainData" in cmake
gui_headers = re.compile(r'#include\s+"(common|network|services|config|widgets|map/core|map/bgfx|entity_inspector)/')
for relative in ["src/geo/geo_metric_projection", "src/geo/geo_web_mercator",
                 "src/geo/geo_wgs84_ellipsoid", "src/map/data/map_terrain_tile",
                 "src/map/data/map_terrain_dataset"]:
    for suffix in (".h", ".cpp"):
        text = (root / (relative + suffix)).read_text()
        assert not gui_headers.search(text), relative + suffix
        assert not re.search(r"\bauto\s+\w", text), relative + suffix
for relative in ["src/map/data/map_terrain_tile", "src/map/data/map_terrain_dataset"]:
    for suffix in (".h", ".cpp"):
        text = (root / (relative + suffix)).read_text()
        assert "aowis/model/" not in text and "GeographicLib" not in text, relative + suffix
# MapModel and its helpers: Render-owned, host services injected.
assert "add_library(aowis-render-view STATIC" in cmake and "AOWIS::RenderView" in cmake
for relative in ["src/map/core/map_model.h", "src/map/core/map_model.cpp",
                 "src/map/core/map_view_types.h", "src/map/core/map_imagery_provider_directory.h",
                 "src/map/render/map_camera_view_state.h", "src/map/render/map_render_diagnostics.h",
                 "src/map/render/map_globe_picking.h", "src/map/render/map_globe_picking.cpp",
                 "src/map/render/map_render_cache_math.h", "src/map/render/map_render_cache_math.cpp"]:
    text = (root / relative).read_text()
    # Every quoted include resolves inside Render (or is MODEL's): no GUI header.
    for included in re.findall(r'#include\s+"([^"]+)"', text):
        assert included.startswith("aowis/model/") or (root / "src" / included).is_file(), \
            relative + " includes " + included
    assert "guiConfiguration" not in text and "mapProviderCatalog" not in text, relative
    assert not re.search(r"\bauto\s+\w", text), relative
# Globe terrain pipeline: Render-owned, terrain tiles through MapTerrainTileSource.
assert "add_library(aowis-render-globe-terrain STATIC" in cmake and "AOWIS::RenderGlobeTerrain" in cmake
for relative in ["src/map/data/map_terrain_tile_source.h", "src/map/render/map_globe_surface_hit.h",
                 "src/map/render/map_globe_terrain_levels.h", "src/map/render/map_globe_retained_origin.h"] + [
        "src/map/render/" + name + suffix
        for name in ["map_globe_camera", "map_globe_heatmap_scene", "map_globe_surface_scene",
                     "map_globe_surface_preparation", "map_terrain_mesh_scheduler",
                     "map_globe_terrain_navigation"]
        for suffix in (".h", ".cpp")]:
    text = (root / relative).read_text()
    for included in re.findall(r'#include\s+"([^"]+)"', text):
        assert included.startswith("aowis/model/") or (root / "src" / included).is_file(), \
            relative + " includes " + included
    assert "guiConfiguration" not in text and "MapTerrainRepository" not in text, relative
    assert not re.search(r"\bauto\s+\w", text), relative
# Map data repositories: Render-owned, transport injected.
assert "add_library(aowis-render-map-data STATIC" in cmake and "AOWIS::RenderMapData" in cmake
for relative in ['src/services/interface_server_map.h', 'src/map/data/map_tile_repository.h', 'src/map/data/map_tile_repository.cpp', 'src/map/data/map_terrain_repository.h', 'src/map/data/map_terrain_repository.cpp', 'src/map/data/map_tile_browser_decoder.h', 'src/map/data/map_tile_browser_decoder.cpp', 'src/map/data/map_server_transport.h']:
    text = (root / relative).read_text()
    for included in re.findall(r'#include\s+"([^"]+)"', text):
        assert included.startswith("aowis/model/") or (root / "src" / included).is_file(), \
            relative + " includes " + included
    assert "MapServerMode" not in text and "AOWIS_STANDALONE" not in text, relative
    assert "mapProviderCatalog" not in text and "guiConfiguration" not in text, relative
    assert not re.search(r"\bauto\s+\w", text), relative
print("Renderer extraction and shader-family contract: OK")
