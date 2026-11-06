# AOWIS-GUI-Render

Shared bgfx renderer components for AOWIS-SERVER-GUI (Qt Widgets/WebAssembly)
and AOWIS-GUI-Mobile (Qt Quick/Android/Linux).

## Standalone Linux build (Ninja)

Render owns its dependencies. For an existing GUI checkout, register exactly
that GUI's **pinned** bgfx/bimg/bx revisions once in the Render repository:

```sh
./tools/bootstrap_vendor_submodules.sh ../AOWIS-SERVER-GUI
# Review the staged .gitmodules and 160000 gitlinks, then commit and push them.
```

All subsequent Render clones work without any GUI checkout:

```sh
git submodule update --init --recursive   # or ./git_submodule_init.sh
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j 8
ctest --test-dir build-linux --output-on-failure
```

Requires CMake 3.21+, Ninja, C++20, Qt6 Core and Gui development packages,
and the native libraries required by the enabled bgfx backends. This builds
`RenderGeometry`, `RenderNetwork`, `RenderGlobe`, `RenderDevice`, `RenderCore`, the selected
shader resources and standalone tests. The core-link test checks shader
registration and device linkage without a display or GPU.

A parent such as GUI can still provide its own **single** `bgfx` CMake target
and `AOWIS_RENDER_VENDOR_ROOT`, keeping its pinned sources and avoiding a
second runtime. The standalone Render vendoring is not compiled in that case.

For geometry-only builds without Qt or vendor checkouts, explicitly disable
`AOWIS_RENDER_BUILD_DEVICE`, `AOWIS_RENDER_BUILD_MAP_CORE` and
`AOWIS_RENDER_BUILD_NETWORK_SCENE`.

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

## Production geometry shared between desktop and Mobile

- `AOWIS::RenderGeometry` is a **Qt-, bgfx-, and model-free** header-only
  target: continuous Web Mercator projection, secondary-view camera, globe
  icon spatial indexing, terrain-height query cache, and globe imagery tile
  fallback UV coordinates. The implementation is moved from GUI, not copied.
- `MapBgfxPanReuse` is now in RenderCore because its screen-space HUD reuse
  types use Qt Core. GUI remains its host and controls pan/content invalidation.
- Configure and run the standalone geometry regressions without any Qt/bgfx
  checkout:

  ```sh
  cmake -S . -B build-geometry -DAOWIS_RENDER_BUILD_DEVICE=OFF \
      -DAOWIS_RENDER_BUILD_MAP_CORE=OFF -DAOWIS_RENDER_BUILD_GEOMETRY_TESTS=ON
  cmake --build build-geometry -j 8
  ctest --test-dir build-geometry --output-on-failure
  ```

The production 2D network scene, mesh staging and GPU submission are in Render; GUI retains its hydraulic snapshot adapter and user-interaction logic.

The first 3D extraction moves production tank and reservoir mesh generation,
vertex/instance layouts, vertical transform, and globe surface vertex math
into **`AOWIS::RenderGlobe`** (Qt6 Gui; no bgfx, GUI model, or platform host
requirements). GUI continues supplying the existing `:/map-3d/*_albedo.png`
texture resources through a small host adapter. Globe GPU draw submission,
camera/model synchronization, terrain scene preparation, and GPU textures
remain in GUI for subsequent extraction steps.

## Host integration

The GUI sets `AOWIS_RENDER_BUILD_MAP_CORE=ON`, provides its pinned upstream
source root and links `AOWIS::RenderCore`. Its Qt Widgets/QWindow adapter,
map controller, map/editor workflow and Projects UI stay in the GUI.

Mobile builds its existing pinned bgfx.cmake target, sets
`AOWIS_RENDER_BUILD_MAP_CORE=OFF`, links `AOWIS::RenderDevice`, and initializes
and shuts down its borrowed Qt Quick EGL context through the shared device
manager. It intentionally retains `bgfx::frame(BGFX_FRAME_FLUSH)` and Qt Quick
scene-graph ownership. No second bgfx runtime is added to either executable.

Both hosts pin `external/AOWIS-GUI-Render` as a Git submodule. Commit Render
changes before advancing either host gitlink, then build against the newly
pinned revision. No implicit sibling checkout selection.

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

The library boundary, device lifecycle, camera/projection/terrain geometry,
production 2D network scene/mesh/draw submission, GPU uploaders and shader assets
have been moved to this repository. The larger GUI-specific `MapBgfxSurface`,
HUD text content/controller, 3D scene preparation, input routing and Projects
DAG presentation have *not* been extracted. Moving them verbatim
would introduce GUI services, widgets and application model dependencies into
Mobile. A later step should isolate backend-neutral scene inputs and rendering
from their GUI host, then replace Mobile's test geometry with that interface.

Use `./tools/apply_patch.sh` for incremental ZIP patches and
`./tests/test_patch_tooling.sh` to validate ZIP patch tooling.

### 3D globe-network GPU submission

`MapBgfxGlobeGpu` (in `AOWIS::RenderCore`) owns the production globe
network's pass order, bgfx vertex/index binding, per-draw uniforms,
shader-compatible style binding, and GPU submissions. It does not access GUI
hydraulic types, map widgets, camera controllers, or network snapshots.
`MapBgfxGlobeDrawPass` is a Qt/bgfx-free, regression-tested description of
Solid, X-Ray and Hide pass ordering. The GUI currently supplies buffer,
program, texture and uniform handles and owns their lifetime; migrating
resource allocation/scene synchronization is a separate later step.

### Globe-network GPU buffer ownership

`MapBgfxGlobeBufferPool` owns the thirteen dynamic vertex streams and the
optional depth-ordered icon index buffer. The desktop host supplies its
existing scene vertex layouts, vertex bytes, and icon ordering; Render
allocates, reuses, uploads, and explicitly destroys the bgfx buffers. Empty
streams preserve their allocation but report a zero draw count. `release()`
must run before device teardown, and the same instance can upload again
after device reinitialization. Programs, uniforms, and model-material textures
remain a separate extraction step.
