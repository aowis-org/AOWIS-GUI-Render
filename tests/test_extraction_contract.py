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
# Network render contract and globe network scene: Render-owned, no hydraulic data.
assert "add_library(aowis-render-globe-network STATIC" in cmake and "AOWIS::RenderGlobeNetwork" in cmake
for relative in ['src/map/core/map_network_types.h', 'src/network/infrastructure_entity_traits.h', 'src/network/network_render_snapshot.h', 'src/network/network_symbology.h', 'src/network/network_symbology_rendering.h', 'src/map/render/map_render_types.h', 'src/map/render/map_label_settings.h', 'src/map/render/map_tile_streaming_policy.h', 'src/map/render/map_network_render_data.h', 'src/map/render/map_network_render_symbology.h', 'src/map/render/map_retained_network_scene.h', 'src/map/render/map_globe_icon_draw_order.h', 'src/map/render/map_icon_atlas.h', 'src/map/render/map_icon_atlas.cpp', 'src/map/render/map_globe_network_scene.h', 'src/map/render/map_globe_network_scene.cpp', 'src/map/render/map_globe_render_frame.h', 'src/map/render/map_globe_render_frame.cpp']:
    text = (root / relative).read_text()
    for included in re.findall(r'#include\s+"([^"]+)"', text):
        assert included.startswith("aowis/model/") or (root / "src" / included).is_file(), \
            relative + " includes " + included
    assert "HydraulicData" not in text and "hydraulic_data.h" not in text, relative
    assert not re.search(r"\bauto\s+\w", text), relative
# The globe network scene keeps using the shared flow / link / underground /
# invalidation code (formerly checked by the GUI's boundary test).
globe_scene = (root / "src/map/render/map_globe_network_scene.cpp").read_text()
assert not re.search(r"FlowDirection(SpacingPixels|ChevronHalfWidthRatio|MaximumMarkersPerLink)|MapGlobeNetworkScene::ellipsoidNormalAt", globe_scene)
assert "longLinkSubdivisionCount" not in globe_scene
assert not re.search(r"UndergroundSubdivision(FallbackTargetLengthM|MinimumTargetLengthM|TerrainCellFraction|MaximumCount)", globe_scene)
for token in ["MapGlobeFlowGeometry::emitStrokes(", "MapGlobeLinkGeometry::emitCurvedSegments",
              "MapGlobeLinkGeometry::emitRibbonCorners", "MapGlobeUndergroundGeometry::subdivisionCount(",
              "MapGlobeUndergroundGeometry::emitBuriedRuns(", "MapGlobeSceneSync::forSymbology(changes)",
              "applySceneInvalidation("]:
    assert token in globe_scene, token
# bgfx map renderers: Render-owned, host assets and settings injected.
assert "add_library(aowis-render-map STATIC" in cmake and "AOWIS::RenderMap" in cmake
for relative in ['src/map/bgfx/map_bgfx_2d_snapshot_adapter.h', 'src/map/bgfx/map_bgfx_2d_symbology.h', 'src/map/bgfx/map_bgfx_2d_picking.h', 'src/map/bgfx/map_bgfx_layer_visibility.h', 'src/map/bgfx/map_bgfx_entity_style.h', 'src/map/bgfx/map_bgfx_entity_style.cpp', 'src/map/bgfx/map_bgfx_globe_network.h', 'src/map/bgfx/map_bgfx_globe_network.cpp', 'src/map/bgfx/map_bgfx_globe_surface.h', 'src/map/bgfx/map_bgfx_globe_surface.cpp', 'src/map/bgfx/map_bgfx_hud.h', 'src/map/bgfx/map_bgfx_hud.cpp', 'src/map/bgfx/map_bgfx_labels.h', 'src/map/bgfx/map_bgfx_labels.cpp']:
    text = (root / relative).read_text()
    for included in re.findall(r'#include\s+"([^"]+)"', text):
        assert included.startswith("aowis/model/") or (root / "src" / included).is_file(), \
            relative + " includes " + included
    assert "guiConfiguration" not in text and '":/' not in text, relative
    assert not re.search(r"\bauto\s+\w", text), relative
# bgfx globe network / surface keep using Render's GPU building blocks
# (formerly checked by the GUI's boundary test).
network = (root / "src/map/bgfx/map_bgfx_globe_network.cpp").read_text() + \
    (root / "src/map/bgfx/map_bgfx_globe_network.h").read_text()
surface = (root / "src/map/bgfx/map_bgfx_globe_surface.cpp").read_text() + \
    (root / "src/map/bgfx/map_bgfx_globe_surface.h").read_text()
surface_cpp = (root / "src/map/bgfx/map_bgfx_globe_surface.cpp").read_text()
dynamic_buffers = r"Dynamic(Vertex|Index)BufferHandle|createDynamic(Vertex|Index)Buffer|bgfx::update\("
assert not re.search(dynamic_buffers, network)
assert not re.search(dynamic_buffers, surface)
assert not re.search(r"this->(link|node|icon|junction|model)_layout", network)
for token in ["gpu_buffers.populateInput(&draw_input)", "this->gpu_layouts.initialize(strides)",
              "gpu_resources.populateInput(&draw_input)"] + [
        "this->gpu_layouts.%s()" % layout for layout in ["link", "node", "icon", "junction", "model"]]:
    assert token in network, token
assert not re.search(r"bgfx::(createUniform|createTexture2D|destroy\()", network)
assert not re.search(r"tile_index_buffers|MapBgfxGlobeSurface::tileIndexBuffer\(", surface)
assert not re.search(r"this->(surface_program|wireframe_program|tile_sampler|view_projection_uniform|missing_texture|cap_texture|tile_uniform|background_uniform|origin_uniform|light_uniform|shading_uniform)", surface)
assert not re.search(r"this->(sky_program|sky_triangle|sky_forward_uniform|heatmap_bake_program|heatmap_surface_program|heatmap_pages)|bgfx::createFrameBuffer\(|bgfx::allocTransientVertexBuffer\(", surface)
for token in ["this->tile_index_pool.get(tile.grid_cell_count)", "this->terrain_buffers.patchWindow(",
              "this->terrain_gpu.drawTile(", "this->terrain_gpu.drawCap(", "this->terrain_gpu.drawWireframe(",
              "this->sky_gpu.initialize(", "this->sky_gpu.draw(", "this->heatmap_gpu.initialize(",
              "this->heatmap_gpu.bakePage("]:
    assert token in surface_cpp, token
render_body = surface_cpp.split("MapBgfxGlobeSurface::FrameResult MapBgfxGlobeSurface::render(", 1)[1].split("// --- Heatmap", 1)[0]
assert not re.search(r"bgfx::(setIndexBuffer|setVertexBuffer|submit)\(", render_body)
# The toolkit-neutral map view: QObject, no Qt Widgets, no GUI services; the
# host (MapBgfxMapViewHost) embeds its native window and injects the rest.
view_header = (root / "src/map/bgfx/map_bgfx_map_view.h").read_text()
view_source = (root / "src/map/bgfx/map_bgfx_map_view.cpp").read_text()
view = view_header + view_source
assert "class MapBgfxMapViewHost" in view_header
assert "class MapBgfxMapView final : public QObject" in view_header
assert "AUTOMOC ON" in cmake and "src/map/bgfx/map_bgfx_map_view.cpp" in cmake
for relative in ["src/map/bgfx/map_bgfx_map_view.h", "src/map/bgfx/map_bgfx_map_view.cpp",
                 "src/map/core/map_frame_pacer.h", "src/map/core/map_input_focus_policy.h"]:
    text = (root / relative).read_text()
    for included in re.findall(r'#include\s+"([^"]+)"', text):
        assert included.startswith("aowis/model/") or (root / "src" / included).is_file(), \
            relative + " includes " + included
    assert not re.search(r"\bauto\s+\w", text), relative
assert not re.search(r"#include\s+<Q(Widget|VBoxLayout|Application)>|\bQWidget\s*[*&]|createWindowContainer\(\w", view)
for token in ["guiConfiguration", "guiShortcutRegistry", "UnitProfileManager", "UnitAwareView",
              "BgfxQtNativeSurface", "MapBgfxGlobeHudController", "MapScaleDistance",
              "mapGlobeTankAlbedoImage", '":/']:
    assert token not in view, token
for token in ["this->host->mapViewVisible()", "this->host->mapViewPalette()",
              "this->host->mapViewFont()", "this->host->mapViewFocusNavigationInput(",
              "captureNativeSurface(this->native_window)", "this->globe_hud_state_provider("]:
    assert token in view_source, token
print("Renderer extraction and shader-family contract: OK")
