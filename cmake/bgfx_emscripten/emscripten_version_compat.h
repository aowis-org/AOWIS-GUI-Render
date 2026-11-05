// Emscripten 4.0.7 exports lowercase version macros in version.h,
// while current bx/platform.h expects uppercase names. Keep the alias
// local to AOWIS (pre-included into every bx consumer through the bx target,
// cmake/aowis_bgfx.cmake) instead of editing the submodule.
#pragma once

#if !defined(__EMSCRIPTEN__)
#error "emscripten_version_compat.h is only for Emscripten builds"
#endif

#include <emscripten/version.h>

#if !defined(__EMSCRIPTEN_MAJOR__)
#  if !defined(__EMSCRIPTEN_major__)
#    error "Emscripten major version macro is unavailable"
#  endif
#  define __EMSCRIPTEN_MAJOR__ __EMSCRIPTEN_major__
#endif
#if !defined(__EMSCRIPTEN_MINOR__)
#  if !defined(__EMSCRIPTEN_minor__)
#    error "Emscripten minor version macro is unavailable"
#  endif
#  define __EMSCRIPTEN_MINOR__ __EMSCRIPTEN_minor__
#endif
#if !defined(__EMSCRIPTEN_TINY__)
#  if !defined(__EMSCRIPTEN_tiny__)
#    error "Emscripten patch version macro is unavailable"
#  endif
#  define __EMSCRIPTEN_TINY__ __EMSCRIPTEN_tiny__
#endif
