# Host integration

How an application embeds Render and hosts the map. The two hosts today are
AOWIS-SERVER-GUI (Qt Widgets, native window) and AOWIS-GUI-Mobile (Qt Quick,
host context).

| | AOWIS-SERVER-GUI | AOWIS-GUI-Mobile |
| --- | --- | --- |
| Toolkit | Qt Widgets; WebAssembly canvas | Qt Quick (Android, Linux desktop) |
| Host class | `MapBgfxSurface` (`MapBgfxMapViewHost`) | `MobileMapView` (`QQuickItem`, `MapBgfxMapViewHost`) |
| Render target | `NativeWindow`: own `QWindow`, swapchain and frame loop | `HostContext`: Qt Quick's EGL context, Qt presents |
| bgfx sources | GUI's `external/{bx,bimg,bgfx}` (`AOWIS_RENDER_VENDOR_ROOT`) | Render's nested `external/{bx,bimg,bgfx}` |
| Renderers | Linux OpenGL + Vulkan, Windows D3D11 + OpenGL, macOS Metal, WASM OpenGL ES | Android OpenGL ES, Linux OpenGL |
| GeographicLib, MODEL | GUI's submodules | Mobile's submodules |
| Map server | REST, or the in-process server of the Standalone build | REST |
| Network | `NetworkRenderSnapshot` and symbology from the hydraulic model | none yet |
| Input | `MapWidget` navigation, editor tools, HUD routing | `MobileMapView` touch and mouse gestures, HUD routing |

## Embedding the CMake project

Add Render with `add_subdirectory(external/AOWIS-GUI-Render)` after the
host's own GeographicLib and MODEL targets, and switch on the libraries the
host needs:

```cmake
set(AOWIS_RENDER_VENDOR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external" CACHE PATH "")  # optional
set(AOWIS_RENDER_BUILD_MAP ON CACHE BOOL "" FORCE)
set(AOWIS_RENDER_BUILD_MAP_REST ON CACHE BOOL "" FORCE)
set(AOWIS_RENDER_BUILD_GEOMETRY_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/AOWIS-GUI-Render aowis-render)
target_link_libraries(app PRIVATE AOWIS::RenderMap AOWIS::RenderMapRest)
```

- **One bgfx.** Render builds `bx`, `bimg` and `bgfx` from
  `AOWIS_RENDER_VENDOR_ROOT` (default: its own `external/`). A host that sets
  it to its own checkouts must pin the same revisions, or the committed
  shaders may not match. A process never links a second bgfx.
- **Renderers.** `AOWIS_BGFX_RENDERERS` selects what is compiled in and
  which shader families are embedded. Android and WebAssembly require
  `OPENGLES`.
- **GeographicLib and MODEL.** When `GeographicLib_STATIC` and `AOWIS::Model`
  exist before `add_subdirectory`, Render links them. Otherwise it builds its
  own submodules, or the ones at `AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR` and
  `AOWIS_RENDER_MODEL_SOURCE_DIR`.
- **Checkout check.** A host should fail configuration with a clear message
  when the Render checkout or its vendor submodules are incomplete (the GUI
  lists required Render files; Mobile checks the vendor headers).
- Adding production sources to Render needs a CMake re-configure in the host.

A geometry-only configuration without Qt and bgfx:

```sh
cmake -S . -B build-geometry -G Ninja \
  -DAOWIS_RENDER_BUILD_DEVICE=OFF \
  -DAOWIS_RENDER_BUILD_MAP_CORE=OFF \
  -DAOWIS_RENDER_BUILD_NETWORK_SCENE=OFF \
  -DAOWIS_RENDER_BUILD_GLOBE_MODELS=OFF \
  -DAOWIS_RENDER_BUILD_GEOMETRY_TESTS=ON
cmake --build build-geometry -j 8 && ctest --test-dir build-geometry --output-on-failure
```

It covers the pure geometry and recording-mock tests, not the Qt and bgfx
libraries.

## Hosting `MapBgfxMapView`

`MapBgfxMapView` is the complete map: 2D and globe, imagery, terrain,
network, labels, HUD, picking and terrain queries. Construct it with the
host's `MapModel`, a `MapBgfxMapViewHost` and the render target.

Every host:

- implements `MapBgfxMapViewHost`: `mapViewVisible()`, `mapViewFont()`,
  `mapViewPalette()`, `mapViewLayoutDirection()`, `mapViewFocusNavigationInput()`, and the area's logical
  size, device pixel ratio, screen, global position and cursor where the view
  has no window of its own (WASM, host context);
- forwards `handleHostShown()`, `handleHostHidden()`, `handleHostResized()`,
  `handleHostChange(type)` (language, layout direction, palette, font) and, where the host owns
  the keyboard, `handleKeyPress()`, `handleKeyRelease()`, `handleFocusOut()`;
- gives every pointer event to the HUD first (`handleHudMouseEvent()`,
  `handleHudWheelEvent()`) and acts on the returned `HudPointer`; a consumed
  press keeps its moves and release;
- applies HUD commands from `setHudActionHandler()` (view mode, zoom,
  joystick, toggles, underground mode, download cancel, legend choices);
- injects settings and assets: `setTerrainDetailReductionSource()`,
  `setGlobeModelImages()`, `setHudImages()`, `setHudNavigationKeys()`,
  `setHudScaleProvider()`, `setGlobeHudStateProvider()`;
- sets the repositories (`setTileRepository()`, `setTerrainRepository()`)
  and, if it has one, the network (`setNetworkSnapshot()`, `setSymbology()`,
  `setVisualControlSettings()`).

### Native window (`RenderTarget::NativeWindow`)

- Embed `nativeInputWindow()` in the toolkit (the GUI uses
  `QWidget::createWindowContainer()`); the view deletes it if no host ever
  reparented it. `setNativeWindowAcceptsFocus(false)` keeps keyboard focus
  on another receiver.
- Native handles come from `BgfxQtNativeSurface::capture()`;
  `setNativeSurfaceCapture()` replaces it for a toolkit that adapter lacks.
- Wrap planned reparenting (fullscreen and the like) in
  `suspendForNativeWindowChange()` / `resumeAfterNativeWindowChange()`, or
  better keep the window parented.
- WebAssembly: the page provides `window.aowisBgfxMap`, the bridge that
  positions the browser canvas over the host's area; the frame loop is the
  browser's animation frame.

### Host context (`RenderTarget::HostContext`)

For a host whose toolkit owns the graphics context and presents, such as
Qt Quick's OpenGL / OpenGL ES scene graph:

- Run Qt Quick's **basic** render loop (`QSG_RENDER_LOOP=basic`), so the
  render pass runs on the thread of the view, its model and repositories.
  On Linux desktop the context must be EGL (`QT_XCB_GL_INTEGRATION=xcb_egl`
  under X11).
- From `QQuickWindow::beforeRenderPassRecording`, inside
  `beginExternalCommands()` / `endExternalCommands()`, call
  `renderHostFrame({native_context, opengl_es})` with the current
  `EGLContext` (`QNativeInterface::QEGLContext`). bgfx is initialized in that
  context on the first frame, and each frame is flushed into the host's bound
  framebuffer (`bgfx::frame(BGFX_FRAME_FLUSH)`) without a swap.
- Call `releaseHostContext()` from `QQuickWindow::sceneGraphInvalidated`
  (context still current). The next frame initializes again.
- Call `QQuickWindow::update()` on `hostFrameRequested()`.
- The item's geometry is the view's size (`handleHostResized()`); every host
  frame redraws completely, since Qt Quick clears the framebuffer.

### Tiles and terrain from aowis-server-map

```cpp
MapServerRestConfiguration server;
server.base_url = QUrl("https://maps.example.org:8443");
server.api_key = key;                       // optional
MapProviderCatalog catalog;                 // MapModel's MapImageryProviderDirectory
catalog.setServer(server);
catalog.setFallbackStore(fallbacks, save);  // QHash<provider, fallback>; save persists a choice
MapServerTransport transport;
transport.create = [server](QObject *parent) -> InterfaceServerMap * {
    return new InterfaceServerMapREST(server, parent);
};
MapTileRepository tiles(transport);
MapTerrainRepository terrain(transport);
catalog.refresh();                          // GET /map/v1/providers
```

`MapProviderCatalog::refresh()` reports an error while no server is
configured. A host recreates the repositories when the server changes.

## GPU lifecycle rules

- **One `BgfxDeviceManager` and bgfx runtime per process.** Additional views
  register as clients before they acquire GPU resources, view IDs or
  targets.
- **The frame coordinator** hands out process-global, generation-scoped view
  IDs. Native windows: it submits all clients before one `bgfx::frame()`.
  Host context: `flush()` submits without presenting.
- **Retire before destroying.** Clients, GPU pools, view IDs and swapchains
  are retired before their surface or the device goes away. A stale device
  generation invalidates handles and view IDs.
- `BgfxNativeSurface` is toolkit-neutral. A secondary target resizes on its
  own; an ordinary secondary resize never resets the whole device.
- **D24S8 depth** for 3D targets. Without depth, terrain skirts overdraw
  nearby ground; the skirts themselves are still needed for seams.
- Keep the embedded shader paths `:/aowis/map/bgfx/<family>/<shader>.bin`
  stable. `AOWIS_BGFX_SHADER_TOOLS=ON` regenerates shaders; it is never a
  build prerequisite.

## Verification

1. In Render: `./compile_linux_tests.sh` (build first, then CTest).
2. In the host: its build-gated tests, e.g. the GUI's
   `./compile_linux_tests.sh` with `tools/verify_render_checkout.sh`, or
   Mobile's `./compile_linux.sh --tests`.
3. On real GPUs: 2D/3D switching, terrain LOD and skirts, wireframe and polar
   caps, X-Ray, models, sky, heatmap, tile replacement after a provider
   switch, device teardown and recreation, resize, focus loss.
4. Qualify WebAssembly and Android separately: passing Linux tests says
   nothing about their GPU and lifecycle behaviour (Android: background and
   resume, scene-graph invalidation).

### Live localization and right-to-left HUD

`MapBgfxMapViewHost::mapViewLayoutDirection()` returns the current host
layout direction; the default remains left-to-right for existing HUD-free
hosts. Qt Widgets hosts should return their widget's `layoutDirection()`.
`handleHostChange(QEvent::LanguageChange)` refreshes HUD text and asks the
application to refresh its own legend/slider strings. A subsequent
`LayoutDirectionChange` reanchors HUD panels without rebuilding the map or
closing a legend popup. The map projection, GPS/terrain positions, navigation
axes, compass heading, and color-ramp value progression never mirror.

The GUI uses `GuiLanguage` for the `aowis_gui_*` and `aowis_render_*`
translator resources; the Render library does not own or install translators.
Use `aowis-render-bgfx-hud` to verify both live translation and RTL hitbox
placement headlessly. Native/WASM screenshots and Arabic font shaping still
need end-to-end acceptance on their actual Qt kits.
