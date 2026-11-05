# Minimal bgfx runtime for AOWIS (replaces add_subdirectory(external/bgfx.cmake)).
#
# bgfx.cmake builds the whole engine for every renderer, plus an ASTC texture
# encoder and, through shaderc, Tint/Dawn. AOWIS uses a small 2D subset of the
# runtime and embeds pre-compiled shaders, so this file builds only:
#
#   bx    bx/src/amalgamated.cpp          (upstream unity build, 1 TU)
#   bimg  bimg/src/image.cpp              (format tables, mip/convert helpers;
#                                          software block decoders and the
#                                          ASTC encoder switched off)
#   bgfx  bgfx/src/amalgamated.cpp        (upstream unity build, 1 TU) with
#                                          only the renderers in
#                                          AOWIS_BGFX_RENDERERS compiled in
#
# Measured on Linux (2 cores, Release, OpenGL + Vulkan): 3 TUs / ~55 s CPU instead of
# 76 TUs / ~262 s CPU for the bgfx.cmake runtime. Every switch used here is an
# upstream configuration macro; no vendor source is edited.
#
# Inputs:  CMAKE_CURRENT_SOURCE_DIR/external/{bx,bimg,bgfx}
# Outputs: targets bx, bimg, bgfx; AOWIS_BGFX_DIR; AOWIS_BGFX_RENDERERS
#          (list of enabled renderers, see map_bgfx_backend.h).

set(AOWIS_BX_DIR "${AOWIS_RENDER_VENDOR_ROOT}/bx")
set(AOWIS_BIMG_DIR "${AOWIS_RENDER_VENDOR_ROOT}/bimg")
set(AOWIS_BGFX_DIR "${AOWIS_RENDER_VENDOR_ROOT}/bgfx")
foreach(aowis_bgfx_required
        "${AOWIS_BX_DIR}/src/amalgamated.cpp"
        "${AOWIS_BIMG_DIR}/src/image.cpp"
        "${AOWIS_BGFX_DIR}/src/amalgamated.cpp"
        "${AOWIS_BGFX_DIR}/src/bgfx_shader.sh")
    if(NOT EXISTS "${aowis_bgfx_required}")
        message(FATAL_ERROR
            "${aowis_bgfx_required} is missing. Run ./git_submodule_init.sh")
    endif()
endforeach()

# --- Renderers -----------------------------------------------------------------
# bgfx still picks among these at runtime (RendererType::Count). Defining any
# BGFX_CONFIG_RENDERER_* switches every renderer not defined to 0
# (bgfx/src/config.h), so the list below is exactly what gets compiled.
# Allowed: OPENGL OPENGLES VULKAN DIRECT3D11 METAL. Each needs its shader
# family in src/map/bgfx/shaders/bin (glsl essl spirv dx11 metal).
if(EMSCRIPTEN OR ANDROID)
    set(aowis_bgfx_default_renderers OPENGLES)
elseif(WIN32)
    # dx11 shaders are committed like the others (compiled on Linux under
    # Wine, see cmake/aowis_bgfx_shaders.cmake); OpenGL stays as fallback.
    set(aowis_bgfx_default_renderers DIRECT3D11 OPENGL)
elseif(APPLE)
    set(aowis_bgfx_default_renderers METAL)
else()
    # Both kept so the renderer that works on this desktop today (Wayland or
    # X11) stays available. Dropping VULKAN saves ~20 s CPU and the spirv set.
    set(aowis_bgfx_default_renderers OPENGL VULKAN)
endif()
set(AOWIS_BGFX_RENDERERS "${aowis_bgfx_default_renderers}" CACHE STRING
    "bgfx renderers compiled in (OPENGL OPENGLES VULKAN DIRECT3D11 METAL)")
if((EMSCRIPTEN OR ANDROID) AND NOT AOWIS_BGFX_RENDERERS STREQUAL "OPENGLES")
    message(FATAL_ERROR "WebAssembly and Android builds require AOWIS_BGFX_RENDERERS=OPENGLES")
endif()
foreach(aowis_bgfx_renderer IN LISTS AOWIS_BGFX_RENDERERS)
    if(NOT aowis_bgfx_renderer MATCHES "^(OPENGL|OPENGLES|VULKAN|DIRECT3D11|METAL)$")
        message(FATAL_ERROR "Unknown AOWIS_BGFX_RENDERERS entry: ${aowis_bgfx_renderer}")
    endif()
endforeach()
if("OPENGL" IN_LIST AOWIS_BGFX_RENDERERS AND "OPENGLES" IN_LIST AOWIS_BGFX_RENDERERS)
    message(FATAL_ERROR "bgfx cannot compile OPENGL and OPENGLES into one build")
endif()
message(STATUS "AOWIS bgfx renderers: ${AOWIS_BGFX_RENDERERS}")

# --- bx ------------------------------------------------------------------------
add_library(bx STATIC "${AOWIS_BX_DIR}/src/amalgamated.cpp")
if(MINGW)
    set(aowis_bx_compat mingw)
elseif(WIN32)
    set(aowis_bx_compat msvc)
elseif(APPLE)
    set(aowis_bx_compat osx)
else()
    set(aowis_bx_compat linux)
endif()
# (bx/3rdparty only holds bx's own test framework; not checked out.)
target_include_directories(bx PUBLIC
    "${AOWIS_BX_DIR}/include"
    "${AOWIS_BX_DIR}/include/compat/${aowis_bx_compat}")
target_compile_definitions(bx PUBLIC
    "BX_CONFIG_DEBUG=$<CONFIG:Debug>"
    __STDC_LIMIT_MACROS
    __STDC_FORMAT_MACROS
    __STDC_CONSTANT_MACROS)
target_compile_features(bx PUBLIC cxx_std_14)
if(MSVC)
    target_compile_options(bx PUBLIC /Zc:__cplusplus /Zc:preprocessor)
    target_compile_options(bx PRIVATE /EHs-c-)
    target_compile_definitions(bx PRIVATE _HAS_EXCEPTIONS=0)
endif()
# bx's SIMD headers use SSE4.1 intrinsics on x86 (upstream minspec SSE4.2).
# PUBLIC: bimg, bgfx and shaderc compile those headers too. Emscripten
# reports x86 but builds bx's wasm path.
if(NOT MSVC AND NOT EMSCRIPTEN)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        set(aowis_bx_arch "${CMAKE_OSX_ARCHITECTURES}")
    elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        set(aowis_bx_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
    else()
        set(aowis_bx_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(aowis_bx_arch MATCHES "^(x86_64|amd64|AMD64|x64|i[3-6]86|x86)$")
        target_compile_options(bx PUBLIC -msse4.2)
    elseif(aowis_bx_arch MATCHES "x86_64")
        # Universal macOS build: scope the flag to the x86_64 slice.
        target_compile_options(bx PUBLIC "SHELL:-Xarch_x86_64 -msse4.2")
    endif()
endif()
if(WIN32)
    target_link_libraries(bx PUBLIC psapi)
elseif(APPLE)
    target_link_libraries(bx PUBLIC "-framework Foundation")
elseif(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(bx PUBLIC Threads::Threads ${CMAKE_DL_LIBS} rt)
endif()
if(EMSCRIPTEN)
    # bx threading is unsupported on Emscripten (bgfx runs single-threaded
    # there); this does not change Qt's own pthread configuration.
    target_compile_definitions(bx PUBLIC BX_CONFIG_SUPPORTS_THREADING=0)
    # bx itself: POSIX declarations some bx revisions miss (includes the
    # version shim below).
    target_compile_options(bx PRIVATE
        "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/bgfx_emscripten/bx_emscripten_compat.h")
    # Everything that includes bx headers (bimg, bgfx, the map): Emscripten 4.0.7 only defines the lower-case version macros
    # bx/platform.h expects in upper case. INTERFACE, so consumers get it
    # through linking and no target has to repeat it.
    target_compile_options(bx INTERFACE
        "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/bgfx_emscripten/emscripten_version_compat.h")
endif()

# --- bimg ----------------------------------------------------------------------
# AOWIS uploads RGBA8 only: no software decoding of BC/ETC/ATC/ASTC textures,
# which also removes the ASTC encoder library (~58 s CPU) entirely.
add_library(bimg STATIC "${AOWIS_BIMG_DIR}/src/image.cpp")
target_include_directories(bimg PUBLIC "${AOWIS_BIMG_DIR}/include")
target_compile_definitions(bimg PRIVATE BIMG_CONFIG_DECODE_ENABLE=0)
target_link_libraries(bimg PUBLIC bx)

# --- bgfx ----------------------------------------------------------------------
add_library(bgfx STATIC "${AOWIS_BGFX_DIR}/src/amalgamated.cpp")
target_include_directories(bgfx
    PUBLIC "${AOWIS_BGFX_DIR}/include"
    PRIVATE "${AOWIS_BGFX_DIR}/3rdparty" "${AOWIS_BGFX_DIR}/3rdparty/khronos")
target_link_libraries(bgfx PUBLIC bimg bx)
target_compile_definitions(bgfx PRIVATE
    # Single-threaded API on every platform: MapBgfxSurface calls
    # bgfx::renderFrame() before init and drives frames from the GUI thread.
    BGFX_CONFIG_MULTITHREADED=0
    BGFX_CONFIG_DEFAULT_MAX_ENCODERS=1
    # No video decoding (Vulkan/D3D/Metal video paths).
    BGFX_CONFIG_VIDEO=0
    "BGFX_CONFIG_DEBUG_ANNOTATION=$<CONFIG:Debug>")
foreach(aowis_bgfx_renderer IN LISTS AOWIS_BGFX_RENDERERS)
    if(aowis_bgfx_renderer STREQUAL "OPENGL")
        # The value is the GL version; bgfx raises anything below 4.3 to 4.3.
        target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_OPENGL=43)
    elseif(aowis_bgfx_renderer STREQUAL "OPENGLES")
        # WebGL 2 only, never the WebGL 1 fallback.
        target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_OPENGLES=30)
    else()
        target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_RENDERER_${aowis_bgfx_renderer}=1)
    endif()
endforeach()
if(MSVC)
    target_compile_definitions(bgfx PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(WIN32)
    target_include_directories(bgfx PRIVATE
        "${AOWIS_BGFX_DIR}/3rdparty/directx-headers/include/directx"
        "${AOWIS_BGFX_DIR}/3rdparty/directx-headers/include")
elseif(APPLE)
    # The Metal renderer is metal-cpp based (3rdparty/metal-cpp).
    target_link_libraries(bgfx PUBLIC
        "-framework Cocoa" "-framework Metal" "-framework QuartzCore"
        "-framework IOKit" "-framework CoreFoundation")
elseif(EMSCRIPTEN)
    # Enables WebGL 2 in Emscripten's GL library (as bgfx.cmake did). The
    # application target additionally requires WebGL 2 for every context
    # (MIN_WEBGL_VERSION, CMakeLists.txt); bgfx itself does not impose that.
    target_link_options(bgfx PUBLIC "-sMAX_WEBGL_VERSION=2")
endif()
# Linux needs no extra link libraries: bgfx dlopen()s libEGL, libGL,
# libwayland-egl (X11 and Wayland, see MapBgfxSurface) and libvulkan itself.
# bgfx.cmake's X11/OpenGL/wayland-egl link lines are legacy.

# Vendor code: keep its warnings out of AOWIS build logs.
foreach(aowis_bgfx_vendor_target bx bimg bgfx)
    target_compile_options(${aowis_bgfx_vendor_target} PRIVATE
        $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
endforeach()
