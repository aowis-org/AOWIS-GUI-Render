# AOWIS bgfx shader compiler (opt-in developer tool).
#
# Only needed to REGENERATE the committed shader binaries in
# src/map/bgfx/shaders/bin/ after a .sc file changed
# (-DAOWIS_BGFX_SHADER_TOOLS=ON). Normal desktop and WASM builds never build
# it: they embed the committed binaries.
#
# Compared with bgfx.cmake's shaderc this drops what AOWIS never uses:
# * Tint + Dawn (WGSL/WebGPU, 381 of ~690 translation units),
# * DXC (DXIL / Direct3D 12),
# * the SPIRV-Tools reducer (spirv-reduce, never called by shaderc),
# What remains is the shaderc front end and preprocessor, glslang
# (GLSL -> SPIR-V), the SPIRV-Tools optimizer/validator and SPIRV-Cross
# (SPIR-V -> GLSL/ESSL/MSL). DXBC (dx11) comes from Microsoft's
# d3dcompiler_47.dll, which bgfx ships in tools/bin/windows: on a Windows host
# shaderc loads it directly (copied next to the executable, so Windows' own
# or Wine's builtin compiler is never picked); on Linux and macOS shaderc runs
# it through bgfx's d3d4linux helper (tools/bin/windows/d3d4linux.exe) under
# Wine.
#
# Requires: AOWIS_BGFX_DIR (external/bgfx) and the bx target from
# aowis_bgfx.cmake. Host-only: never include this for an Emscripten build.

if(EMSCRIPTEN)
    message(FATAL_ERROR "aowis-bgfx-shaderc is a host tool; it cannot be built for WebAssembly")
endif()

set(AOWIS_BGFX_GLSLANG_DIR "${AOWIS_BGFX_DIR}/3rdparty/glslang")
set(AOWIS_BGFX_SPIRV_TOOLS_DIR "${AOWIS_BGFX_DIR}/3rdparty/spirv-tools")
set(AOWIS_BGFX_SPIRV_HEADERS_DIR "${AOWIS_BGFX_DIR}/3rdparty/spirv-headers")
set(AOWIS_BGFX_SPIRV_CROSS_DIR "${AOWIS_BGFX_DIR}/3rdparty/spirv-cross")
foreach(aowis_bgfx_tool_dir
        "${AOWIS_BGFX_GLSLANG_DIR}" "${AOWIS_BGFX_SPIRV_TOOLS_DIR}"
        "${AOWIS_BGFX_SPIRV_HEADERS_DIR}" "${AOWIS_BGFX_SPIRV_CROSS_DIR}"
        "${AOWIS_BGFX_DIR}/tools/shaderc" "${AOWIS_BGFX_DIR}/3rdparty/d3d4linux"
        "${AOWIS_BGFX_DIR}/tools/bin/windows")
    if(NOT IS_DIRECTORY "${aowis_bgfx_tool_dir}")
        message(FATAL_ERROR
            "${aowis_bgfx_tool_dir} is missing. The shader tools need the full "
            "bgfx shader-compiler sources: run ./git_submodule_init.sh --shader-tools")
    endif()
endforeach()

# Third-party compiler code: its warnings are not ours to fix.
set(AOWIS_BGFX_TOOL_QUIET $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)

# --- glslang: GLSL/HLSL front end producing SPIR-V ---------------------------
file(GLOB_RECURSE AOWIS_BGFX_GLSLANG_SOURCES
    "${AOWIS_BGFX_GLSLANG_DIR}/glslang/*.cpp"
    "${AOWIS_BGFX_GLSLANG_DIR}/hlsl/*.cpp"
    "${AOWIS_BGFX_GLSLANG_DIR}/SPIRV/*.cpp"
    "${AOWIS_BGFX_GLSLANG_DIR}/OGLCompilersDLL/*.cpp")
if(WIN32)
    list(FILTER AOWIS_BGFX_GLSLANG_SOURCES EXCLUDE REGEX "glslang/OSDependent/Unix/")
else()
    list(FILTER AOWIS_BGFX_GLSLANG_SOURCES EXCLUDE REGEX "glslang/OSDependent/Windows/")
endif()
add_library(aowis-bgfx-glslang STATIC ${AOWIS_BGFX_GLSLANG_SOURCES})
target_compile_definitions(aowis-bgfx-glslang PRIVATE ENABLE_OPT=1 ENABLE_HLSL=1)
target_compile_options(aowis-bgfx-glslang PRIVATE ${AOWIS_BGFX_TOOL_QUIET})
target_include_directories(aowis-bgfx-glslang
    PUBLIC
        "${AOWIS_BGFX_GLSLANG_DIR}"
        "${AOWIS_BGFX_GLSLANG_DIR}/glslang/Public"
        "${AOWIS_BGFX_GLSLANG_DIR}/glslang/Include"
    PRIVATE
        "${AOWIS_BGFX_DIR}/3rdparty"
        "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/include"
        "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/source")

# --- SPIRV-Tools: optimizer and validator (no reducer) ------------------------
# Every source/*.cpp and source/util/*.cpp is part of the core library; opt/
# is the optimizer, val/ the validator glslang runs after code generation.
file(GLOB AOWIS_BGFX_SPIRV_OPT_SOURCES
    "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/source/*.cpp"
    "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/source/util/*.cpp"
    "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/source/opt/*.cpp"
    "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/source/val/*.cpp")
# mimalloc.cpp needs the external mimalloc allocator; pch_source.cpp only
# feeds upstream's precompiled-header build.
list(FILTER AOWIS_BGFX_SPIRV_OPT_SOURCES EXCLUDE REGEX "/source/(mimalloc|pch_source)\\.cpp$")
add_library(aowis-bgfx-spirv-opt STATIC ${AOWIS_BGFX_SPIRV_OPT_SOURCES})
target_compile_options(aowis-bgfx-spirv-opt PRIVATE ${AOWIS_BGFX_TOOL_QUIET})
target_include_directories(aowis-bgfx-spirv-opt
    PUBLIC
        "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/include"
    PRIVATE
        "${AOWIS_BGFX_SPIRV_TOOLS_DIR}"
        "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/include/generated"
        "${AOWIS_BGFX_SPIRV_TOOLS_DIR}/source"
        "${AOWIS_BGFX_SPIRV_HEADERS_DIR}/include")

# --- SPIRV-Cross: SPIR-V -> GLSL / ESSL / MSL, plus reflection ----------------
add_library(aowis-bgfx-spirv-cross STATIC
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_cfg.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_cpp.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_cross.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_cross_parsed_ir.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_cross_util.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_glsl.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_hlsl.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_msl.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_parser.cpp"
    "${AOWIS_BGFX_SPIRV_CROSS_DIR}/spirv_reflect.cpp")
target_compile_definitions(aowis-bgfx-spirv-cross PRIVATE SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS)
target_compile_options(aowis-bgfx-spirv-cross PRIVATE ${AOWIS_BGFX_TOOL_QUIET})
target_include_directories(aowis-bgfx-spirv-cross
    PUBLIC "${AOWIS_BGFX_SPIRV_CROSS_DIR}"
    PRIVATE "${AOWIS_BGFX_SPIRV_CROSS_DIR}/include")

# --- shaderc -----------------------------------------------------------------
file(GLOB AOWIS_BGFX_SHADERC_SOURCES "${AOWIS_BGFX_DIR}/tools/shaderc/*.cpp")
add_executable(aowis-bgfx-shaderc
    ${AOWIS_BGFX_SHADERC_SOURCES}
    # Shader binary chunk writer and vertex layout tables shared with the runtime.
    "${AOWIS_BGFX_DIR}/src/shader.cpp"
    "${AOWIS_BGFX_DIR}/src/vertexlayout.cpp")
target_compile_options(aowis-bgfx-shaderc PRIVATE ${AOWIS_BGFX_TOOL_QUIET})
target_include_directories(aowis-bgfx-shaderc PRIVATE "${AOWIS_BGFX_DIR}/include")
# bimg only for its headers: src/shader.cpp includes bgfx_p.h.
target_link_libraries(aowis-bgfx-shaderc PRIVATE
    bimg bx aowis-bgfx-glslang aowis-bgfx-spirv-opt aowis-bgfx-spirv-cross)
# Upstream switches (tools/shaderc/shaderc.h); no source edits. Without the
# Tint headers on the include path __has_include would already disable WGSL,
# the explicit 0 documents it. The D3D compiler is found by __has_include:
# <d3dcompiler.h> on Windows, <d3d4linux.h> elsewhere.
target_compile_definitions(aowis-bgfx-shaderc PRIVATE
    SHADERC_CONFIG_HAS_TINT=0
    SHADERC_CONFIG_HAS_DXC=0)
if(NOT WIN32)
    # As upstream's scripts/shaderc.lua: d3d4linux plus the Win32 type
    # adapter of the DirectX headers (already in the runtime sparse set).
    target_include_directories(aowis-bgfx-shaderc PRIVATE
        "${AOWIS_BGFX_DIR}/3rdparty/d3d4linux/include"
        "${AOWIS_BGFX_DIR}/3rdparty/directx-headers/include"
        "${AOWIS_BGFX_DIR}/3rdparty/directx-headers/include/wsl/stubs")
endif()
# shaderc looks for the D3D compiler relative to its own executable: Windows
# next to it, d3d4linux in ../windows/ (shaderc_hlsl.cpp). Both layouts in one
# build-tree folder: bin/ for the executable, windows/ for the helper.
set(AOWIS_BGFX_SHADERC_ROOT "${CMAKE_BINARY_DIR}/aowis-bgfx-shaderc")
set_target_properties(aowis-bgfx-shaderc PROPERTIES
    OUTPUT_NAME aowis-bgfx-shaderc
    RUNTIME_OUTPUT_DIRECTORY "${AOWIS_BGFX_SHADERC_ROOT}/bin")
if(WIN32)
    set(aowis_bgfx_d3d_runtime "${AOWIS_BGFX_SHADERC_ROOT}/bin")
    set(aowis_bgfx_d3d_files d3dcompiler_47.dll)
else()
    set(aowis_bgfx_d3d_runtime "${AOWIS_BGFX_SHADERC_ROOT}/windows")
    set(aowis_bgfx_d3d_files d3d4linux.exe d3dcompiler_47.dll)
endif()
foreach(aowis_bgfx_d3d_file IN LISTS aowis_bgfx_d3d_files)
    add_custom_command(TARGET aowis-bgfx-shaderc POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${aowis_bgfx_d3d_runtime}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${AOWIS_BGFX_DIR}/tools/bin/windows/${aowis_bgfx_d3d_file}"
            "${aowis_bgfx_d3d_runtime}/${aowis_bgfx_d3d_file}"
        VERBATIM)
endforeach()
