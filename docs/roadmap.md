# Renderer forward roadmap

This is **remaining product work**, not a historical patch log. Implemented
ownership is documented in the [README](../README.md) and
[map-engine boundary](map-engine-boundary.md). Each item stays open until its
**real Render build, full GUI build-before-test, relevant functional tests,
and visual/device checks** pass. Source-only/mock validation is not a sign-off.

## First: complete shared terrain/imagery engine

| Workstream | Deliverable | Verification that closes it |
| --- | --- | --- |
| Imagery GPU residency/lifecycle | Centralize tile-to-atlas residency, generations, eviction, upload decisions and recovery while preserving 2D/3D atlas sharing | Provider switch, stale replies, ancestor fallback, GPU reset and bounded uploads |
| DEM tile format and terrain datasets | Decoder and catalog are in `AOWIS::RenderTerrainData`; remaining: real server-tile fixtures in the tests | Real server-tile fixtures |
| Globe selection and LOD | Move quadtree leaves, horizon/view-cone culling, foreshortening, hysteresis and terrain zoom policy | Equivalent tile sets under camera paths, poles, antimeridian and viewport changes |
| Terrain mesh generation and preparation | Move immutable requests, 65×65 sampling, neighbor normals, ancestor DEM stand-ins, worker scheduling and sparse patch staging | Continuous hillshading, correct skirts and bounded rebuilds on partial DEM arrival |
| Tile streaming/caches | Unify visible-tile priority, bounded in-flight requests, cancellation, decode and cached payloads behind a source interface | Bad-network, provider-switch, cache, out-of-order response, retry and memory-limit tests |
| Terrain queries and adapters | Shared drawn-height/ray intersection results; host supplies source/server configuration and editor interaction | Terrain-aligned placement/picking while editor retains model elevation semantics |
| GUI handoff/cleanup | Replace GUI engine duplicates without changing its Widgets/server/editor controllers | Full GUI build + CTest gate, source-ownership tests and OpenGL/Vulkan runtime parity |

The listed work is all part of the shared renderer/engine scope even where it
is **CPU-side** rather than direct GPU rendering. Keep the normal pipeline
usable without hard-coding `MapModel`, `AOWIS-SERVER-MODEL`,
`InterfaceServerMap` or GUI widget dependencies into the library.

## Then: finish common rendering surfaces

- Remaining GUI-owned 3D scene coordination and the small number of geometry
  or shader-state conversions that can be made model-neutral.
- HUD, labels, editor overlays and translated presentation **as inputs** to
  Render; app/tool strings, locale selection, units and RTL widget layout stay
  in each host. Preserve the `MapBgfxHud` Qt translation context and lazy
  language-change redraw contract.
- Stable host-neutral scene/frame inputs and a clearly owned map render
  lifecycle; remove duplicate rendering code only after the replacement is
  exercised by GUI.

## Final acceptance and cleanup

- Verify native Linux OpenGL/Vulkan, WebAssembly WebGL2, viewport resize,
  map/Projects view switching, first-owner swapchain cases, D24S8 and skirts.
- Validate loading into uncached terrain, cache eviction and bad connectivity;
  collect frame/upload/CPU budgets without assuming old GPU metrics apply.
- Test repeated device retirement/recreation and distinguish CPU cache
  invalidation from GPU resource teardown.
- Update the reference GUI documentation and repository API guides to the
  final ownership; remove obsolete duplicate source and stage-specific notes.
- **Only then** resume Mobile's production 2D/3D map integration and Android
  GPU/lifecycle acceptance. Mobile is not currently the integration host.

The workstreams are sequencing guidance, not guaranteed turn counts or
completed capabilities. Revisit the order if real builds or runtime tests
expose correctness issues.
