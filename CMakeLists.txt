cmake_minimum_required(VERSION 3.21)
project(AOWIS-GUI-Render LANGUAGES CXX)

# Exactly one bgfx target per process. GUI creates it here using its existing
# pinned vendor sources; Mobile supplies its already-pinned bgfx.cmake target.
# Nothing is fetched during configure.
set(AOWIS_RENDER_VENDOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external" CACHE PATH
    "Directory containing pinned bx, bimg and bgfx source checkouts")
if(NOT TARGET bgfx)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/aowis_bgfx.cmake")
endif()

# No Qt dependency for host-independent GPU device, view and swapchain lifetime.
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
target_link_libraries(aowis-render-device PUBLIC bgfx)

option(AOWIS_RENDER_BUILD_MAP_CORE
    "Build AOWIS map GPU uploaders and embed its shader families" ON)
if(AOWIS_RENDER_BUILD_MAP_CORE)
    find_package(Qt6 REQUIRED COMPONENTS Gui)
    add_library(aowis-render-core STATIC
        src/map/bgfx/map_bgfx_backend.h
        src/map/bgfx/map_bgfx_2d_imagery_atlas.cpp
        src/map/bgfx/map_bgfx_2d_imagery_atlas.h
        src/map/bgfx/map_bgfx_2d_imagery_gpu.cpp
        src/map/bgfx/map_bgfx_2d_imagery_gpu.h
        src/map/bgfx/map_bgfx_2d_imagery_layout.h
        src/map/bgfx/map_bgfx_hud_canvas.h
        src/map/bgfx/map_bgfx_hud_gpu.cpp
        src/map/bgfx/map_bgfx_hud_gpu.h
        src/map/bgfx/map_bgfx_hud_text_cache.cpp
        src/map/bgfx/map_bgfx_hud_text_cache.h)
    add_library(AOWIS::RenderCore ALIAS aowis-render-core)
    target_compile_features(aowis-render-core PUBLIC cxx_std_20)
    target_include_directories(aowis-render-core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
    target_link_libraries(aowis-render-core PUBLIC AOWIS::RenderDevice Qt6::Gui)

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

    # Pass the embedded family to a parent-hosted link/resource regression.
    set(AOWIS_BGFX_TEST_SHADER_FAMILY "${AOWIS_BGFX_TEST_SHADER_FAMILY}" PARENT_SCOPE)
    if(EMSCRIPTEN)
        option(AOWIS_WASM_SIMD "Compile app GPU code with wasm SIMD" ON)
        if(AOWIS_WASM_SIMD)
            target_compile_options(aowis-render-core PRIVATE -msimd128)
        endif()
    endif()
endif()
