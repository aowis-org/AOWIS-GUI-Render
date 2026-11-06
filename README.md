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
./git_submodule_init.sh
./compile_linux.sh           # Release: build/link all production Render libraries
./compile_linux_tests.sh     # Release: build/link all targets first; only then CTest
# Both accept --debug; AOWIS_BUILD_JOBS=8 and AOWIS_LINUX_LINKER=lld|system.
```

The standalone Ninja build scripts use separate `build-linux` and
`build-linux-tests` directories; they never run a GUI program. The test script
**never runs CTest when configuration, compilation or linking fails**.
`bash tests/test_linux_compile_scripts.sh` checks that failure gate without
building native dependencies. The manual CMake equivalent is:

```sh
git submodule update --init --recursive   # or ./git_submodule_init.sh
cmake -S . -B build-linux-manual-tests -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DAOWIS_RENDER_BUILD_GEOMETRY_TESTS=ON
cmake --build build-linux-manual-tests -j 8 && \
  ctest --test-dir build-linux-manual-tests --output-on-failure
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

RenderGlobe owns retained Globe scene stream invalidation decisions through
`MapGlobeSceneSync`. The GUI maps Qt/hydraulic symbology and selection state to
neutral change flags and executes the resulting CPU rebuild plan. Visual-only
snapshot revisions, camera movement and flow time steps do not rebuild ECEF
network geometry; full geometry rebuilds subsume dependent stream updates.
The Qt-free tests exercise the same compiled production planner and its
production-library linkage. Retained snapshot/index storage, ECEF anchoring,
terrain lookup and UI interactions remain in GUI.

Globe scene preparation is now being separated incrementally: RenderGlobe
owns curved-link ECEF tessellation, six-vertex ribbon topology and tangent
width directions, while the GUI supplies GeographicLib/geodetic sampling,
terrain heights and shader vertex coloration. RenderGlobe also
owns CPU flow-chevron placement (including both orientations, WGS84 surface
normal, and model tangent basis), while GUI supplies hydraulic entity IDs,
scene paths and shader-vertex adaptation. Mock-backend and real-Qt geometry
regressions protect the 3D flow and model orientation math.

The first 3D extraction moves production tank and reservoir mesh generation,
vertex/instance layouts, vertical transform, and globe surface vertex math
into **`AOWIS::RenderGlobe`** (Qt6 Gui; no bgfx, GUI model, or platform host
requirements). GUI continues supplying the existing `:/map-3d/*_albedo.png`
texture resources through a small host adapter. Render now owns globe GPU
draw submission, dynamic buffers, shader/uniform resources, and GPU model
textures. Camera/model synchronization and terrain scene preparation remain in GUI;
RenderCore owns the production globe vertex layouts.

### Globe-network programs, uniforms and textures

`MapBgfxGlobeResourcePool` is bgfx-only. It creates and owns eight linked
shader programs (the shared backend supplies a loader), six frame uniforms,
two samplers and two GPU material textures. The host passes tank/reservoir
images; `mapBgfxGlobeMipTexture()` in Render converts `QImage` into the
existing smooth, premultiplied RGBA8 mip chain. GPU upload uses the same
clamped sampler flags and same `createTexture2D()` format as before. The
shared icon atlas remains externally owned; it is never destroyed here.

Initialization rolls back partially created handles; `release()` is
idempotent and must run before bgfx device shutdown. The pool populates the
named program/uniform/texture slots consumed by `MapBgfxGlobeGpu`, without
including desktop-model headers. The real GPU-resource implementation is
compiled against a recording mock bgfx backend in the standalone Ninja
regressions, including shader failures, partial allocations, and restarts.

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
Solid, X-Ray and Hide pass ordering. `MapBgfxGlobeBufferPool` owns GPU
streams and `MapBgfxGlobeResourcePool` owns the eight shader programs,
frame uniforms, samplers, and tank/reservoir material textures. GUI provides
vertex layouts, scene data and material images, not GPU handle lifetimes.

### Globe-network GPU buffer ownership

`MapBgfxGlobeBufferPool` owns the thirteen dynamic vertex streams and the
optional depth-ordered icon index buffer. The desktop host supplies its
existing scene vertex layouts, vertex bytes, and icon ordering; Render
allocates, reuses, uploads, and explicitly destroys the bgfx buffers. Empty
streams preserve their allocation but report a zero draw count. `release()`
must run before device teardown, and the same instance can upload again
after device reinitialization. The shared `MapBgfxGlobeResourcePool` owns
shader programs, uniforms and model-material textures.

### Globe vertex-layout ownership

`MapBgfxGlobeVertexLayouts` lives in `AOWIS::RenderCore` and defines all five
production globe-network shader attribute layouts (link, node, icon, junction,
model). The GUI supplies only the byte strides of its existing scene vertex
structs. Render validates their strides before GPU allocation; GUI maintains
compile-time offset checks for the shader-facing float prefixes. No GUI model
headers are used by Render, and no second bgfx device is created.

### Globe terrain-aware X-Ray classification

`MapGlobeUndergroundGeometry` owns the X-Ray link subdivision policy (20 m
fallback, half the smallest resolved terrain-cell size, 0.25 m minimum,
128-subdivision cap) and merges classification samples into contiguous buried
runs before GPU tessellation. This preserves dashed-stroke continuity. The
host supplies its terrain queries, WGS84 geodesic coordinates and vertical
transforms; no hydraulic MODEL or GeographicLib dependency enters Render.

### Globe terrain topology and GPU index buffers

`MapGlobeTileTopology` (`map_globe_tile_topology.h`) supplies the production
64-cell DEM grid/index ABI and its four crack-masking skirt walls. The single
process-lifetime, thread-safe index cache is independent of tile providers and
terrain requests. `MapBgfxGlobeTileIndexPool` owns one bgfx 16-bit index
buffer per used cell count and reuses it across all window tiles. The host
continues to prepare vertex positions (including DEM sampling, normals and
vertical exaggeration); invoke `release()` before bgfx device shutdown.

### Terrain GPU vertex streams

`MapBgfxGlobeTerrainBufferPool` owns the globe surface window, cap and wireframe
dynamic vertex buffers and the caps' 32-bit index buffer. A host supplies
vertex layouts and contiguous CPU data, while the pool performs full uploads
and **range-only** DEM mesh updates through `patchWindow`. Retained-window
counts and strides are checked before patching; failed updates request full
reupload in the host. `release()` is required before bgfx device shutdown.
The immutable per-resolution terrain/skirt index buffers remain in
`MapBgfxGlobeTileIndexPool`.

### Globe terrain GPU draw ownership

`MapBgfxGlobeTerrainGpu` (`AOWIS::RenderCore`) owns the base globe terrain
and wireframe shader programs, seven shared uniforms/samplers, two RGBA8
fallback textures, and cap/tile/wireframe bgfx submission. It consumes only
neutral bgfx handles and a five-vector frame uniform block. The host still
prepares terrain tiles, DEM vertex patches, atlas rectangles, sky geometry,
heatmap atlas baking and view ordering. Heatmap-composited terrain draws are
submitted by Render with host-supplied heatmap program/texture/uniform handles.
Release on the owning rendering thread before bgfx shutdown. The production
implementation has a recording-bgfx mock test covering draw order, resource
lifecycle, validation, and failure cleanup.

### Globe sky and heatmap GPU resources

`MapBgfxGlobeSkyGpu` and `MapBgfxGlobeHeatmapGpu` own the optional
atmosphere and heatmap shader/uniform resources and draw submission. Sky uses a
single static full-screen triangle. Heatmap atlas pages (four 2048² render
textures, allocated on demand) and the transient clear/stamp passes belong to
Render. The host supplies camera vectors and stamp vertices, chooses tile
cells, tracks LRU/revisions, and maintains the 24-bakes-per-frame policy.
Both owners support complete release/reinitialization on bgfx device changes.
