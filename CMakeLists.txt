cmake_minimum_required(VERSION 3.21)
project(AOWIS-GUI-Render LANGUAGES CXX)

# A standalone Render checkout is a fully buildable/testable CMake project.
# When embedded by GUI/Mobile, their parent CTest settings remain authoritative.
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

# Immutable geometry interfaces used by both the Qt Widgets desktop host and
# the Android Qt Quick host. Header-only, without Qt, bgfx, model or GPU.
# Keep this target available when only testing geometry on a host lacking
# native graphics/Qt toolchains.
add_library(aowis-render-geometry INTERFACE)
add_library(AOWIS::RenderGeometry ALIAS aowis-render-geometry)
target_compile_features(aowis-render-geometry INTERFACE cxx_std_20)
target_include_directories(aowis-render-geometry INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_sources(aowis-render-geometry INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/map_bgfx_2d_projection.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/map_bgfx_three_view_camera.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/map_bgfx_globe_icon_index.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/map_bgfx_globe_height_cache.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/map_bgfx_globe_imagery.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/map_bgfx_globe_draw_plan.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/map_bgfx_2d_imagery_layout.h")

option(AOWIS_RENDER_BUILD_DEVICE "Build the bgfx-dependent renderer device" ON)
option(AOWIS_RENDER_BUILD_MAP_CORE
    "Build AOWIS map GPU uploaders and embed its shader families" ON)
# Production 2D geometry is usable on Mobile with Qt6 Core only.
option(AOWIS_RENDER_BUILD_NETWORK_SCENE
    "Build 2D network scene and mesh staging (Qt6 Core, no bgfx)"
    ${AOWIS_RENDER_BUILD_MAP_CORE})
if(AOWIS_RENDER_BUILD_MAP_CORE AND NOT AOWIS_RENDER_BUILD_NETWORK_SCENE)
    message(FATAL_ERROR "Map GPU core requires AOWIS_RENDER_BUILD_NETWORK_SCENE=ON")
endif()
option(AOWIS_RENDER_BUILD_GLOBE_MODELS
    "Build host-neutral production globe tank/reservoir meshes (Qt6 Gui)"
    ${AOWIS_RENDER_BUILD_MAP_CORE})
option(AOWIS_RENDER_BUILD_GEOMETRY_TESTS
    "Build the standalone production geometry regressions" ${PROJECT_IS_TOP_LEVEL})
if(AOWIS_RENDER_BUILD_MAP_CORE AND NOT AOWIS_RENDER_BUILD_GLOBE_MODELS)
    message(FATAL_ERROR "Map render core requires AOWIS_RENDER_BUILD_GLOBE_MODELS=ON")
endif()
if(AOWIS_RENDER_BUILD_MAP_CORE AND NOT AOWIS_RENDER_BUILD_DEVICE)
    message(FATAL_ERROR "Map render core requires AOWIS_RENDER_BUILD_DEVICE=ON")
endif()

if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
    enable_testing()
    foreach(AOWIS_GEOMETRY_TEST IN ITEMS projection globe_icon_index globe_height_cache camera globe_imagery)
        add_executable(aowis-render-${AOWIS_GEOMETRY_TEST}-tests
            tests/render_${AOWIS_GEOMETRY_TEST}_test.cpp)
        target_link_libraries(aowis-render-${AOWIS_GEOMETRY_TEST}-tests PRIVATE
            AOWIS::RenderGeometry)
        add_test(NAME aowis-render-${AOWIS_GEOMETRY_TEST}
            COMMAND aowis-render-${AOWIS_GEOMETRY_TEST}-tests)
    endforeach()
    # Keep the GPU style-binding ABI testable without Qt, bgfx, or a GPU.
    add_executable(aowis-render-2d-style-binding-tests
        tests/render_2d_style_binding_test.cpp)
    target_link_libraries(aowis-render-2d-style-binding-tests PRIVATE
        AOWIS::RenderGeometry)
    add_test(NAME aowis-render-2d-style-binding
        COMMAND aowis-render-2d-style-binding-tests)
    add_executable(aowis-render-globe-draw-plan-tests
        tests/render_globe_draw_plan_test.cpp)
    target_link_libraries(aowis-render-globe-draw-plan-tests PRIVATE AOWIS::RenderGeometry)
    add_test(NAME aowis-render-globe-draw-plan COMMAND aowis-render-globe-draw-plan-tests)
    set_tests_properties(aowis-render-globe-draw-plan PROPERTIES LABELS "aowis;rendering")
    # Compile and execute the production GPU submission implementation against
    # a tiny recording bgfx stub: this exercises real draw calls even on
    # machines without a graphics device, Qt or vendor submodules.
    add_executable(aowis-render-globe-buffer-pool-mock-tests
        src/map/bgfx/map_bgfx_globe_buffer_pool.cpp
        tests/render_globe_buffer_pool_mock_test.cpp)
    target_link_libraries(aowis-render-globe-buffer-pool-mock-tests PRIVATE AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-buffer-pool-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-buffer-pool-mock
        COMMAND aowis-render-globe-buffer-pool-mock-tests)
    set_tests_properties(aowis-render-globe-buffer-pool-mock PROPERTIES LABELS "aowis;rendering")
    add_executable(aowis-render-globe-vertex-layouts-mock-tests
        src/map/bgfx/map_bgfx_globe_vertex_layouts.cpp
        tests/render_globe_vertex_layouts_mock_test.cpp)
    target_link_libraries(aowis-render-globe-vertex-layouts-mock-tests PRIVATE AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-vertex-layouts-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-vertex-layouts-mock
        COMMAND aowis-render-globe-vertex-layouts-mock-tests)
    set_tests_properties(aowis-render-globe-vertex-layouts-mock PROPERTIES LABELS "aowis;rendering")
    # Production Globe flow/tangent geometry compiled against tiny Qt math
    # shims: validates the real algorithm without a GPU or Qt dev packages.
    # Compile the actual globe link tessellator against Qt math stubs too.
    add_executable(aowis-render-globe-link-geometry-mock-tests
        src/map/render/map_globe_link_geometry.cpp
        tests/render_globe_link_geometry_mock_test.cpp)
    target_link_libraries(aowis-render-globe-link-geometry-mock-tests PRIVATE
        AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-link-geometry-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_qt")
    add_test(NAME aowis-render-globe-link-geometry-mock
        COMMAND aowis-render-globe-link-geometry-mock-tests)
    set_tests_properties(aowis-render-globe-link-geometry-mock PROPERTIES LABELS "aowis;geometry")
    add_executable(aowis-render-globe-flow-geometry-mock-tests
        src/map/render/map_globe_flow_geometry.cpp
        tests/render_globe_flow_geometry_mock_test.cpp)
    target_link_libraries(aowis-render-globe-flow-geometry-mock-tests PRIVATE
        AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-flow-geometry-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_qt")
    add_test(NAME aowis-render-globe-flow-geometry-mock
        COMMAND aowis-render-globe-flow-geometry-mock-tests)
    set_tests_properties(aowis-render-globe-flow-geometry-mock PROPERTIES LABELS "aowis;geometry")
    # Compile the real terrain topology/skirt implementation with mock Qt,
    # then exercise the same GPU index pool against the recording bgfx stub.
    add_executable(aowis-render-globe-tile-topology-mock-tests
        src/map/render/map_globe_tile_topology.cpp
        tests/render_globe_tile_topology_test.cpp)
    target_link_libraries(aowis-render-globe-tile-topology-mock-tests PRIVATE
        AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-tile-topology-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_qt")
    add_test(NAME aowis-render-globe-tile-topology-mock
        COMMAND aowis-render-globe-tile-topology-mock-tests)
    set_tests_properties(aowis-render-globe-tile-topology-mock PROPERTIES
        LABELS "aowis;geometry")
    add_executable(aowis-render-globe-tile-index-pool-mock-tests
        src/map/bgfx/map_bgfx_globe_tile_index_pool.cpp
        src/map/render/map_globe_tile_topology.cpp
        tests/render_globe_tile_index_pool_mock_test.cpp)
    target_link_libraries(aowis-render-globe-tile-index-pool-mock-tests PRIVATE
        AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-tile-index-pool-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_qt"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-tile-index-pool-mock
        COMMAND aowis-render-globe-tile-index-pool-mock-tests)
    set_tests_properties(aowis-render-globe-tile-index-pool-mock PROPERTIES
        LABELS "aowis;rendering")
    # Real terrain vertex/index uploader compiled against recording bgfx.
    # Covers sparse DEM mesh patches (offset and byte range), buffer reuse,
    # invalid patches, complete replacement and device recreation.
    add_executable(aowis-render-globe-terrain-buffer-pool-mock-tests
        src/map/bgfx/map_bgfx_globe_terrain_buffer_pool.cpp
        tests/render_globe_terrain_buffer_pool_mock_test.cpp)
    target_link_libraries(aowis-render-globe-terrain-buffer-pool-mock-tests PRIVATE
        AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-terrain-buffer-pool-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-terrain-buffer-pool-mock
        COMMAND aowis-render-globe-terrain-buffer-pool-mock-tests)
    set_tests_properties(aowis-render-globe-terrain-buffer-pool-mock PROPERTIES
        LABELS "aowis;rendering")
    # The production sky/heatmap GPU submission/resource owner, compiled
    # against recording bgfx without Qt or a GPU device.
    add_executable(aowis-render-globe-overlay-gpu-mock-tests
        src/map/bgfx/map_bgfx_globe_overlay_gpu.cpp
        tests/render_globe_overlay_gpu_mock_test.cpp)
    target_link_libraries(aowis-render-globe-overlay-gpu-mock-tests PRIVATE
        AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-overlay-gpu-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-overlay-gpu-mock
        COMMAND aowis-render-globe-overlay-gpu-mock-tests)
    set_tests_properties(aowis-render-globe-overlay-gpu-mock PROPERTIES
        LABELS "aowis;rendering")
    # Base terrain GPU submission has its own independent mock regression.
    add_executable(aowis-render-globe-terrain-gpu-mock-tests
        src/map/bgfx/map_bgfx_globe_terrain_gpu.cpp
        tests/render_globe_terrain_gpu_mock_test.cpp)
    target_link_libraries(aowis-render-globe-terrain-gpu-mock-tests PRIVATE
        AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-terrain-gpu-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-terrain-gpu-mock
        COMMAND aowis-render-globe-terrain-gpu-mock-tests)
    set_tests_properties(aowis-render-globe-terrain-gpu-mock PROPERTIES
        LABELS "aowis;rendering")
    # The same production retained-scene invalidation policy is exercised
    # without Qt, a GPU, or hydraulic model types.
    add_executable(aowis-render-globe-scene-sync-tests
        src/map/render/map_globe_scene_sync.cpp
        tests/render_globe_scene_sync_test.cpp)
    target_link_libraries(aowis-render-globe-scene-sync-tests PRIVATE
        AOWIS::RenderGeometry)
    add_test(NAME aowis-render-globe-scene-sync
        COMMAND aowis-render-globe-scene-sync-tests)
    set_tests_properties(aowis-render-globe-scene-sync PROPERTIES
        LABELS "aowis;geometry")
    # Pure C++ classification policy: real production source, no Qt or GPU.
    add_executable(aowis-render-globe-underground-geometry-tests
        src/map/render/map_globe_underground_geometry.cpp
        tests/render_globe_underground_geometry_test.cpp)
    target_link_libraries(aowis-render-globe-underground-geometry-tests PRIVATE
        AOWIS::RenderGeometry)
    add_test(NAME aowis-render-globe-underground-geometry
        COMMAND aowis-render-globe-underground-geometry-tests)
    set_tests_properties(aowis-render-globe-underground-geometry PROPERTIES
        LABELS "aowis;geometry")
    add_executable(aowis-render-globe-resource-pool-mock-tests
        src/map/bgfx/map_bgfx_globe_resource_pool.cpp
        tests/render_globe_resource_pool_mock_test.cpp)
    target_link_libraries(aowis-render-globe-resource-pool-mock-tests PRIVATE AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-resource-pool-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-resource-pool-mock
        COMMAND aowis-render-globe-resource-pool-mock-tests)
    set_tests_properties(aowis-render-globe-resource-pool-mock PROPERTIES LABELS "aowis;rendering")
    add_executable(aowis-render-globe-gpu-mock-tests
        src/map/bgfx/map_bgfx_globe_gpu.cpp
        tests/render_globe_gpu_mock_test.cpp)
    target_link_libraries(aowis-render-globe-gpu-mock-tests PRIVATE AOWIS::RenderGeometry)
    target_include_directories(aowis-render-globe-gpu-mock-tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/mock_bgfx")
    add_test(NAME aowis-render-globe-gpu-mock COMMAND aowis-render-globe-gpu-mock-tests)
    set_tests_properties(aowis-render-globe-gpu-mock PROPERTIES LABELS "aowis;rendering")
endif()

# Camera-independent network scene and CPU mesh staging; no GPU or MODEL.
if(AOWIS_RENDER_BUILD_NETWORK_SCENE)
    find_package(Qt6 REQUIRED COMPONENTS Core)
    add_library(aowis-render-network STATIC
        src/map/render/map_node_declutter.h
        src/map/render/map_node_declutter.cpp
        src/map/bgfx/map_bgfx_2d_network_input.h
        src/map/bgfx/map_bgfx_2d_scene.h
        src/map/bgfx/map_bgfx_2d_scene_variants.h
        src/map/bgfx/map_bgfx_2d_mesh.h)
    add_library(AOWIS::RenderNetwork ALIAS aowis-render-network)
    target_compile_features(aowis-render-network PUBLIC cxx_std_20)
    target_include_directories(aowis-render-network PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-network PUBLIC Qt6::Core AOWIS::RenderGeometry)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-2d-network-scene-tests
            tests/render_2d_network_scene_test.cpp)
        target_link_libraries(aowis-render-2d-network-scene-tests PRIVATE AOWIS::RenderNetwork)
        add_test(NAME aowis-render-2d-network-scene COMMAND aowis-render-2d-network-scene-tests)
    endif()
endif()

# Globe model mesh generation is shared, and independent of GUI, MODEL,
# GeographicLib, bgfx and any native surface. Host-specific material images
# remain in the host until the texture asset resources are extracted.
if(AOWIS_RENDER_BUILD_GLOBE_MODELS)
    find_package(Qt6 REQUIRED COMPONENTS Gui)
    add_library(aowis-render-globe STATIC
        src/map/render/map_globe_tile_topology.h
        src/map/render/map_globe_tile_topology.cpp
        src/map/render/map_globe_render_instances.h
        src/map/render/map_globe_vertical_transform.h
        src/map/render/map_globe_surface_vertex.h
        src/map/render/map_globe_flow_geometry.h
        src/map/render/map_globe_flow_geometry.cpp
        src/map/render/map_globe_link_geometry.h
        src/map/render/map_globe_link_geometry.cpp
        src/map/render/map_globe_underground_geometry.h
        src/map/render/map_globe_underground_geometry.cpp
        src/map/render/map_globe_scene_sync.h
        src/map/render/map_globe_scene_sync.cpp
        src/map/render/map_globe_tank_model.h
        src/map/render/map_globe_tank_model.cpp
        src/map/render/map_globe_reservoir_model.h
        src/map/render/map_globe_reservoir_model.cpp)
    add_library(AOWIS::RenderGlobe ALIAS aowis-render-globe)
    target_compile_features(aowis-render-globe PUBLIC cxx_std_20)
    target_include_directories(aowis-render-globe PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-globe PUBLIC Qt6::Gui)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        # Real Qt headers/production library linkage, in addition to the
        # Qt-free mock test registered at the top of this file.
        # Real Qt and production RenderGlobe link, not only mocked headers.
        add_executable(aowis-render-globe-tile-topology-tests
            tests/render_globe_tile_topology_test.cpp)
        target_link_libraries(aowis-render-globe-tile-topology-tests PRIVATE
            AOWIS::RenderGlobe)
        add_test(NAME aowis-render-globe-tile-topology
            COMMAND aowis-render-globe-tile-topology-tests)
        set_tests_properties(aowis-render-globe-tile-topology PROPERTIES
            LABELS "aowis;geometry;integration")
        add_executable(aowis-render-globe-link-geometry-tests
            tests/render_globe_link_geometry_mock_test.cpp)
        target_link_libraries(aowis-render-globe-link-geometry-tests PRIVATE AOWIS::RenderGlobe)
        add_test(NAME aowis-render-globe-link-geometry
            COMMAND aowis-render-globe-link-geometry-tests)
        set_tests_properties(aowis-render-globe-link-geometry PROPERTIES LABELS "aowis;geometry")
        # Independently link the actual production RenderGlobe archive, so
        # missing CMake object registration cannot hide behind mock tests.
        add_executable(aowis-render-globe-underground-library-link-tests
            tests/render_globe_underground_geometry_test.cpp)
        target_link_libraries(aowis-render-globe-underground-library-link-tests PRIVATE
            AOWIS::RenderGlobe)
        add_test(NAME aowis-render-globe-underground-library-link
            COMMAND aowis-render-globe-underground-library-link-tests)
        set_tests_properties(aowis-render-globe-underground-library-link PROPERTIES
            LABELS "aowis;geometry;integration")
        # Resolve the production RenderGlobe symbol, not only the isolated
        # Qt-free compilation above.
        add_executable(aowis-render-globe-scene-sync-library-link-tests
            tests/render_globe_scene_sync_test.cpp)
        target_link_libraries(aowis-render-globe-scene-sync-library-link-tests PRIVATE
            AOWIS::RenderGlobe)
        add_test(NAME aowis-render-globe-scene-sync-library-link
            COMMAND aowis-render-globe-scene-sync-library-link-tests)
        set_tests_properties(aowis-render-globe-scene-sync-library-link PROPERTIES
            LABELS "aowis;geometry;integration")
        add_executable(aowis-render-globe-flow-geometry-tests
            tests/render_globe_flow_geometry_test.cpp)
        target_link_libraries(aowis-render-globe-flow-geometry-tests PRIVATE AOWIS::RenderGlobe)
        add_test(NAME aowis-render-globe-flow-geometry
            COMMAND aowis-render-globe-flow-geometry-tests)
        set_tests_properties(aowis-render-globe-flow-geometry PROPERTIES LABELS "aowis;geometry")
        add_executable(aowis-render-globe-models-tests
            tests/render_globe_models_test.cpp)
        target_link_libraries(aowis-render-globe-models-tests PRIVATE AOWIS::RenderGlobe)
        add_test(NAME aowis-render-globe-models COMMAND aowis-render-globe-models-tests)
        set_tests_properties(aowis-render-globe-models PROPERTIES LABELS "aowis;geometry")
    endif()
endif()

# Geodesy shared by every map host: WGS84 ellipsoid / ECEF and the orbit
# camera basis, Web Mercator tile math, UTM and project-local metric
# coordinates. Its public API uses MODEL's geodetic value types
# (aowis/model/gis.h); GeographicLib stays an implementation detail. Host
# targets are reused when present, see cmake/aowis_render_dependencies.cmake.
option(AOWIS_RENDER_BUILD_GEO
    "Build the geodesy library (Qt6 Gui, GeographicLib, AOWIS-SERVER-MODEL)"
    ${AOWIS_RENDER_BUILD_MAP_CORE})
# The terrain service's normalized DEM tile and dataset-catalog formats,
# decoded identically by every host. Transport stays with the host.
option(AOWIS_RENDER_BUILD_TERRAIN_DATA
    "Build the terrain tile/dataset decoders (Qt6 Core)"
    ${AOWIS_RENDER_BUILD_MAP_CORE})

if(AOWIS_RENDER_BUILD_GEO)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/aowis_render_dependencies.cmake")
    add_library(aowis-render-geo STATIC
        src/geo/geo_metric_projection.cpp
        src/geo/geo_metric_projection.h
        src/geo/geo_web_mercator.cpp
        src/geo/geo_web_mercator.h
        src/geo/geo_wgs84_ellipsoid.cpp
        src/geo/geo_wgs84_ellipsoid.h)
    add_library(AOWIS::RenderGeo ALIAS aowis-render-geo)
    # GeoMetricProjection is a QObject (originChanged signal).
    set_target_properties(aowis-render-geo PROPERTIES AUTOMOC ON)
    target_compile_features(aowis-render-geo PUBLIC cxx_std_20)
    target_include_directories(aowis-render-geo PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-geo
        PUBLIC Qt6::Core Qt6::Gui AOWIS::Model
        PRIVATE GeographicLib_STATIC)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-geo-tests tests/render_geo_test.cpp)
        target_link_libraries(aowis-render-geo-tests PRIVATE AOWIS::RenderGeo)
        add_test(NAME aowis-render-geo COMMAND aowis-render-geo-tests)
        set_tests_properties(aowis-render-geo PROPERTIES LABELS "aowis;geometry;integration")
    endif()
endif()

# Camera/view state shared by every map host: MapModel (2D continuous zoom,
# globe orbit rig, screen <-> WGS84, imagery tile addressing through the
# host's MapImageryProviderDirectory), globe picking rays, render cache math
# and the opt-in render diagnostics counters.
option(AOWIS_RENDER_BUILD_VIEW
    "Build the map view state library (Qt6 Gui, RenderGeo)"
    ${AOWIS_RENDER_BUILD_GEO})
if(AOWIS_RENDER_BUILD_VIEW AND NOT AOWIS_RENDER_BUILD_GEO)
    message(FATAL_ERROR "AOWIS_RENDER_BUILD_VIEW requires AOWIS_RENDER_BUILD_GEO=ON")
endif()
if(AOWIS_RENDER_BUILD_VIEW)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_library(aowis-render-view STATIC
        src/map/core/map_view_types.h
        src/map/core/map_imagery_provider_directory.h
        src/map/core/map_model.h
        src/map/core/map_model.cpp
        src/map/render/map_camera_view_state.h
        src/map/render/map_globe_picking.h
        src/map/render/map_globe_picking.cpp
        src/map/render/map_render_cache_math.h
        src/map/render/map_render_cache_math.cpp
        src/map/render/map_render_diagnostics.h)
    add_library(AOWIS::RenderView ALIAS aowis-render-view)
    # MapModel is a QObject (camera and provider signals).
    set_target_properties(aowis-render-view PROPERTIES AUTOMOC ON)
    target_compile_features(aowis-render-view PUBLIC cxx_std_20)
    target_include_directories(aowis-render-view PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-view PUBLIC Qt6::Core Qt6::Gui AOWIS::RenderGeo)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-view-tests tests/render_view_test.cpp)
        target_link_libraries(aowis-render-view-tests PRIVATE AOWIS::RenderView)
        add_test(NAME aowis-render-view COMMAND aowis-render-view-tests)
        set_tests_properties(aowis-render-view PROPERTIES LABELS "aowis;integration")
    endif()
endif()

if(AOWIS_RENDER_BUILD_TERRAIN_DATA)
    find_package(Qt6 REQUIRED COMPONENTS Core)
    add_library(aowis-render-terrain-data STATIC
        src/map/data/map_terrain_dataset.cpp
        src/map/data/map_terrain_dataset.h
        src/map/data/map_terrain_tile.cpp
        src/map/data/map_terrain_tile.h)
    add_library(AOWIS::RenderTerrainData ALIAS aowis-render-terrain-data)
    target_compile_features(aowis-render-terrain-data PUBLIC cxx_std_20)
    target_include_directories(aowis-render-terrain-data PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-terrain-data PUBLIC Qt6::Core)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-terrain-data-tests tests/render_terrain_data_test.cpp)
        target_link_libraries(aowis-render-terrain-data-tests PRIVATE AOWIS::RenderTerrainData)
        add_test(NAME aowis-render-terrain-data COMMAND aowis-render-terrain-data-tests)
        set_tests_properties(aowis-render-terrain-data PROPERTIES LABELS "aowis;integration")
    endif()
endif()

# Map data repositories shared by every map host: imagery tile and DEM
# tile request scheduling, caches, decode workers (on WASM the browser's
# native image decoders), the terrain dataset catalog and cache deletion.
# They talk to the map server through the InterfaceServerMap contract; the
# host supplies the transport (MapServerTransport).
option(AOWIS_RENDER_BUILD_MAP_DATA
    "Build the map data repositories (Qt6 Gui, RenderView, RenderTerrainData)"
    ${AOWIS_RENDER_BUILD_VIEW})
if(AOWIS_RENDER_BUILD_MAP_DATA AND NOT (AOWIS_RENDER_BUILD_VIEW AND AOWIS_RENDER_BUILD_TERRAIN_DATA))
    message(FATAL_ERROR "AOWIS_RENDER_BUILD_MAP_DATA requires AOWIS_RENDER_BUILD_VIEW "
        "and AOWIS_RENDER_BUILD_TERRAIN_DATA")
endif()
if(AOWIS_RENDER_BUILD_MAP_DATA)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_library(aowis-render-map-data STATIC
        src/services/interface_server_map.h
        src/map/data/map_server_transport.h
        src/map/data/map_terrain_tile_source.h
        src/map/data/map_tile_repository.h
        src/map/data/map_tile_repository.cpp
        src/map/data/map_terrain_repository.h
        src/map/data/map_terrain_repository.cpp)
    if(EMSCRIPTEN)
        # Browser-native tile decoding (createImageBitmap + OffscreenCanvas
        # on a dedicated worker); its EMSCRIPTEN_KEEPALIVE entry points stay
        # exported because the tile repository references the decoder.
        target_sources(aowis-render-map-data PRIVATE
            src/map/data/map_tile_browser_decoder.h
            src/map/data/map_tile_browser_decoder.cpp)
    endif()
    add_library(AOWIS::RenderMapData ALIAS aowis-render-map-data)
    # InterfaceServerMap and both repositories are QObjects.
    set_target_properties(aowis-render-map-data PROPERTIES AUTOMOC ON)
    target_compile_features(aowis-render-map-data PUBLIC cxx_std_20)
    target_include_directories(aowis-render-map-data PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-map-data PUBLIC
        Qt6::Core Qt6::Gui AOWIS::RenderView AOWIS::RenderTerrainData)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-map-data-tests tests/render_map_data_test.cpp)
        set_target_properties(aowis-render-map-data-tests PROPERTIES AUTOMOC ON)
        target_link_libraries(aowis-render-map-data-tests PRIVATE AOWIS::RenderMapData)
        add_test(NAME aowis-render-map-data COMMAND aowis-render-map-data-tests)
        set_tests_properties(aowis-render-map-data PROPERTIES
            LABELS "aowis;integration" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

# CPU side of the globe terrain: quadtree leaf selection and LOD, the
# retained tile window with sparse DEM patches, the terrain mesh worker
# (normals, skirts, ancestor stand-ins), terrain-aware navigation, the globe
# camera and the heatmap bake scene. Terrain tiles come from the host's
# MapTerrainTileSource; the detail-reduction setting is an injected getter.
option(AOWIS_RENDER_BUILD_GLOBE_TERRAIN
    "Build the globe terrain pipeline (Qt6 Gui, RenderView, RenderTerrainData, RenderGlobe)"
    ${AOWIS_RENDER_BUILD_VIEW})
if(AOWIS_RENDER_BUILD_GLOBE_TERRAIN AND NOT (AOWIS_RENDER_BUILD_VIEW
        AND AOWIS_RENDER_BUILD_TERRAIN_DATA AND AOWIS_RENDER_BUILD_GLOBE_MODELS))
    message(FATAL_ERROR "AOWIS_RENDER_BUILD_GLOBE_TERRAIN requires AOWIS_RENDER_BUILD_VIEW, "
        "AOWIS_RENDER_BUILD_TERRAIN_DATA and AOWIS_RENDER_BUILD_GLOBE_MODELS")
endif()
if(AOWIS_RENDER_BUILD_GLOBE_TERRAIN)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_library(aowis-render-globe-terrain STATIC
        src/map/data/map_terrain_tile_source.h
        src/map/render/map_globe_surface_hit.h
        src/map/render/map_globe_terrain_levels.h
        src/map/render/map_globe_retained_origin.h
        src/map/render/map_globe_camera.h
        src/map/render/map_globe_camera.cpp
        src/map/render/map_globe_heatmap_scene.h
        src/map/render/map_globe_heatmap_scene.cpp
        src/map/render/map_globe_surface_scene.h
        src/map/render/map_globe_surface_scene.cpp
        src/map/render/map_globe_surface_preparation.h
        src/map/render/map_globe_surface_preparation.cpp
        src/map/render/map_terrain_mesh_scheduler.h
        src/map/render/map_terrain_mesh_scheduler.cpp
        src/map/render/map_globe_terrain_navigation.h
        src/map/render/map_globe_terrain_navigation.cpp)
    add_library(AOWIS::RenderGlobeTerrain ALIAS aowis-render-globe-terrain)
    target_compile_features(aowis-render-globe-terrain PUBLIC cxx_std_20)
    target_include_directories(aowis-render-globe-terrain PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-globe-terrain PUBLIC
        Qt6::Core Qt6::Gui AOWIS::RenderView AOWIS::RenderTerrainData AOWIS::RenderGlobe)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-globe-terrain-tests tests/render_globe_terrain_test.cpp)
        target_link_libraries(aowis-render-globe-terrain-tests PRIVATE AOWIS::RenderGlobeTerrain)
        add_test(NAME aowis-render-globe-terrain COMMAND aowis-render-globe-terrain-tests)
        set_tests_properties(aowis-render-globe-terrain PROPERTIES
            LABELS "aowis;integration" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

# The network render contract (network/*.h, map/core/map_network_types.h:
# render snapshot, symbology settings and ramps, entity kinds) and the CPU
# side of the globe network: retained globe network scene (pipes, junction
# impostors, icons, models, chevrons, X-Ray runs, demand-point drape),
# render frame, icon atlas, resolved symbology data. The host fills the
# snapshot and the symbology (the GUI from its hydraulic data).
option(AOWIS_RENDER_BUILD_GLOBE_NETWORK
    "Build the globe network scene (Qt6 Gui, RenderGlobeTerrain, RenderNetwork, GeographicLib)"
    ${AOWIS_RENDER_BUILD_GLOBE_TERRAIN})
if(AOWIS_RENDER_BUILD_GLOBE_NETWORK AND NOT (AOWIS_RENDER_BUILD_GLOBE_TERRAIN
        AND AOWIS_RENDER_BUILD_NETWORK_SCENE))
    message(FATAL_ERROR "AOWIS_RENDER_BUILD_GLOBE_NETWORK requires "
        "AOWIS_RENDER_BUILD_GLOBE_TERRAIN and AOWIS_RENDER_BUILD_NETWORK_SCENE")
endif()
if(AOWIS_RENDER_BUILD_GLOBE_NETWORK)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_library(aowis-render-globe-network STATIC
        src/map/core/map_network_types.h
        src/network/infrastructure_entity_traits.h
        src/network/network_render_snapshot.h
        src/network/network_symbology.h
        src/network/network_symbology_rendering.h
        src/map/render/map_render_types.h
        src/map/render/map_label_settings.h
        src/map/render/map_tile_streaming_policy.h
        src/map/render/map_network_render_data.h
        src/map/render/map_network_render_symbology.h
        src/map/render/map_retained_network_scene.h
        src/map/render/map_globe_icon_draw_order.h
        src/map/render/map_icon_atlas.h
        src/map/render/map_icon_atlas.cpp
        src/map/render/map_globe_network_scene.h
        src/map/render/map_globe_network_scene.cpp
        src/map/render/map_globe_render_frame.h
        src/map/render/map_globe_render_frame.cpp)
    add_library(AOWIS::RenderGlobeNetwork ALIAS aowis-render-globe-network)
    target_compile_features(aowis-render-globe-network PUBLIC cxx_std_20)
    target_include_directories(aowis-render-globe-network PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-globe-network
        PUBLIC Qt6::Core Qt6::Gui AOWIS::RenderView AOWIS::RenderGlobeTerrain
               AOWIS::RenderGlobe AOWIS::RenderNetwork
        PRIVATE GeographicLib_STATIC)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-globe-network-tests tests/render_globe_network_test.cpp)
        target_link_libraries(aowis-render-globe-network-tests PRIVATE AOWIS::RenderGlobeNetwork)
        add_test(NAME aowis-render-globe-network COMMAND aowis-render-globe-network-tests)
        set_tests_properties(aowis-render-globe-network PROPERTIES
            LABELS "aowis;integration" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

# Exactly one bgfx target per process. GUI creates it here using its existing
# pinned vendor sources; Mobile supplies its already-pinned bgfx.cmake target.
# Nothing is fetched during configure.
set(AOWIS_RENDER_VENDOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external" CACHE PATH
    "Directory containing pinned bx, bimg and bgfx source checkouts")
if(AOWIS_RENDER_BUILD_DEVICE AND NOT TARGET bgfx)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/aowis_bgfx.cmake")
endif()

# No Qt dependency for host-independent GPU device, view and swapchain lifetime.
if(AOWIS_RENDER_BUILD_DEVICE)
add_library(aowis-render-device STATIC
    src/map/bgfx/bgfx_device_manager.cpp
    src/map/bgfx/bgfx_device_manager.h
    src/map/bgfx/bgfx_frame_coordinator.cpp
    src/map/bgfx/bgfx_frame_coordinator.h
    src/map/bgfx/bgfx_native_target_registry.cpp
    src/map/bgfx/bgfx_native_target_registry.h
    src/map/bgfx/bgfx_native_surface.h)
add_library(AOWIS::RenderDevice ALIAS aowis-render-device)
target_compile_features(aowis-render-device PUBLIC cxx_std_20)
target_include_directories(aowis-render-device PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_link_libraries(aowis-render-device PUBLIC bgfx AOWIS::RenderGeometry)
endif()

if(AOWIS_RENDER_BUILD_MAP_CORE)
    find_package(Qt6 REQUIRED COMPONENTS Gui)
    add_library(aowis-render-core STATIC
        src/map/bgfx/map_bgfx_backend.h
        src/map/bgfx/map_bgfx_2d_style_binding.h
        src/map/bgfx/map_bgfx_2d_gpu.cpp
        src/map/bgfx/map_bgfx_2d_gpu.h
        src/map/bgfx/map_bgfx_2d_imagery_atlas.cpp
        src/map/bgfx/map_bgfx_2d_imagery_atlas.h
        src/map/bgfx/map_bgfx_2d_imagery_gpu.cpp
        src/map/bgfx/map_bgfx_2d_imagery_gpu.h
        src/map/bgfx/map_bgfx_2d_imagery_layout.h
        src/map/bgfx/map_bgfx_globe_draw_plan.h
        src/map/bgfx/map_bgfx_globe_gpu.h
        src/map/bgfx/map_bgfx_globe_gpu.cpp
        src/map/bgfx/map_bgfx_globe_tile_index_pool.h
        src/map/bgfx/map_bgfx_globe_tile_index_pool.cpp
        src/map/bgfx/map_bgfx_globe_terrain_buffer_pool.h
        src/map/bgfx/map_bgfx_globe_terrain_buffer_pool.cpp
        src/map/bgfx/map_bgfx_globe_terrain_gpu.h
        src/map/bgfx/map_bgfx_globe_terrain_gpu.cpp
        src/map/bgfx/map_bgfx_globe_overlay_gpu.h
        src/map/bgfx/map_bgfx_globe_overlay_gpu.cpp
        src/map/bgfx/map_bgfx_globe_buffer_pool.h
        src/map/bgfx/map_bgfx_globe_buffer_pool.cpp
        src/map/bgfx/map_bgfx_globe_vertex_layouts.h
        src/map/bgfx/map_bgfx_globe_vertex_layouts.cpp
        src/map/bgfx/map_bgfx_globe_resource_pool.h
        src/map/bgfx/map_bgfx_globe_resource_pool.cpp
        src/map/bgfx/map_bgfx_globe_texture_pixels.cpp
        src/map/bgfx/map_bgfx_pan_reuse.h
        src/map/bgfx/map_bgfx_hud_canvas.h
        src/map/bgfx/map_bgfx_hud_gpu.cpp
        src/map/bgfx/map_bgfx_hud_gpu.h
        src/map/bgfx/map_bgfx_hud_text_cache.cpp
        src/map/bgfx/map_bgfx_hud_text_cache.h)
    add_library(AOWIS::RenderCore ALIAS aowis-render-core)
    target_compile_features(aowis-render-core PUBLIC cxx_std_20)
    target_include_directories(aowis-render-core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-core PUBLIC
        AOWIS::RenderDevice AOWIS::RenderGeometry AOWIS::RenderNetwork
        AOWIS::RenderGlobe Qt6::Gui)

    if(NOT AOWIS_BGFX_RENDERERS)
        message(FATAL_ERROR
            "Map render core needs AOWIS_BGFX_RENDERERS. Supply this variable "
            "when embedding a pre-existing bgfx target.")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/aowis_bgfx_shaders.cmake")
    if(AOWIS_BGFX_SHADER_TOOLS)
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/aowis_bgfx_shaderc.cmake")
    endif()
    aowis_bgfx_add_shaders(aowis-render-core)

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        add_executable(aowis-render-globe-texture-pixels-tests
            tests/render_globe_texture_pixels_test.cpp)
        target_link_libraries(aowis-render-globe-texture-pixels-tests PRIVATE AOWIS::RenderCore)
        add_test(NAME aowis-render-globe-texture-pixels
            COMMAND aowis-render-globe-texture-pixels-tests)
        set_tests_properties(aowis-render-globe-texture-pixels PROPERTIES LABELS "aowis;rendering")
    endif()

    # Parent-hosted link/resource regressions need the selected family too.
    # There is no parent scope in a standalone build.
    if(NOT PROJECT_IS_TOP_LEVEL)
        set(AOWIS_BGFX_TEST_SHADER_FAMILY "${AOWIS_BGFX_TEST_SHADER_FAMILY}" PARENT_SCOPE)
    endif()
    if(EMSCRIPTEN)
        option(AOWIS_WASM_SIMD "Compile app GPU code with wasm SIMD" ON)
        if(AOWIS_WASM_SIMD)
            target_compile_options(aowis-render-core PRIVATE -msimd128)
        endif()
    endif()
endif()

# Qt hosting adapter for native handles: QWindow -> BgfxNativeSurface (winId(),
# plus Qt's platform native interface for the Wayland/XCB display; that part
# needs Qt6::GuiPrivate and is compiled out without it). Shared by every Qt
# host and by MapBgfxMapView; RenderDevice stays Qt-free.
option(AOWIS_RENDER_BUILD_QT_SURFACE
    "Build the Qt native surface adapter (RenderDevice + Qt Gui)" ${AOWIS_RENDER_BUILD_MAP_CORE})
if(AOWIS_RENDER_BUILD_QT_SURFACE AND NOT AOWIS_RENDER_BUILD_DEVICE)
    message(FATAL_ERROR "AOWIS_RENDER_BUILD_QT_SURFACE requires AOWIS_RENDER_BUILD_DEVICE")
endif()
if(AOWIS_RENDER_BUILD_QT_SURFACE)
    find_package(Qt6 REQUIRED COMPONENTS Gui OPTIONAL_COMPONENTS GuiPrivate)
    add_library(aowis-render-qt-surface STATIC
        src/map/bgfx/bgfx_qt_native_surface.h
        src/map/bgfx/bgfx_qt_native_surface.cpp)
    add_library(AOWIS::RenderQtSurface ALIAS aowis-render-qt-surface)
    target_compile_features(aowis-render-qt-surface PUBLIC cxx_std_20)
    target_include_directories(aowis-render-qt-surface PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-qt-surface PUBLIC AOWIS::RenderDevice Qt6::Gui)
    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN AND TARGET Qt6::GuiPrivate)
        # qpa/qplatformnativeinterface.h: the wl_display / wl_surface and the
        # X11 display of a window.
        target_link_libraries(aowis-render-qt-surface PRIVATE Qt6::GuiPrivate)
        target_compile_definitions(aowis-render-qt-surface PRIVATE
            AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE=1)
    endif()
    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        # No display: the offscreen QPA has no native handles to capture.
        add_executable(aowis-render-bgfx-qt-native-surface-tests
            tests/bgfx_qt_native_surface_test.cpp)
        target_link_libraries(aowis-render-bgfx-qt-native-surface-tests
            PRIVATE AOWIS::RenderQtSurface)
        add_test(NAME aowis-render-bgfx-qt-native-surface
            COMMAND aowis-render-bgfx-qt-native-surface-tests)
        set_tests_properties(aowis-render-bgfx-qt-native-surface PROPERTIES
            LABELS "aowis;rendering" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()

# A real executable link test catches missing symbols and missing static-library
# Qt resources. It does not initialize bgfx or require a display/GPU.
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING AND AOWIS_RENDER_BUILD_MAP_CORE)
    add_executable(aowis-render-core-link-tests
        tests/render_core_link_test.cpp)
    target_link_libraries(aowis-render-core-link-tests PRIVATE AOWIS::RenderCore)
    target_compile_definitions(aowis-render-core-link-tests PRIVATE
        AOWIS_RENDER_TEST_SHADER_PATH=":/aowis/map/bgfx/${AOWIS_BGFX_TEST_SHADER_FAMILY}/vs_network_2d.bin")
    add_test(NAME aowis-render-core-link COMMAND aowis-render-core-link-tests)
    set_tests_properties(aowis-render-core-link PROPERTIES LABELS "aowis;integration")
endif()

# The bgfx map renderers above the GPU core: GPU network colouring
# (MapBgfxEntityStyle), the globe network and globe terrain surfaces, 2D
# symbology / picking / snapshot adapter, decluttered labels and the native
# HUD (layout, input, legend), and MapBgfxMapView: the toolkit-neutral map
# view (frame loop, device lifecycle, native-window event routing, picking,
# terrain queries). Host assets (model textures, HUD images), settings and the
# window embedding are injected by the host (MapBgfxMapViewHost: the GUI's Qt
# Widgets surface, the mobile Qt Quick item); native handles come from
# AOWIS::RenderQtSurface unless the host overrides the capture.
option(AOWIS_RENDER_BUILD_MAP
    "Build the bgfx map renderers (RenderCore, RenderGlobeNetwork, RenderMapData)"
    ${AOWIS_RENDER_BUILD_MAP_CORE})
if(AOWIS_RENDER_BUILD_MAP AND NOT (AOWIS_RENDER_BUILD_MAP_CORE
        AND AOWIS_RENDER_BUILD_GLOBE_NETWORK AND AOWIS_RENDER_BUILD_MAP_DATA
        AND AOWIS_RENDER_BUILD_QT_SURFACE))
    message(FATAL_ERROR "AOWIS_RENDER_BUILD_MAP requires AOWIS_RENDER_BUILD_MAP_CORE, "
        "AOWIS_RENDER_BUILD_GLOBE_NETWORK, AOWIS_RENDER_BUILD_MAP_DATA and "
        "AOWIS_RENDER_BUILD_QT_SURFACE")
endif()
if(AOWIS_RENDER_BUILD_MAP)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui)
    add_library(aowis-render-map STATIC
        src/map/bgfx/map_bgfx_2d_snapshot_adapter.h
        src/map/bgfx/map_bgfx_2d_symbology.h
        src/map/bgfx/map_bgfx_2d_picking.h
        src/map/bgfx/map_bgfx_layer_visibility.h
        src/map/bgfx/map_bgfx_entity_style.h
        src/map/bgfx/map_bgfx_entity_style.cpp
        src/map/bgfx/map_bgfx_globe_network.h
        src/map/bgfx/map_bgfx_globe_network.cpp
        src/map/bgfx/map_bgfx_globe_surface.h
        src/map/bgfx/map_bgfx_globe_surface.cpp
        src/map/bgfx/map_bgfx_hud.h
        src/map/bgfx/map_bgfx_hud.cpp
        src/map/bgfx/map_bgfx_labels.h
        src/map/bgfx/map_bgfx_labels.cpp
        src/map/bgfx/map_bgfx_map_view.h
        src/map/bgfx/map_bgfx_map_view.cpp
        src/map/core/map_frame_pacer.h
        src/map/core/map_input_focus_policy.h)
    add_library(AOWIS::RenderMap ALIAS aowis-render-map)
    set_target_properties(aowis-render-map PROPERTIES AUTOMOC ON)
    target_compile_features(aowis-render-map PUBLIC cxx_std_20)
    target_include_directories(aowis-render-map PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-map PUBLIC
        Qt6::Core Qt6::Gui AOWIS::RenderCore AOWIS::RenderGlobeNetwork
        AOWIS::RenderGlobeTerrain AOWIS::RenderMapData AOWIS::RenderView
        AOWIS::RenderQtSurface)
    if(EMSCRIPTEN AND AOWIS_WASM_SIMD)
        target_compile_options(aowis-render-map PRIVATE -msimd128)
    endif()

    if(AOWIS_RENDER_BUILD_GEOMETRY_TESTS)
        # CPU sides only, no bgfx device: entity-style texels, 2D picking,
        # layer visibility, HUD layout/input/legend, label placement, the
        # map view's host contract (never shown, so never initialized) and
        # the native-window focus policy.
        foreach(aowis_render_map_test IN ITEMS
                bgfx_entity_style bgfx_2d_picking map_bgfx_layer_visibility bgfx_hud bgfx_labels
                map_bgfx_map_view map_input_focus_policy)
            string(REPLACE "_" "-" aowis_render_map_test_name "${aowis_render_map_test}")
            add_executable(aowis-render-${aowis_render_map_test_name}-tests
                tests/${aowis_render_map_test}_test.cpp)
            target_link_libraries(aowis-render-${aowis_render_map_test_name}-tests
                PRIVATE AOWIS::RenderMap)
            add_test(NAME aowis-render-${aowis_render_map_test_name}
                COMMAND aowis-render-${aowis_render_map_test_name}-tests)
            set_tests_properties(aowis-render-${aowis_render_map_test_name} PROPERTIES
                LABELS "aowis;rendering" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        endforeach()
        # Full-viewport input sweeps are CPU-intensive: never concurrently,
        # with a bounded timeout to detect genuine hangs.
        set_tests_properties(aowis-render-bgfx-hud PROPERTIES RUN_SERIAL TRUE TIMEOUT 240)
    endif()
endif()

# Every Render test carries the "aowis" label; compile_linux_tests.sh runs
# only that label. A standalone build may also contain foreign tests
# (GeographicLib registers tools/test programs it never builds here).
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    get_property(_aowis_render_tests DIRECTORY PROPERTY TESTS)
    foreach(_aowis_render_test IN LISTS _aowis_render_tests)
        get_test_property(${_aowis_render_test} LABELS _aowis_render_labels)
        if(NOT _aowis_render_labels)
            set(_aowis_render_labels "")
        endif()
        if(NOT "aowis" IN_LIST _aowis_render_labels)
            list(APPEND _aowis_render_labels aowis)
            set_tests_properties(${_aowis_render_test} PROPERTIES
                LABELS "${_aowis_render_labels}")
        endif()
    endforeach()
endif()
