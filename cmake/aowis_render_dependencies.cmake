# Geodesy and MODEL dependencies of the Render geo / terrain-data targets.
#
# Same rule as bgfx: exactly one copy of each library per process. A host
# (GUI, Mobile) that already added GeographicLib or AOWIS-SERVER-MODEL keeps
# ownership, and Render links the host's targets. Only a standalone Render
# checkout, or a host without its own copy, builds Render's pinned
# submodules. Nothing is fetched during configure.

set(AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/GeographicLib"
    CACHE PATH "Pinned GeographicLib checkout, used only when no GeographicLib_STATIC target exists")
set(AOWIS_RENDER_MODEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-SERVER-MODEL"
    CACHE PATH "Pinned AOWIS-SERVER-MODEL checkout, used only when no AOWIS::Model target exists")

# A function scope keeps GeographicLib's build switches away from the host:
# CMP0077 (GeographicLib requires CMake 3.17) lets these normal variables
# satisfy its option() calls without writing the host's cache.
function(aowis_render_add_geographiclib)
    if(NOT EXISTS "${AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "GeographicLib is missing at ${AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR}.\n"
            "Run ./git_submodule_init.sh, or add GeographicLib (target "
            "GeographicLib_STATIC) in the host before AOWIS-GUI-Render.")
    endif()
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_BOTH_LIBS OFF)
    set(BUILD_DOCUMENTATION OFF)
    set(BUILD_MANPAGES OFF)
    set(USE_BOOST OFF)
    # GeographicLib turns warnings into errors outside Release builds; a
    # newer compiler warning must not break the renderer build.
    set(CONVERT_WARNINGS_TO_ERRORS OFF)
    # GeographicLib calls enable_testing() and registers tests for tools and
    # test programs that EXCLUDE_FROM_ALL never builds. They cannot be
    # unregistered before CMake 3.28, so Render's CTest entry points select
    # the "aowis" label instead (see the end of CMakeLists.txt).
    add_subdirectory("${AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/aowis-render-geographiclib" EXCLUDE_FROM_ALL)
endfunction()

if(NOT TARGET GeographicLib_STATIC)
    aowis_render_add_geographiclib()
endif()
if(NOT TARGET GeographicLib_STATIC)
    message(FATAL_ERROR "GeographicLib did not define the static target GeographicLib_STATIC")
endif()

# MODEL registers its own regression tests when BUILD_TESTING is on; they
# belong to MODEL's own checkout. The function-scope variable keeps them out
# of a standalone Render build without changing the host's BUILD_TESTING.
function(aowis_render_add_model)
    set(BUILD_TESTING OFF)
    add_subdirectory("${AOWIS_RENDER_MODEL_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/aowis-render-model" EXCLUDE_FROM_ALL)
endfunction()

# Render uses MODEL's plain geodetic value types (aowis/model/gis.h) in its
# public geo API, so GUI, Mobile and Render share one CoordinateWGS84.
if(NOT TARGET AOWIS::Model)
    if(NOT EXISTS "${AOWIS_RENDER_MODEL_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "AOWIS-SERVER-MODEL is missing at ${AOWIS_RENDER_MODEL_SOURCE_DIR}.\n"
            "Run ./git_submodule_init.sh, or add AOWIS-SERVER-MODEL in the host "
            "before AOWIS-GUI-Render.")
    endif()
    aowis_render_add_model()
endif()
if(NOT TARGET AOWIS::Model)
    message(FATAL_ERROR "AOWIS-SERVER-MODEL did not define AOWIS::Model")
endif()
