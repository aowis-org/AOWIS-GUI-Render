# AOWIS-GUI-Render

Shared bgfx renderer components for AOWIS-SERVER-GUI (Qt Widgets/WebAssembly)
and AOWIS-GUI-Mobile (Qt Quick/Android/Linux).

## Current libraries

- `AOWIS::RenderDevice` / `aowis-render-device`: Qt-independent, process-wide
  bgfx initialization and retirement, native surface descriptors, multi-view
  reservations and swapchain target registry. Both hosts use this library.
- `AOWIS::RenderCore` / `aowis-render-core`: desktop map GPU uploaders,
  imagery atlas, HUD text/texture cache and embedded map shader resources.
  Built when `AOWIS_RENDER_BUILD_MAP_CORE=ON`; used by the GUI.
- Pinned bx/bimg/bgfx can be **provided by the parent CMake project** as one
  `bgfx` target (Mobile), or built through `cmake/aowis_bgfx.cmake` with
  `AOWIS_RENDER_VENDOR_ROOT` containing `bx/`, `bimg/` and `bgfx/` (GUI).
  No source downloads occur during configure.

All original map shader binaries are preserved, including GL, GLES/WebGL 2,
Vulkan, D3D11 and Metal families. Qt resource paths remain
`:/aowis/map/bgfx/<family>/<shader>.bin` and are registered by the library.

## Host integration

The GUI sets `AOWIS_RENDER_BUILD_MAP_CORE=ON`, provides its pinned upstream
source root and links `AOWIS::RenderCore`. Its Qt Widgets/QWindow adapter,
map controller, map/editor workflow and Projects UI stay in the GUI.

Mobile builds its existing pinned bgfx.cmake target, sets
`AOWIS_RENDER_BUILD_MAP_CORE=OFF`, links `AOWIS::RenderDevice`, and initializes
and shuts down its borrowed Qt Quick EGL context through the shared device
manager. It intentionally retains `bgfx::frame(BGFX_FRAME_FLUSH)` and Qt Quick
scene-graph ownership. No second bgfx runtime is added to either executable.

A host may use `external/AOWIS-GUI-Render` as a Git submodule, or (for local
development) keep `AOWIS-GUI-Render` alongside its GUI/Mobile checkout.

## Safety contracts

- One bgfx device per process; never instantiate a second `bgfx` target.
- Only the owning render thread may invoke bgfx; on Mobile it is the Qt Quick
  scene-graph render thread; on desktop it is the map/GUI render thread.
- Retire device clients and GPU resources before destroying native surfaces.
- Keep the D24S8 depth attachment for map swapchains/resizes: without it the
  terrain skirts overdraw nearer terrain.
- Shader regeneration (`-DAOWIS_BGFX_SHADER_TOOLS=ON`) is host-only and is not
  required by normal builds.

## Extraction status

The library boundary, device lifecycle, map GPU uploader and shader assets
have been moved to this repository. The larger GUI-specific `MapBgfxSurface`,
HUD text content/controller, 2D/3D scene preparation, input routing and
Projects DAG presentation have *not* been extracted. Moving them verbatim
would introduce GUI services, widgets and application model dependencies into
Mobile. A later step should isolate backend-neutral scene inputs and rendering
from their GUI host, then replace Mobile's test geometry with that interface.

Use `./tools/apply_patch.sh` for incremental ZIP patches and
`./tests/test_patch_tooling.sh` to validate ZIP patch tooling.
