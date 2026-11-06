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
        AOWIS::RenderDevice AOWIS::RenderGeometry AOWIS::RenderNetwork Qt6::Gui)

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
