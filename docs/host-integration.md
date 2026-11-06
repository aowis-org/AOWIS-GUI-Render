# Integrating the shared renderer

This is the current host contract, not a promise that all production Maps
surfaces are available through a single engine object yet.

## Hosts and build targets

| Contract | AOWIS-SERVER-GUI | AOWIS-GUI-Mobile |
| --- | --- | --- |
| Toolkit | Qt Widgets / native `QWindow`; WebAssembly canvas hosted by GUI | Qt Quick / Android EGL |
| Shared device | `AOWIS::RenderDevice` | `AOWIS::RenderDevice` |
| Geodesy, terrain data, view state | `AOWIS::RenderGeo`, `AOWIS::RenderTerrainData`, `AOWIS::RenderView` | Same targets; implement `MapImageryProviderDirectory` for `MapModel` |
| GeographicLib, MODEL | GUI's submodules (added before Render) | Mobile's submodules (add before Render) |
| Shared geometry | `AOWIS::RenderGeometry`, plus production `RenderNetwork`/`RenderGlobe` used by GUI | Low-level geometry/device integration; production globe/terrain integration deferred |
| GPU library | `AOWIS::RenderCore` | Production Map GPU core not yet integrated |
| bgfx source | Host-supplied pinned bx/bimg/bgfx target via Render | Pinned bgfx.cmake target, supplied by Mobile |
| Native surface | GUI `BgfxQtNativeSurface` adaptor | Qt Quick borrowed EGL context |
| Present | `BgfxFrameCoordinator` coordinates all registered native clients | Qt Quick owns swap; borrowed-context path uses `bgfx::frame(BGFX_FRAME_FLUSH)` |
| Map view | `AOWIS::RenderMap` `MapBgfxMapView`, hosted by GUI `MapBgfxSurface` (`MapBgfxMapViewHost`) | Same view; Qt Quick host item to be written |

## Hosting the map view

`MapBgfxMapView` (`AOWIS::RenderMap`) is the complete map: 2D and globe,
imagery, terrain, network, labels, HUD, picking and terrain queries. It is a
`QObject`; the host supplies everything toolkit-specific:

- implement `MapBgfxMapViewHost`: `mapViewVisible()`, `mapViewFont()`,
  `mapViewPalette()`, `mapViewFocusNavigationInput()`; on WASM also the
  area's logical size, device pixel ratio, screen, global position and
  cursor;
- embed `nativeInputWindow()` (desktop; the view deletes it if no host ever
  reparented it);
- forward events: `handleHostShown()`, `handleHostHidden()`,
  `handleHostResized()`, `handleHostChange(type)`, `handleKeyPress()`,
  `handleKeyRelease()`, `handleFocusOut()`; keep focus on another receiver
  with `setNativeWindowAcceptsFocus(false)`;
- inject `setNativeSurfaceCapture()` (Qt private native handles),
  `setTerrainDetailReductionSource()`, `setGlobeModelImages()`,
  `setHudImages()`, `setHudNavigationKeys()`, `setHudScaleProvider()` and
  `setGlobeHudStateProvider()`;
- the WASM page provides `window.aowisBgfxMap` (the browser canvas bridge).

The GUI pins Render at `external/AOWIS-GUI-Render`. To advance a GUI checkout
**after publishing Render**:

```sh
cd ~/coding/aowis/AOWIS-SERVER-GUI
./git_submodule_update.sh --render
./tools/verify_render_checkout.sh
./compile_linux_tests.sh
```

The GUI's test entry point builds the full application and all tests before
CTest. Its `AOWIS_RENDER_SOURCE_DIR` override is for explicit development
only; no sibling repository is selected implicitly. CMake must be regenerated
when Render adds production source files.

A standalone Render checkout uses:

```sh
./git_submodule_init.sh
./compile_linux.sh
./compile_linux_tests.sh
```

A no-Qt geometry-only configuration (without bgfx vendor sources) is supported:

```sh
cmake -S . -B build-geometry -G Ninja \
  -DAOWIS_RENDER_BUILD_DEVICE=OFF \
  -DAOWIS_RENDER_BUILD_MAP_CORE=OFF \
  -DAOWIS_RENDER_BUILD_NETWORK_SCENE=OFF \
  -DAOWIS_RENDER_BUILD_GLOBE_MODELS=OFF \
  -DAOWIS_RENDER_BUILD_GEOMETRY_TESTS=ON
cmake --build build-geometry -j 8 && \
  ctest --test-dir build-geometry --output-on-failure
```

This covers pure geometry and recording-mock tests, **not** the Qt/bgfx
production libraries. For production, `RenderCore` transitively requires
`RenderNetwork`, `RenderGlobe` and `RenderDevice`; the normal standalone
scripts enable them.

## Neutral network inputs and GPU responsibilities

- `MapBgfx2dNetworkInput` contains renderer IDs, class codes and WGS84
  geometry, not hydraulic MODEL objects. GUI converts its retained network
  snapshot through `map_bgfx_2d_snapshot_adapter.h`.
- `MapBgfx2dStyleBinding` maps host-owned symbology to the production shader
  binding. Camera or simulation updates should not rebuild network geometry.
- `MapGlobeSceneSync` computes which 3D CPU geometry streams must change;
  GUI owns the retained snapshot/index and executes rebuilds.
- `MapGlobeLinkGeometry`, `MapGlobeFlowGeometry` and
  `MapGlobeUndergroundGeometry` perform reusable tessellation/classification
  without network MODEL dependencies. GUI supplies geodesic samples, terrain
  queries, render IDs and GPU-vertex conversion.
- Globe network buffers/resources, terrain index and vertex pools, terrain
  draw commands, sky and heatmap GPU submissions live in Render.
- Tank and reservoir material images are currently supplied by GUI Qt
  resources (`:/map-3d/`); Render owns their GPU textures/mips.

The **remaining engine-neutral map pipeline**—DEM decoding, LOD selection,
terrain mesh scheduling, tile streaming, imagery lookup, shared caching and
terrain picking queries—is mapped in [Map-engine boundary](map-engine-boundary.md).
Do not introduce server/network or editor callbacks into GPU-only targets.

## GPU lifecycle and platform rules

- One `BgfxDeviceManager` / bgfx runtime per process. Register secondary
  clients before acquiring GPU resources or view/target reservations.
- The frame coordinator hands out **process-global**, generation-scoped view
  IDs and batches submissions before one native `bgfx::frame()`.
- Retire clients, GPU pools, view IDs and swapchains before the owning surface
  is destroyed. A stale device generation invalidates handles and view IDs.
- `BgfxNativeSurface` is toolkit-neutral; GUI captures the native display,
  window, type and physical size. A secondary target resizes independently;
  do not reset the whole device for an ordinary secondary resize.
- Use D24S8 for 3D primary/secondary targets. Missing depth causes terrain
  skirts to overdraw near terrain; skirts themselves remain needed for seams.
- The Mobile Qt Quick scene graph controls context and swap, and is not
  entitled to a second bgfx device.
- Keep embedded shader paths `:/aowis/map/bgfx/<family>/<shader>.bin` stable.
  Normal builds use committed shaders; `AOWIS_BGFX_SHADER_TOOLS=ON` is an
  explicit shader-regeneration option, not a build prerequisite.

## Practical verification

1. Run both standalone Render build scripts; never run CTest after a failed
   build. Include the production-library link test, not only mock tests.
2. Run GUI's `./compile_linux_tests.sh`: application **and** tests compile and
   link before CTest. Also run the extraction boundary and checkout verifier.
3. Exercise native OpenGL and Vulkan: 2D/3D switching, terrain LOD/skirt
   boundaries, cap/wireframe, X-Ray, models, sky, heatmap, tile replacement,
   GPU teardown/recreation, resize and input-focus loss.
4. Qualify WebAssembly and Android independently; passing Linux tests does
   not establish their GPU/runtime behavior.

The GUI's `docs/rendering-architecture.md` (in the separate
`AOWIS-SERVER-GUI` repository) documents the single shared Maps viewport,
Projects graph and host event routing.
