# Map engine: current ownership and data pipeline

The intended library is a **reusable map visualization engine**, not merely a
bgfx wrapper. Any host drawing the same 2D map or 3D globe should reuse the
same tile selection, imagery/terrain decode, streaming, mesh generation,
cache invalidation, and GPU residency policy. The host should supply its
view, configuration, and data-source adapter instead of duplicating the
pipeline. **This end-to-end API is planned, not implemented.**

## Data-to-frame path today

```text
Host MapModel, camera, provider/dataset configuration
    |
    +-- GUI MapGlobeSurfaceScene: quadtree, culling, hysteresis, LOD
    |      +-- GUI MapGlobeSurfacePreparation: visible tile pool, terrain
    |          requests, fallback geometry, mesh scheduling, terrain queries
    |             +-- GUI MapTerrainRepository: dataset catalog, requests,
    |             |   cache, decode, invalidation
    |             +-- GUI MapTerrainMeshScheduler: async DEM mesh vertices
    |
    +-- GUI MapTileRepository: imagery requests, decode and cache
    |      +-- GUI MapProviderCatalog and server/service adapters
    |
    +-- GUI MapBgfxGlobeSurface: frame coordination, residency decisions,
           imagery lookup and atlas upload budget
                  |
                  +-- Render: tile topology/skirts, index and vertex pools,
                      terrain drawing, sky/heatmap GPU, shared imagery atlas
```

`MapBgfxSurface` owns the desktop map's frame/event orchestration. The
existing source contains substantial backend-neutral CPU work, but it still
references GUI `MapModel`, Qt `QObject` repositories, server configuration,
and—in parts of the 3D network scene—the hydraulic model.

## Verified source ownership

| Capability | Current implementation | Intended stable boundary |
| --- | --- | --- |
| 65×65 DEM tile format, validation, decoding | GUI `src/map/data/map_terrain_tile.*` | Render terrain-data module; preserve dataset and datum metadata |
| Terrain dataset catalog and request/cancel/cache bookkeeping | GUI `map_terrain_repository.*` | Neutral terrain cache/requests in engine; REST/cancel/config adapter outside |
| Terrain mesh worker, DEM neighbor normals and ancestor stand-ins | GUI `map_terrain_mesh_scheduler.*`, `map_globe_surface_preparation.*` | Render worker and result streams; host supplies immutable tiles and render origin |
| Globe quadtree culling, foreshortening and hysteresis | GUI `map_globe_surface_scene.*`, `map_globe_terrain_levels.h` | Render selection policy with neutral camera, viewport, dataset capability inputs |
| Visible tile pool, DEM range patches, terrain-height intersection | GUI `map_globe_surface_preparation.*` | Render retained scene preparation and query API |
| Terrain indices/skirt topology and GPU index pool | Render `MapGlobeTileTopology`, `MapBgfxGlobeTileIndexPool` | Already shared |
| Terrain vertex/index uploads and GPU terrain/sky/heatmap draw | Render GPU classes | Already shared; host still selects per-frame work |
| Imagery request batching, prioritization, decoded QImage cache | GUI `map_tile_repository.*`, `map_tile_streaming_policy.h` | Render tile scheduler/cache; host supplies tile-source transport |
| Imagery atlas, GPU upload and ancestor UV fallback | Render `MapBgfx2dImageryGpu`, `MapBgfx2dImageryAtlas` and `map_bgfx_globe_imagery.h` | Shared residency layer; host still controls which tiles/budget are uploaded |
| Provider catalog, fallback chains and map-server configuration | GUI `map_provider_catalog.*` and service/config files | Keep user settings and provider administration in host; normalize source metadata for engine |
| Native window, Qt input, editor tools, hydraulic data and picking IDs | GUI | Keep host-owned; engine exposes terrain ray/height query results |

**Important distinctions:**

- Terrain data decoding/mesh preparation is common to every map consumer and
  therefore belongs in the reusable engine eventually. GDAL source processing,
  raster acquisition and terrain generation on the MAP/HYDROLOGY servers do
  **not** move into Render.
- A tile source should expose bytes and metadata (dataset/provider identity,
  tile address, content encoding, generation/cancellation); the engine should
  not require GUI's `InterfaceServerMap` or a hard-coded REST endpoint.
- Network availability and retry policy can be reusable without forcing the
  library to own authentication, server selection, or provider configuration.
- Preserve source-identity keys across provider switches and ignore replies
  from stale request generations. Failed/cancelled requests must not block
  the next provider's visible tiles.

## Essential correctness contracts

1. **Stable geometry across camera movement.** Retained tile pools and network
   meshes should not be reconstructed merely for pan/orbit; only coverage,
   LOD or relevant data changes invalidate geometry.
2. **Sparse DEM updates.** A replacement or improved DEM tile updates its
   assigned vertex range, not the entire visible window. The current shared
   Render buffer pool already supports range uploads.
3. **Ancestor stand-ins.** A child imagery/terrain tile may display valid
   cached ancestor data until its exact-resolution content arrives. Replacing
   a stand-in must be deterministic and must not leave stale GPU resources.
4. **Terrain continuity.** The current 64-cell DEM topology, four skirts,
   neighbor-sensitive normals, persistent ECEF origin, and drawn-surface
   query semantics must survive the extraction.
5. **Separation of lifetimes.** CPU decoded caches survive a GPU context
   recreation when appropriate; GPU handles do not. An engine restart
   re-uploads still-valid cached content using a new device generation.
6. **Bad-network behavior.** Explicit bounded in-flight work, prioritization,
   cancellation, fair scheduling, cache reuse and retry backoff matter just
   as much as frame draw performance.
7. **Single device.** Shared streaming does not create another bgfx device
   or let worker threads submit GPU commands.

## Future public interface (design intent, not an existing API)

The engine should expose distinct, testable contracts for an **immutable tile
payload**, a **tile source** (async request/result/cancellation), **terrain
scene preparation** (camera + visible leaves + DEM/imagery state), a **GPU
residency coordinator**, and **terrain queries**. Exact types, ownership,
threading and Qt/C++ boundary are to be designed during extraction; do not
assume those interfaces are already part of the source.

The GUI will remain the reference host until engine and GPU behavior are
verified. The Mobile host is intentionally not modified during this work.
See the [forward roadmap](roadmap.md) for the extraction order and acceptance.
