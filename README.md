# AOWIS-GUI-Render

Shared map-rendering code for AOWIS. This is an **independently buildable
repository**, consumed as a pinned Git submodule by the desktop GUI. It provides
production 2D network and 3D globe GPU components, geometry, and the shared
bgfx device lifecycle. It is **not yet a complete, host-independent map engine**:
terrain preparation, network and imagery repositories, and frame orchestration
still have desktop-GUI owners.

The planned consumers are `AOWIS-SERVER-GUI` (Qt Widgets, native Linux and
WebAssembly) and `AOWIS-GUI-Mobile` (Qt Quick/Android). The Mobile renderer
currently uses the shared low-level device/geometry contracts, **not** the
production desktop globe scene. Mobile integration is deliberately deferred
until the shared engine boundary is stable.

## Build on Linux

A fresh clone needs its pinned `bx`, `bimg`, `bgfx`, `GeographicLib` and
`AOWIS-SERVER-MODEL` submodules:

```sh
./git_submodule_init.sh
./compile_linux.sh
./compile_linux_tests.sh
```

- `compile_linux.sh`: configure and build production libraries with Ninja;
  **does not launch an application or run CTest**.
- `compile_linux_tests.sh`: configure and build **all library and test targets
  first**, then run CTest only after successful compilation and linking. A
  failed build cannot produce a false pass from stale test executables.
- Both accept `--debug` or `--release` (default). They use separate
  `build-linux` and `build-linux-tests` directories.
- `AOWIS_BUILD_JOBS=8` selects build parallelism; `AOWIS_LINUX_LINKER=lld`
  (default) or `system` selects the linker. LLD must be installed for the
  default configuration.

Required: CMake 3.21+, Ninja, a C++20 toolchain, Qt 6 Core/Gui development
packages, Python 3 (MODEL's schema generator), and the graphics dependencies
of the selected bgfx backends.
No shader tool download is needed for normal builds; compiled shader resources
are included. For the geometry-only, no-Qt build configuration, see
[Build and host integration](docs/host-integration.md).

Vendor gitlinks are registered once with
`./tools/bootstrap_vendor_submodules.sh ../AOWIS-SERVER-GUI`, which copies the
GUI's pinned revision of each dependency and leaves already registered ones
alone (so it also adds dependencies introduced later, such as GeographicLib
and MODEL). Ordinary clones and builds should **not** run that bootstrap
step. No dependency is fetched during CMake configuration.

`compile_linux_tests.sh` runs the tests labelled `aowis` only: a standalone
build also contains GeographicLib's own CTest entries for tools it never
builds.

## Public CMake targets

| Target | Runtime dependencies | Responsibility |
| --- | --- | --- |
| `AOWIS::RenderGeometry` | C++20 only | Projections, camera helpers, icon index, terrain-height cache, imagery UV fallback, draw-plan contracts |
| `AOWIS::RenderGeo` | Qt6 Gui, MODEL (`aowis/model/gis.h`), GeographicLib (private) | WGS84 ellipsoid/ECEF, orbit camera basis, screen rays, Web Mercator tile math, UTM and project-local metric coordinates |
| `AOWIS::RenderView` | Qt6 Gui, RenderGeo | `MapModel` (2D continuous zoom, globe orbit rig, screen <-> WGS84, imagery tile addressing through the host's `MapImageryProviderDirectory`), camera view state, globe picking, render cache math, render diagnostics |
| `AOWIS::RenderGlobeTerrain` | Qt6 Gui, RenderView, RenderTerrainData, RenderGlobe | Globe terrain CPU pipeline: quadtree selection/LOD, surface preparation, mesh scheduler, terrain navigation, globe camera, heatmap scene; tiles from the host's `MapTerrainTileSource` |
| `AOWIS::RenderMapData` | Qt6 Gui, RenderView, RenderTerrainData | Imagery and DEM tile repositories (scheduling, caches, decode workers, WASM browser decoder, dataset catalog, cache deletion) over the `InterfaceServerMap` contract; the host supplies a `MapServerTransport` |
| `AOWIS::RenderGlobeNetwork` | Qt6 Gui, RenderGlobeTerrain, RenderNetwork, GeographicLib (private) | Network render contract (snapshot, symbology settings and ramps, entity kinds) and the globe network CPU side: retained globe network scene, render frame, icon atlas, resolved symbology data |
| `AOWIS::RenderMap` | RenderCore, RenderGlobeNetwork, RenderMapData | bgfx map renderers: GPU network colouring, globe network and terrain surfaces, 2D symbology / picking, labels, native HUD (layout, input, legend) |
| `AOWIS::RenderTerrainData` | Qt6 Core | Normalized 65×65 DEM tile decoder (v1 format), tile keys/endpoints, terrain dataset catalog |
| `AOWIS::RenderNetwork` | Qt6 Core, RenderGeometry | Production 2D network scene variants and CPU mesh staging |
| `AOWIS::RenderGlobe` | Qt6 Gui, RenderGeometry | 3D network/model/flow/link/underground geometry, terrain topology, scene invalidation policy |
| `AOWIS::RenderDevice` | bgfx and RenderGeometry | Device generation, client retirement, view/frame coordination and swapchains |
| `AOWIS::RenderCore` | Qt6 Gui, bgfx, other Render targets | Production 2D and 3D GPU uploads/draws, texture atlases, buffers, shader resources and HUD cache |

`RenderCore` requires the device, 2D scene and globe targets. Render may build
its own pinned bgfx target, or a parent can supply **one existing `bgfx`
CMake target**. It must never create a second bgfx runtime in the process.
GeographicLib and MODEL follow the same rule: a host that added
`GeographicLib_STATIC` / `AOWIS::Model` before Render keeps ownership, and
Render's submodules are used only when those targets do not exist
(`cmake/aowis_render_dependencies.cmake`).
Normal GUI builds embed Render with `AOWIS_RENDER_BUILD_MAP_CORE=ON`.

All committed shader families use the existing resource convention
`:/aowis/map/bgfx/<family>/<shader>.bin`. `MapBgfxBackend` initializes the
registered resources without source-level dependency on GUI resource files.

## What Render currently owns

**Geodesy and terrain data**

- `GeoWgs84Ellipsoid`, `GeoWebMercator`, `GeoMetricProjection` (`src/geo/`).
- `MapTerrainTile` decoding and `MapTerrainDatasetCatalog` parsing
  (`src/map/data/`); requests, caching and transport stay with the host.

**View state**

- `MapModel` with `MapViewMode` / `MapProvider` (`map/core/map_view_types.h`).
  Host services are injected: the provider catalog as a
  `MapImageryProviderDirectory` (change notifications via
  `providerCatalogChanged()` / `providerFallbackChanged()`), the orbit
  sensitivity as a getter (`setOrbitSensitivitySource()`).

**bgfx map renderers**

- `MapBgfxEntityStyle`, `MapBgfxGlobeNetwork` (model textures from the host:
  `setModelImages()`), `MapBgfxGlobeSurface` (detail setting from the host:
  `setTerrainDetailReductionSource()`), 2D symbology / picking / snapshot
  adapter, `MapBgfxLabels`, `MapBgfxHud`.
- `MapBgfxMapView` (`map/bgfx/map_bgfx_map_view.h`): the map view that owns
  them — frame loop, device lifecycle, native QWindow and its event routing,
  picking, terrain queries, HUD. A `QObject`, no Qt Widgets. The host
  implements `MapBgfxMapViewHost` (visibility, font, palette, focus; WASM:
  size, ratio, cursor), embeds `nativeInputWindow()`, forwards its events
  (`handleHostShown()`, `handleKeyPress()`, ...) and injects native handle
  capture, terrain detail, model textures, HUD images, navigation keys, the
  scale bar and the globe HUD state. The GUI's `MapBgfxSurface` is that host
  for Qt Widgets.

**Network render contract**

- `network/network_render_snapshot.h`, `network/network_symbology*.h`,
  `network/infrastructure_entity_traits.h`, `map/core/map_network_types.h`.
  The host builds the snapshot and fills `MapNetworkRenderSymbology` from its
  own model and results; Render never sees hydraulic data.
- `MapGlobeNetworkScene`, `MapGlobeRenderFrame`, the icon atlas (drawn in
  code, no host resources).

**Map data**

- `MapTileRepository`, `MapTerrainRepository` (`src/map/data/`) and the map
  server transport contract `InterfaceServerMap` (`src/services/`). The host
  creates the concrete transport (REST, in-process server) through
  `MapServerTransport` and forwards provider-catalog changes to
  `MapTileRepository::invalidateProviderCatalog()`.

**Globe terrain pipeline**

- `MapGlobeSurfaceScene`, `MapGlobeSurfacePreparation`,
  `MapTerrainMeshScheduler`, `MapGlobeTerrainNavigation`, `MapGlobeCamera`,
  `MapGlobeHeatmapScene`. Decoded tiles come from the host's
  `MapTerrainTileSource`; the host forwards tile arrivals and injects the
  terrain detail setting (`setTerrainDetailReductionSource()`).

**Device and 2D**

- Process-wide bgfx device/target management: `BgfxDeviceManager`,
  `BgfxFrameCoordinator`, `BgfxNativeTargetRegistry`, `BgfxNativeSurface`.
- Web Mercator/camera math, 2D network scene/mesh and 2D GPU submission.
- 2D imagery atlas/upload support, style-binding interface, HUD texture/text
  cache and shader resources.

**3D hydraulic network**

- Tank/reservoir model geometry and tangent basis; curved globe-link ribbons,
  flow chevrons and terrain-aware buried-run classification.
- CPU stream invalidation decisions (`MapGlobeSceneSync`); GUI still retains
  the authoritative scene snapshot and decides when to execute the plan.
- Globe draw ordering and Solid/X-Ray/Hide submissions; the thirteen dynamic
  vertex streams, optional icon-order index stream, shader/uniform/model
  texture pools and the five production vertex layouts.

**3D terrain and overlays**

- Shared tile-grid indices, skirt geometry and index-buffer pool
  (`MapGlobeTileTopology`, `MapBgfxGlobeTileIndexPool`).
- Window/cap/wireframe GPU vertex buffers and range-only DEM patch uploads
  (`MapBgfxGlobeTerrainBufferPool`).
- Terrain/cap/wireframe GPU programs and draw submission
  (`MapBgfxGlobeTerrainGpu`); sky and heatmap GPU resources/submissions
  (`MapBgfxGlobeSkyGpu`, `MapBgfxGlobeHeatmapGpu`).
- Shared geometry helpers for icon placement, height caching and imagery
  ancestor-tile UV fallback. **These helpers are not yet the full terrain
  preparation, tile-streaming, or imagery-provider pipeline.**

For the data path, remaining GUI owners and exact future boundary, read
[Map-engine ownership and data pipeline](docs/map-engine-boundary.md). For
usage from a host, see [Build and host integration](docs/host-integration.md).

## What stays with the GUI today

The GUI's `MapBgfxSurface` hosts `MapBgfxMapView` in Qt Widgets and supplies
its settings and assets. The GUI also owns `MapGlobeSurfaceScene` (quadtree/LOD),
`MapGlobeSurfacePreparation`, `MapTerrainMeshScheduler`, decoded terrain and
imagery repositories, provider configuration and upstream request adapters.
The GUI supplies GeographicLib/geodesic sampling, height data, network
snapshot/IDs, picking and editor interaction, and the Qt/native/WebAssembly
surface adapters. Moving these classes verbatim would introduce app services
and `AOWIS-SERVER-MODEL` dependencies into Render; their engine-neutral
algorithms and their host adapters must be separated first.

Backend-specific notes and renderer-owned versus host-owned resources are in
[Host integration](docs/host-integration.md). The remaining **forward work**
is in [Renderer roadmap](docs/roadmap.md); it is not a record of historic patch
stages.

## Runtime contracts

1. **Exactly one bgfx device per process.** A new view does not call
   `bgfx::init()` again. Device generation tokens, reserved view IDs and
   client retirement protect shared resources.
2. **GPU calls run on the owning render thread.** GUI and Mobile use different
   toolkit lifecycle adapters; Mobile's Qt Quick scene graph retains control
   of presentation.
3. **Retire resources before native surface/device teardown.** Each GPU pool
   releases its handles on the owning render thread; a restarted device needs
   newly allocated handles.
4. **Keep a D24S8 depth attachment** for primary/secondary 3D swapchains.
   Terrain skirts conceal LOD seams but must not overdraw foreground terrain.
5. **Keep geometry identity and elevation data in the host's domain model.**
   No private hydraulic elevation model or duplicate editor state is
   introduced by the renderer.
6. **Do not confuse compilation with runtime acceptance.** GPU mock tests
   validate submission and cleanup, but OpenGL/Vulkan/WebGL/Android visual
   behavior and native surface restoration still require device testing.

## Verification and change workflow

Render's test scripts validate production-source geometry and GPU code (with
recording bgfx/Qt shims where appropriate), real-Qt library linkage, and
shader resource registration. The desktop GUI's `./compile_linux_tests.sh`
adds **full application compilation and linking before CTest**, so a failed
GUI target cannot be hidden by green test binaries.

For a two-repository change:

1. Apply and build the Render patch; run its tests.
2. Commit and push the **actual Render Git tree**, checking newly added
   `.cpp` and `.h` files are tracked (a source ZIP alone is not a Git commit).
3. Update GUI's pinned `external/AOWIS-GUI-Render` gitlink, apply its adapter
   patch and run its full build-gated test script.
4. Exercise affected map features and both native backends where possible;
   only then commit the GUI integration.

`./tools/apply_patch.sh` accepts changed-file ZIP patches. Keep patch ZIPs
limited to modified or added files, with fresh timestamps and repository-root
paths. No compiled build directories belong in Git.

## Documentation

- [Map-engine ownership and data pipeline](docs/map-engine-boundary.md)
- [Host build, linking and GPU lifecycle](docs/host-integration.md)
- [Remaining renderer roadmap and acceptance gates](docs/roadmap.md)
