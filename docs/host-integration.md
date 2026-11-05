# Renderer host and lifecycle contracts

| Owner | Desktop GUI | Mobile |
|---|---|---|
| bgfx target | Lean AOWIS unity build via Render CMake | Pinned `bgfx.cmake` target supplied to Render |
| Render thread | GUI/map submission thread | Qt Quick scene-graph render thread |
| External window | `BgfxQtNativeSurface` from live `QWindow` | Borrowed EGL context from `QOpenGLContext` |
| Shared library | `AOWIS::RenderCore`, `AOWIS::RenderGeometry` | `AOWIS::RenderDevice`, `AOWIS::RenderGeometry` |
| Map geometry | Common Mercator camera, icon index, terrain height cache and imagery UV | Same renderer-owned algorithms, without GUI/model dependencies |
| Frame semantics | `BgfxFrameCoordinator` / ordinary `bgfx::frame()` | Qt owns swap; `bgfx::frame(BGFX_FRAME_FLUSH)` |
| Shaders | Embedded renderer-map resources | Mobile demo's pinned sample shaders |

RenderGeometry exposes Qt/bgfx-free production map geometry for both hosts.
RenderDevice owns the bgfx device generation and retirement guards in both
apps. Qt-specific adapters stay on the host side. Mobile still uses one
independent scene (stress-test GPU geometry), not the production 3D globe.

The next extraction boundary is not `MapBgfxSurface` wholesale: first separate
scene construction, rendering commands, editor labels/HUD inputs and host
callbacks, then give Mobile a stable neutral scene API. Check Linux OpenGL,
Vulkan, WebAssembly, Android GLES, resize, context loss/recreation and D24S8
before removing the fallback scene or consolidating the pinned vendor versions.
