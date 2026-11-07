# AOWIS-GUI-Render

The AOWIS map engine. It draws the 2D map and the 3D terrain globe, with
imagery, terrain, the hydraulic network, labels, the native HUD and picking.
It also loads tiles and terrain from aowis-server-map. Every source is
backend-neutral C++20/Qt; the GPU side is bgfx.

It is an **independently buildable repository**, consumed as a pinned Git
submodule (`external/AOWIS-GUI-Render`) by:

| Host | Toolkit | How it hosts the map |
| --- | --- | --- |
| AOWIS-SERVER-GUI | Qt Widgets: native Linux/Windows/macOS, WebAssembly | `MapBgfxMapView` in its own native window (`RenderTarget::NativeWindow`), embedded by the GUI's `MapBgfxSurface` |
| AOWIS-GUI-Mobile | Qt Quick: Android, Linux desktop | `MapBgfxMapView` inside Qt Quick's OpenGL / OpenGL ES scene graph (`RenderTarget::HostContext`), hosted by Mobile's `MobileMapView` item |

Both hosts get the same map. A host supplies only what is toolkit- or
application-specific: its window or graphics context, input, settings,
assets and (GUI) the hydraulic network. Render never sees hydraulic data
or application settings.

## Build on Linux

A fresh clone needs its pinned `bx`, `bimg`, `bgfx`, `GeographicLib` and
`AOWIS-SERVER-MODEL` submodules:

```sh
./git_submodule_init.sh
./compile_linux.sh
./compile_linux_tests.sh
```

- `compile_linux.sh` builds the libraries with Ninja (`build-linux`). It does
  not run tests.
- `compile_linux_tests.sh` builds **every library and test target first**
  and runs CTest only after the build succeeded (`build-linux-tests`). A
  failed build can never pass with stale test executables. It runs the
  tests labelled `aowis`: a standalone build also registers GeographicLib's
  own CTest entries for tools it never builds.
- Both accept `--debug` or `--release` (default).
- `AOWIS_BUILD_JOBS=8` sets the parallelism; `AOWIS_LINUX_LINKER=lld`
  (default, LLD must be installed) or `system` selects the linker.

Requirements: CMake 3.21+, Ninja, a C++20 compiler, Qt 6 Core, Gui and
Network, Python 3 (MODEL's schema generator) and the graphics development files of the
selected bgfx renderers. On Linux, Qt's private Gui headers (Debian/Ubuntu
`qt6-base-private-dev`) are needed for Wayland and X11 native windows; without
them `BgfxQtNativeSurface` captures only the window ID. Compiled shaders are
committed, so a normal build needs no shader compiler.

No dependency is fetched during configuration. `tools/bootstrap_vendor_submodules.sh`
registers missing vendor gitlinks from a GUI checkout
(`./tools/bootstrap_vendor_submodules.sh ../AOWIS-SERVER-GUI`); ordinary clones
and builds never run it.

## Libraries

Each library is a CMake target with an `AOWIS_RENDER_BUILD_<NAME>` option.
A host switches on what it uses; the defaults build everything.

| Target | Depends on | Contents |
| --- | --- | --- |
| `AOWIS::RenderGeometry` | C++20 only (header-only) | 2D projection, three-view camera, globe icon index, height cache, imagery ancestor UVs, globe draw plan |
| `AOWIS::RenderGeo` | Qt Gui, MODEL (`aowis/model/gis.h`), GeographicLib (private) | WGS84 ellipsoid and ECEF, orbit camera basis, screen rays, Web Mercator tiles, UTM and project-local metric coordinates (`src/geo/`) |
| `AOWIS::RenderTerrainData` | Qt Core | 65×65 DEM tile decoder, tile keys and endpoints, terrain dataset catalog |
| `AOWIS::RenderView` | Qt Gui, RenderGeo | `MapModel` (continuous 2D zoom, globe orbit rig, screen ↔ WGS84, imagery tile addressing), camera view state, globe picking, render cache math, render diagnostics |
| `AOWIS::RenderMapData` | Qt Gui, RenderView, RenderTerrainData | `MapTileRepository` and `MapTerrainRepository`: request scheduling, caches, decode workers, browser decoder (WASM), dataset catalog, cache deletion; the transport contract `InterfaceServerMap` / `MapServerTransport` |
| `AOWIS::RenderMapRest` | Qt Network, RenderMapData | aowis-server-map client: `RESTClient`, `InterfaceServerMapREST`, `MapProviderCatalog` and fallback resolution, configured by `MapServerRestConfiguration` |
| `AOWIS::RenderNetwork` | Qt Core, RenderGeometry | 2D network scene, its two presentation variants and CPU mesh staging |
| `AOWIS::RenderGlobe` | Qt Gui, RenderGeometry | Globe network geometry: links, flow chevrons, underground runs, tank and reservoir models, terrain tile topology, scene invalidation policy |
| `AOWIS::RenderGlobeTerrain` | Qt Gui, RenderView, RenderTerrainData, RenderGlobe | Globe terrain pipeline: quadtree selection and LOD, surface preparation, terrain mesh scheduler, terrain-aware navigation, globe camera, heatmap scene |
| `AOWIS::RenderGlobeNetwork` | Qt Gui, RenderGlobeTerrain, RenderNetwork, GeographicLib (private) | Network render contract (snapshot, symbology settings and ramps, entity kinds), retained globe network scene, render frame, icon atlas |
| `AOWIS::RenderDevice` | bgfx, RenderGeometry (no Qt) | The process's single bgfx device, frame coordinator, native target registry and the native surface descriptor |
| `AOWIS::RenderCore` | Qt Gui, RenderDevice, RenderNetwork, RenderGlobe | 2D and globe GPU uploads and draws, imagery atlases, terrain buffers and index pools, sky and heatmap GPU, HUD text cache, embedded shaders |
| `AOWIS::RenderQtSurface` | Qt Gui (GuiPrivate optional), RenderDevice | `BgfxQtNativeSurface`: a `QWindow`'s native window and display handles (X11, Wayland, Windows, macOS) |
| `AOWIS::RenderMap` | RenderCore, RenderGlobeNetwork, RenderGlobeTerrain, RenderMapData, RenderQtSurface | The map: `MapBgfxMapView` with its renderers (2D symbology and picking, globe network and surface, labels, HUD) |

The host builds `MapBgfxMapView` and links `AOWIS::RenderMap`, plus
`AOWIS::RenderMapRest` when it loads tiles from aowis-server-map.

Embedded shaders keep the resource paths
`:/aowis/map/bgfx/<family>/<shader>.bin`; the families are glsl, essl,
spirv, dx11 and metal, and only those of the configured renderers are
embedded.

## What a host provides

- **A window or a context.** Native: the host embeds
  `nativeInputWindow()` and forwards its show, hide, resize, key and focus
  events. Host context: the host calls `renderHostFrame()` from its own render
  pass with its context current.
- **Settings and assets** through setters and providers: terrain detail,
  tank/reservoir textures, HUD images, navigation keys, scale bar, globe HUD
  state.
- **Data sources:** a `MapServerTransport` for tiles and terrain (REST through
  `AOWIS::RenderMapRest`, or the host's own, such as the GUI's in-process
  Standalone server), a `MapImageryProviderDirectory` for `MapModel`
  (normally `MapProviderCatalog`), and optionally a `NetworkRenderSnapshot`
  with its `MapNetworkRenderSymbology`.
- **Ownership of GeographicLib and MODEL.** If the host adds
  `GeographicLib_STATIC` or `AOWIS::Model` before Render, Render uses the
  host's targets; otherwise it builds its own submodules
  (`cmake/aowis_render_dependencies.cmake`). The same holds for one existing
  `bgfx` target. A process never contains a second bgfx runtime.

The details are in [Host integration](docs/host-integration.md).

## Runtime contracts

1. **Exactly one bgfx device per process.** A new view never calls
   `bgfx::init()` itself. Device generations, process-global view IDs and
   client retirement protect shared resources.
2. **GPU calls run on the owning render thread**, the thread of the view and
   its model. bgfx is built single-threaded. A Qt Quick host uses the basic
   render loop.
3. **Retire GPU resources before the native surface or the device goes
   away.** A restarted device needs newly allocated handles.
4. **3D swapchains have a D24S8 depth attachment.** Terrain skirts hide LOD
   seams only because the depth test keeps them behind nearer ground.
5. **Hydraulic identity and elevations stay in the host's model.** The
   renderer keeps no hydraulic model of its own.
6. **Compiling is not runtime acceptance.** Mock tests check submissions and
   cleanup; OpenGL, Vulkan, WebGL and Android behaviour need device tests.

## Change workflow

`./tools/apply_patch.sh` applies changed-file ZIP patches
(`aowis-gui-render-*.zip`, paths under `AOWIS-GUI-Render/`; files listed in
`AOWIS-GUI-Render/.aowis-patch-delete.txt` are removed). For a change that
spans Render and a host:

1. Apply the Render patch and run `./compile_linux_tests.sh`.
2. Commit and push the **Render Git tree**; check that new `.cpp` and `.h`
   files are tracked.
3. In the host, update the `external/AOWIS-GUI-Render` gitlink
   (`./git_submodule_update.sh --render`), apply the host patch and run the
   host's build-gated tests.
4. Exercise the affected map features on real GPUs before committing the host.

## Documentation

- [Architecture and data pipeline](docs/architecture.md): what each part
  owns, how a tile becomes a frame, and the correctness contracts.
- [Renderer](docs/renderer.md): how the 2D map, the HUD, labels, imagery,
  terrain and the globe network are drawn, renderer selection and shaders.
- [Host integration](docs/host-integration.md): embedding the CMake project,
  hosting `MapBgfxMapView` in both modes, GPU lifecycle rules, verification.
- [Roadmap](docs/roadmap.md): remaining work and acceptance gates.
