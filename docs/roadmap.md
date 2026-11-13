# Roadmap

Remaining work on the map engine. What exists is described in the
[README](../README.md) and [Architecture](architecture.md). An item is
closed only when the Render build, the hosts' build-gated tests, the relevant
functional tests and a check on real devices pass. Source-only and mock
validation is not a sign-off.

## Engine

| Work | Done when |
| --- | --- |
| Imagery GPU residency: one owner for tile-to-atlas residency, eviction, upload decisions and device-loss recovery, shared by 2D and the globe | Provider switch, stale replies, ancestor fallback, GPU reset and bounded uploads verified on both |
| Terrain fixtures | Tests run on real aowis-server-map DEM tiles and dataset catalogs, not only synthetic ones |
| Streaming under bad networks | Measured behaviour for slow, lossy and offline connections: priorities, cancellation, retries and memory limits |
| Software fallback | A software rasterizer (llvmpipe / WARP) is selected when no GPU renderer initializes |
| Performance budgets | Frame, upload and CPU budgets measured on the target devices, not carried over from older renderers |

## Platform acceptance

- Native Linux OpenGL and Vulkan, macOS Metal and WebAssembly WebGL 2:
  viewport resize, view switching, first-owner swapchain cases, D24S8 depth
  and skirts. (Windows and Android: reported working by the user,
  14 November 2026.)
- Loading into uncached terrain, cache eviction and bad connectivity.
- Repeated device retirement and recreation; CPU caches survive, GPU
  resources are rebuilt.
- Antialiasing: each MSAA level (`SwapChain::flags`) takes effect on every
  backend, and a level the GPU lacks is clamped without an error.

Change the order when real builds or device tests expose correctness
problems.
