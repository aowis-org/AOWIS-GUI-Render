# Architecture and data pipeline

Render is the complete map engine: tile selection, imagery and terrain
streaming and decoding, terrain meshes, network geometry, GPU residency,
drawing, the HUD and picking. A host adds a window or graphics context,
input, settings and data sources (see [Host integration](host-integration.md)).

## Layers

```text
MapBgfxMapView  (AOWIS::RenderMap)      frame loop / host frames, device lifecycle,
  |                                     event routing, HUD, labels, picking, terrain queries
  +-- 2D: MapBgfx2dScene variants, MapBgfx2dGpu, imagery atlas, symbology
  +-- Globe: MapBgfxGlobeSurface (terrain, sky, heatmap)
  |          MapBgfxGlobeNetwork (network, models, X-Ray)
  |
  +-- MapModel (RenderView)             camera: continuous 2D zoom, globe orbit rig
  +-- MapGlobeSurfacePreparation, MapGlobeSurfaceScene,
  |   MapTerrainMeshScheduler (RenderGlobeTerrain)
  +-- MapTileRepository, MapTerrainRepository (RenderMapData)
  |      +-- InterfaceServerMap  <-- host's MapServerTransport
  |             (InterfaceServerMapREST from RenderMapRest, or the host's own)
  +-- BgfxDeviceManager, BgfxFrameCoordinator,
      BgfxNativeTargetRegistry (RenderDevice)
```

Everything below `MapBgfxMapView` is toolkit-neutral. Qt Widgets and Qt Quick
appear only in the hosts. `AOWIS::RenderDevice` does not use Qt at all.

## From the server to a frame

1. **Camera.** `MapModel` holds the camera. 2D uses a continuous zoom; the
   globe uses an orbit rig around a terrain-aware pivot. The host's input
   drives it through `MapModel` (pan, zoom, orbit) or through the view's
   navigation hooks.
2. **Imagery providers.** `MapModel` resolves the provider and the source for
   each zoom through the host's `MapImageryProviderDirectory`.
   `MapProviderCatalog` implements it from the server's `GET /map/v1/providers`
   and resolves the user's fallback chain. The host stores the fallbacks.
3. **Selection.** 2D picks the visible tiles of `round(zoom)`. The globe walks
   its quadtree (`MapGlobeSurfaceScene`) with horizon and view-cone culling,
   foreshortening and hysteresis.
4. **Requests.** `MapTileRepository` and `MapTerrainRepository` keep requests
   in flight (bounded, nearest first), cancel superseded ones, decode on
   worker threads (the browser's own decoders on WASM) and cache the results.
   Terrain waits for the dataset catalog (`GET /terrain/v1/datasets`); no
   dataset or zoom is hard-coded.
5. **Transport.** The repositories talk only to `InterfaceServerMap`. The host
   creates it through a `MapServerTransport`, normally
   `InterfaceServerMapREST`, configured by a `MapServerRestConfiguration`
   (base URL, API key, delete API key).
6. **Terrain meshes.** `MapGlobeSurfacePreparation` keeps the window of
   visible tiles and their vertex pool. `MapTerrainMeshScheduler` builds the
   DEM meshes on a worker thread, with neighbour-aware normals and
   ancestor stand-ins. Only new or changed tiles are uploaded.
7. **GPU.** Imagery goes into the shared atlas within a per-frame upload
   budget. Terrain goes into the terrain buffer and index pools. The network
   goes into retained per-chunk (2D) and per-category (globe) buffers that are
   coloured on the GPU from per-entity data textures.
8. **Frame.** `MapBgfxMapView` draws only when something visible changed
   (camera, size, content, arriving tile, animation). The frame coordinator
   submits all registered views before one `bgfx::frame()`, or flushes them
   into the host's context in `HostContext` mode.

## What the host owns

- The window or graphics context, the toolkit's event loop and its input.
- Settings: map server connection, provider fallbacks, terrain detail, units,
  shortcuts, language.
- Assets: tank and reservoir textures, HUD images, the scale bar's units.
- The network: the host builds `NetworkRenderSnapshot` and fills
  `MapNetworkRenderSymbology` from its own model and results. Editing,
  hydraulic elevations and picking IDs stay in the host.
- Optional overlays drawn through `setOverlayBuilder()` and
  `setGlobeOverlayBuilder()` (the GUI's editor overlay).

Terrain generation and raster acquisition stay on the map and hydrology
servers. Render only consumes the normalized tiles.

## Correctness contracts

1. **Stable geometry under camera movement.** Pan and orbit never rebuild
   retained tiles or network meshes. Only coverage, LOD or data changes do.
2. **Sparse DEM updates.** An arriving or improved DEM tile updates its own
   vertex range, not the whole window.
3. **Ancestor stand-ins.** A tile shows the nearest cached ancestor's imagery
   or DEM until its own content arrives. Replacement is deterministic and
   leaves no stale GPU resource.
4. **Terrain continuity.** The 64-cell DEM topology, the four skirts, the
   neighbour-sensitive normals, the sticky ECEF render origin and the
   drawn-surface query semantics belong together.
5. **Separate lifetimes.** Decoded CPU caches survive a GPU context loss; GPU
   handles do not. A restarted device re-uploads still valid content under a
   new device generation.
6. **Bad networks.** Bounded in-flight work, priorities, cancellation, cache
   reuse and retry matter as much as draw speed. Stale replies after a
   provider switch are ignored and never block the new provider's tiles.
7. **One device, one thread.** Streaming never creates another bgfx device,
   and worker threads never submit GPU commands.

## Precision

Globe geometry is kept relative to a sticky camera-near ECEF origin, in
double precision on the CPU and float32 on the GPU. Horizon and ray tests use
`GeoWgs84Ellipsoid::rayIntersection()` rather than a float sphere quadratic,
which cancels catastrophically near the ground. 2D network chunks each have
their own local origin.
