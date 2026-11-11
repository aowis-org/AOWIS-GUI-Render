# Renderer

How `AOWIS::RenderMap` draws the map. The pipeline that feeds it is in
[Architecture](architecture.md), embedding and lifecycle in
[Host integration](host-integration.md).

- [Retained 2D network GPU drawing](#retained-2d-network-gpu-drawing)
- [On-demand rendering](#on-demand-rendering)
- [Native HUD](#native-hud)
- [Map labels](#map-labels)
- [2D imagery](#2d-imagery)
- [Globe and terrain](#globe-and-terrain)
- [Renderer selection](#renderer-selection)
- [bgfx build](#bgfx-build)
- [Frame clock and smooth zoom](#frame-clock-and-smooth-zoom-do-not-regress)

## Retained 2D network GPU drawing

* Input: the host's `NetworkRenderSnapshot` (`setNetworkSnapshot()`).
  `MapRetainedNetworkScene` retains it, and `map_bgfx_2d_snapshot_adapter.h`
  converts each geometry revision into `MapBgfx2dNetworkInput` (render IDs,
  entity class codes, geodetic positions, link endpoints). Camera, HUD and
  value updates never run that conversion. `MapBgfx2dScene` prepares the CPU
  mesh, `MapBgfx2dGpu` submits it, and `MapBgfxEntityStyle` implements
  `MapBgfx2dStyleBinding` (entity data textures and palette).
* **Spatial chunks** (`map_bgfx_2d_mesh.h`): primitives are grouped by a fixed
  world grid (zoom-12 cells, ~9.8 km). Each chunk has its own local origin
  (float error ~0.002 px at zoom 19 for networks of any extent) and bounds
  for per-frame view culling. Off-screen chunks issue no draw calls.
* **Thick lines**: one indexed quad per segment, extruded in `vs_line_2d.sc`
  in screen pixels with square caps of half the width, which close polyline
  joins. Constant on-screen width at every zoom; a metre-based width is
  converted per frame with `MapBgfx2dChunking::pixelsForMeters()` (continuous
  in the zoom). GL/WebGL line primitives are not used (1 px on core/WebGL).
* **Nodes**: unit-corner quads scaled by a size uniform and cut to
  antialiased circles in `fs_marker_2d.sc`. Every snapshot node is drawn once,
  including demand points (entity `DemandPoint`); the attachment list only
  provides the attachment lines. 
* **Declutter** (`MapBgfx2dScene`): coincident nodes (pump/valve inlet and
  outlet digitized at one coordinate) are spread with the shared
  `computeNodeDeclutterOffsets()` at 1 m, as on the globe. Link
  endpoints and demand-point attachments follow their node's offset. Links
  are kept as polylines so icons and chevrons are placed along the whole link.
* **Icons** (`vs_icon_2d.sc`, `fs_icon_2d.sc`): node icons for non-junction
  nodes and link icons (pumps, valves) at the middle of the link length, from
  `mapIconAtlasImage()` (premultiplied, full CPU-built mip chain, trilinear).
  Constant pixel size (px or metres, continuous in the zoom). When icons are shown the
  node dot of an entity with an icon gets alpha 0.
* **Flow chevrons** (`vs_flow_2d.sc`, `fs_flow_2d.sc`): zoom-independent.
  Every link gets chevrons at fixed world positions centred on the link
  (pumps/valves at 30 %, the icon owns 50 %), organised as a hierarchy:
  position j has level `ctz(|j|) + base_level`, the centre is always shown.
  The vertex shader shows the levels whose on-screen spacing is >= 100 px
  (`MapBgfx2dFlow::thresholdLevel()` from the continuous zoom) and fades the
  next finer level in over one octave, so chevrons never pop at imagery level
  boundaries. At most 17 per link (long links use a coarser base level);
  none on links shorter than 18 px. One quad per chevron; the fragment shader
  draws the two strokes as an antialiased distance field. The direction is the
  link's flow sign in its entity data (below), so a timeline step never
  touches geometry.
* **GPU colouring** (`map_bgfx_entity_style.h`, `shaders/entity_style.sh`):
  the network's vertex buffers carry no colours, only the render id of their
  entity (plus "link icon" on icons). Every vertex shader colours its
  primitive from per-entity data, shared by the 2D map and the globe:
  - two RGBA32F data textures, nodes and links, 2048 texels per row, texel =
    render id: x the colouring metric's raw value, y the heatmap value (nodes)
    or the flow direction -1/0/+1 (links), z flag bits (has value, has heat
    value, hidden, selected, error, stale, junction / pump-or-valve, has
    icon). Read with `texelFetch` in the vertex shaders;
  - a 256 x 3 RGBA8 palette texture: the node, link and heatmap ramps in the
    CPU's 256 buckets (`networkSymbologyRampColor()`), flip applied;
  - uniforms: per metric (minimum, 1 / range, base fraction, state), the
    junction / icon / flow toggles, the fixed colours (base colours, the
    unavailable colour, icon fills, selection cyan, error red, stale grey)
    and a per-draw mode (symbology, selection, diagnostic, heatmap; vertex
    colours for the globe's outlines).
  What a change costs: a time step uploads the changed data texture(s) (16
  bytes per entity, one `updateTexture2D`), a palette change 3 KB, a range,
  junction, icon or flow toggle nothing but uniforms; hiding, selection and
  diagnostics are flag changes in the data textures. No vertex buffer is
  ever rebuilt or re-uploaded for colours. The rules,
  evaluated in the shader: visual None gives the base colour
  (`MapNetworkRenderSymbology::*_base_color`, e.g. the GUI editor's brown
  pumps and valves), a missing value or a non-finite range the unavailable colour,
  else the ramp bucket of `(value - minimum) / (maximum - minimum)` (0.5 for
  an empty range); attachments take their demand point's colour; junctions
  can be switched off and node dots give way to shown icons; icons use the
  default fill unless colourised (then the value colour, or the
  icon-unavailable fill); chevrons are black or white by link luminance.
  Sizes (link width, node size, icon size, chevron size; px or metres) are
  per-frame uniforms (`map_bgfx_2d_symbology.h`).
* **Symbology values** (`map_network_render_symbology.h`): per time step
  the host writes the raw values densely by render id (`QVector<double>`,
  NaN = no value) and the flow directions (`QVector<qint8>`), plus the
  ranges; the GUI's `MapNetworkSymbologyResolver` does this from its
  hydraulic results. Measured on 360 k nodes / 720 k pipes, the render side
  of a time step takes 17 ms (data textures; 308 ms with per-vertex colour
  streams), a palette or range change 0.07 ms.
* **Heatmap** (`fs_heatmap_2d.sc`): one spot per node (incl. demand
  points, at the decluttered position) with a heatmap value. No extra
  geometry: the heatmap pass draws the node marker quads with
  `vs_network_2d.sc` in the heatmap mode, the radius as size uniform (the
  continuous heatmap ramp of the node's heatmap value; no spot without a
  value or without a finite range). Radius in
  px or metres (metres converted from the continuous zoom, so it grows smoothly),
  opacity and solid centre are uniforms; premultiplied "over" compositing. Culled with the radius as margin; skipped when no
  heatmap is selected or the opacity is 0.
* Layer order across all chunks: heatmap, attachments, links, chevrons, nodes,
  icons (the heatmap sits directly above the imagery).
* Buffers are uploaded only on geometry revisions; camera movement changes
  only per-chunk uniforms. The debug overlay shows uploads and
  drawn/total chunks.
* 4x MSAA (`BGFX_RESET_MSAA_X4`, clamped by bgfx to renderer support) for
  antialiased line and marker edges.
* Shaders are compiled per renderer family and embedded in Qt resources
  (see "Renderer selection").

* **Picking** (`map_bgfx_2d_picking.h`): `hitTest()` for click
  selection and the hover cursor, with these priorities and radii
  (icons max(12, size/2 + 6) px, nodes incl. demand points max(10, r + 5) px,
  links max(8, w/2 + 5) px), but against what bgfx draws: decluttered
  positions, link icons at the middle of the link length, sizes from the
  continuous zoom and the model's current camera. Hidden icons/junctions are
  not pickable. CPU pass in screen space on click/mouse move only.
* **Highlights**: selection (0,190,255) and simulation errors (red,
  stale results grey): wider copies of the entity drawn on top of
  links resp. nodes (link width + 2 px, + 6 px when the selected entity has an
  error; node radius + 2 / + 5 px). Small separate meshes built from
  `MapBgfx2dScene::filtered()` (exact drawn positions), rebuilt only when the
  selection, the error set, the prepared geometry or the junction visibility
  changes.
* **Node declutter toggle**: `MapBgfx2dSceneVariants` caches both the
  decluttered monitor and exact-position editor scenes per geometry revision.
  Each is prepared once; revisiting an unchanged variant reuses its
  `MapBgfx2dScene::preparationId()`. The GPU mesh, highlights and picking
  follow the selected variant and can still require a GPU mesh upload on
  switching, but do not recompute derived CPU geometry. On a true geometry
  revision, the unused presentation is evicted and prepared lazily again.

## On-demand rendering

The frame loop (desktop: a timer slightly faster than the display; WASM:
`requestAnimationFrame`) keeps ticking for the navigation clock, but a tick
presents a frame only when something visible changed: the camera
(`mapCameraUpdate()` against the last presented frame), the size or device
ratio, a call of any view setter or `requestRedraw()` (e.g. a host overlay's
content), an arriving tile, an expose; or while something animates by itself
(`MapBgfxHud::animating()`: download busy bars, a tooltip inside its delay;
tile uploads over the per-frame budget; deferred labels). An idle map costs
an early return per refresh instead of a full frame - in the browser it no
longer keeps a core busy. Plain pointer moves over the map present nothing;
only HUD hover feedback does. Unchanged download numbers present nothing
either.

In `HostContext` mode the host's scene graph schedules the passes: the view
emits `hostFrameRequested()` whenever it would present, and the host renders
it in its next pass.

## Native HUD

Toolkit widgets cannot sit on top of bgfx's native window, so the HUD is
drawn by bgfx itself, last in the map view (always on top), as one indexed
draw per frame. Every host gets the same HUD.

* `map_bgfx_hud_canvas.h`: logical-pixel draw list (rects, rounded rects,
  circles, lines, atlas sprites) with premultiplied ABGR vertex colours and
  32-bit indices. Solid shapes sample one white atlas texel, so everything is
  one program, one texture, one draw.
* `map_bgfx_hud_text_cache.*`: whole text runs and images rasterized by Qt
  (QPainter, the host's font, the screen's logical DPI and device pixel
  ratio) into one 1024 x 1024 premultiplied RGBA8 atlas. Shelf packing; only
  the changed rectangle is uploaded. When the atlas is full it is cleared and
  the HUD frame is rebuilt once against the refilled atlas.
* `map_bgfx_hud.*` (pure Qt Gui, unit-tested in `tests/bgfx_hud_test.cpp`):
  state, layout and input: centred crosshair, GPS dot (red, black outline) at the continuous
  camera's projection, scale bar bottom left (sampled through the viewport
  centre from the continuous camera, label from the host's scale provider),
  download panels top centre (map tiles, terrain: busy bar, "n active ·
  m queued", Cancel stays disabled until fresh numbers arrive), one panel
  top left with the fullscreen button and the 2D/3D switch (3D once the
  globe surface initialized; `setHudViewSwitchVisible()`), the pan joystick
  top right with the zoom buttons (zoom in above zoom out) under it. Hit
  boxes come from the last build: clickable is exactly what was drawn; boxes
  of a popup closed since then are ignored.
* Pan joystick (`MapBgfxHud::joystick()`): a ring with four arrows and a
  knob. A press on it sends `JoystickDrag` (Begin, press position) and is not
  held by the HUD: the host captures the pointer and pans while the knob is
  deflected (the GUI: `MapWidget::beginJoystickPan()`, with the globe orbit's
  pointer lock). The host reports the knob (`setHudJoystick()`) and held pan
  keys (`setHudKeyboardPan()`); the arrows glow towards the view's motion.
  The wheel over the joystick and the zoom buttons zooms about the crosshair
  (`MapBgfxHud::wheelZoomsAtCrosshair()`). Their tooltips list the host's
  configured keys (`setHudNavigationKeys()`).
* Keyboard focus: any press on the map or a HUD element calls the host's
  `mapViewFocusNavigationInput(Qt::MouseFocusReason)`, so the keys return to
  the map.
* Map legend, bottom right: node /
  link / heatmap metric combos (list opens on press, below or above the
  combo and tall enough for every entry; only when neither side has room it
  scrolls, with a scroll bar: drag the thumb, click the track to page, wheel),
  colour ramps (gradient through the palette stops, 7/5/3 value labels that
  never collide, "uniform", "No finite values"), the hover value swatch, the
  palette menu (Sequential / Diverging, preview + Flip per palette, Reset)
  and QToolTip-style tooltips after 0.7 s. A press outside an open popup
  closes it and is swallowed; Escape and focus loss close it too.
* The host supplies the legend's content (`setHudLegend()`: entries, ranges
  in the displayed unit, palette stops) and receives the user's choices as
  HUD commands.
* `map_bgfx_hud_gpu.*`, `vs_hud.sc`/`fs_hud.sc`: point-sampled atlas
  (texels on the physical pixel grid), premultiplied blend, transient buffers.

Input: every map mouse event goes to the HUD first
(`handleHudMouseEvent()`, `handleHudWheelEvent()`, `handleHudKeyPress()`);
a press on a HUD panel is
consumed together with its moves and release and never reaches navigation,
picking or hover (the joystick's press only: its drag is navigation). Wheel events over a HUD panel scroll an open list or are
swallowed, never zooming the map underneath.
Button and legend actions are delivered queued as `MapBgfxHud::Command`
(`setHudActionHandler()`), outside the native window's event dispatch
(a handler may change the layout around the map, e.g. fullscreen). Desktop cursors (pointing hand, cross
over ramps) are set on the embedded native window (`setMapCursor()`), the
only cursor visible there.

A planned native-window change (reparenting) goes through
`suspendForNativeWindowChange()` / `resumeAfterNativeWindowChange()`, which
shut bgfx down first: Qt Wayland destroys the embedded window's `wl_surface`
on reparent without a `QPlatformSurfaceEvent`, and a Vulkan swapchain alive on
it ends the Wayland connection. `QPlatformSurfaceEvent` handling covers
unplanned native-window recreation. Hosts therefore keep the map window
parented, e.g. for fullscreen.

Strings: the HUD's own texts use the translation context `MapBgfxHud`
(`Q_DECLARE_TR_FUNCTIONS`, `QT_TRANSLATE_NOOP` for compass abbreviations);
keep that context name stable. Everything a host passes in (legend, slider
labels, scale text) is already translated by the host. A host forwards
`QEvent::LanguageChange` to `handleHostChange()`: the HUD re-translates on the
next frame, rebuilds its layout and hit boxes, uploads only new text runs,
keeps open popups and emits `localizedPresentationRefreshRequested()` for the
host's own texts. bgfx and the map data are untouched. `tests/bgfx_hud_test.cpp`
covers this with a synthetic `QTranslator`.

bgfx's statistics text overlay would cover the HUD; it is shown only with
`AOWIS_BGFX_DEBUG=1`.

## Map labels

Text on the map next to the network entities, like EPANET's *Map Options ->
Notation*: node IDs, link IDs and (monitor) the values of the metric the map
is coloured by, in the legend's unit (`MapLabelSettings`,
`map/render/map_label_settings.h`, `setLabelSettings()`; all off by default).

* `map_bgfx_labels.*` (pure Qt Gui, unit-tested in
  `tests/bgfx_labels_test.cpp`): greedy, decluttered screen-space placement.
  Candidates are the entities on screen at their drawn positions (links: the
  middle of their visible length; pipes shorter than 24 px get none). Every
  on-screen node symbol and device icon is an obstacle; labels are tried in
  priority order (tanks/reservoirs, pumps/valves, junctions, pipes, demand
  points; ties by render id, stable while panning) at up to four positions
  around their symbol, and kept only where they overlap neither a label nor
  a symbol and lie inside the viewport. A 48 px grid keeps each test local.
  Above 30000 on-screen nodes only tanks, reservoirs, pumps and valves are
  labelled.
* Text: a signed-distance glyph atlas, the way game and map engines draw
  text (`map_bgfx_glyph_atlas.*`, `map_bgfx_label_gpu.*`, `vs_hud.sc` +
  `fs_label_sdf.sc`). Lines are shaped by `QTextLayout` (kerning, fallback,
  RTL); every glyph, keyed by its `QRawFont` and glyph index but not its
  pixel size, gets one 1024 x 1024 R8 atlas entry: its signed distance to
  the outline at a 32 px base size (+-8 px range), from the outline filled
  at 4x through an exact Euclidean distance transform. The canvas holds one
  quad per glyph with the text colour (node labels near black, link labels
  dark blue) and fade in the vertex tint; the shader thresholds the linearly
  filtered distance per screen pixel for the fill and, 1.5 logical px
  further out, the white halo (uniforms). Crisp at any zoom and device pixel
  ratio; new values of a running simulation cost no rasterization. At most 64
  new distance fields per build (the first frames only); a label still
  missing one keeps its place and appears in the next build. A full atlas
  defers the labels that do not fit and is cleared at the start of the next
  build, never while quads refer to it. ID and value are two centred lines.
* Fade-in: a label the previous build did not draw fades in over 0.18 s, so
  labels revealed by zooming or panning do not pop; the view keeps building
  frames while one fades.
* `MapBgfxMapView::renderLabels()`: drawn after the network and the host's
  overlay, before the HUD, with the network's camera (the host's
  full-network screen translation included); rebuilt only when the camera, the viewport,
  the prepared scene, the atlas or the content (settings, symbology, value
  formats, hidden entities, renames) changed. Values: canonical metric values
  by render id in `MapNetworkRenderSymbology::node_values / link_values`
  (dense, NaN = none),
  converted with `mapBgfxLabelValueFormat()` (the legend ramp's affine unit
  conversion). Symbol boxes: the GPU network's dots and icons, or the host's
  icon rectangles (`setLabelSymbolRects()`, e.g. the GUI editor's
  bottom-anchored / centred icons).

## 2D imagery

* `map_bgfx_2d_imagery_layout.h` (pure C++, unit-tested): the imagery level is
  `round(continuous zoom)`, but quads are positioned with the *continuous*
  scale, so imagery stretches smoothly between levels and never steps. The
  test asserts continuity of a world point's screen position across x.5.
* A missing tile is covered by the nearest resident ancestor's sub-rectangle
  and by resident children on top; never a hole while tiles stream in.
* `map_bgfx_2d_imagery_gpu.*` with `map_bgfx_2d_imagery_atlas.*`: tiles are
  cells of a few atlas pages (edge-replicating 1-texel gutters, so bilinear
  sampling equals clamp-to-edge), LRU budget of 384 tiles or the tiles a
  frame draws plus 128 (the coarser fallbacks of not yet uploaded tiles;
  the globe draws 400+). Atlas uploads are budgeted nearest-first: at most
  **2 in 2D / 3 on the globe while the camera moves**, rising to **8 / 12
  when stationary**, so newly streaming tiles do not monopolize navigation
  frames. Set `AOWIS_MAP_2D_TILE_UPLOAD_BUDGET` or
  `AOWIS_MAP_3D_TILE_UPLOAD_BUDGET` to override the corresponding limit
  (integer 1–32) when comparing responsiveness with tile-fill throughput.
  Quad runs sharing the same atlas page typically use one draw call.
  The globe shares the atlas.
* Tile decode uses a background thread pool (up to four desktop workers,
  two on WebAssembly). `AOWIS_MAP_TILE_DECODE_THREADS` (integer 1–16)
  overrides the pool size for comparisons. With
  `AOWIS_MAP_RENDER_DIAGNOSTICS=1`, the periodic counters include successful
  2D/globe tile uploads and completion/cache-insertion work on the GUI thread;
  the GUI's `tools/report_2d_performance.py` prints their interval totals.
* Requests go to the shared `MapTileRepository` only when the visible tile set
  changes or a failed tile becomes retryable. Provider switches and deleted
  tiles clear the texture cache. A provider switch also supersedes the old
  provider's requests (`MapTileRepository::supersedeTileSources()`: queued
  ones dropped, those in flight no longer hold request slots), so the new
  provider's tiles go out at once; the globe refreshes its window tiles'
  imagery keys (`MapGlobeSurfacePreparation::refreshImageryKeys()`) and
  starts a new request batch per window.
* The "slide opacity" fades the imagery towards the background colour in the
  imagery shader.

Do not reintroduce integer-zoom geometry caching. Imagery LOD stays
independent of retained network geometry.

## Globe and terrain

The globe is a WGS84 ellipsoid with DEM terrain, imagery draped on it, a sky,
the network in 3D and an optional heatmap. All of it is drawn by
`MapBgfxMapView::renderGlobe()` into a D24S8 swapchain (native window) or
the host's depth-buffered framebuffer (host context). The camera is
`MapModel`'s orbit rig around the crosshair's pivot; `MapGlobeCamera` turns
it into the render matrices about a sticky ECEF render origin.

### Terrain surface

`MapBgfxGlobeSurface` (`map_bgfx_globe_surface.*`) coordinates the frame;
`MapBgfxGlobeTerrainGpu` owns the surface and wireframe programs, the frame
uniforms, the missing-imagery and polar-cap textures and the draw submission;
`MapBgfxGlobeSkyGpu` and `MapBgfxGlobeHeatmapGpu`
(`map_bgfx_globe_overlay_gpu.*`) own sky and heatmap resources.

* CPU work is `MapGlobeSurfacePreparation`: quadtree leaves with horizon and view-cone culling, ellipsoid and polar
  cap geometry relative to `MapGlobeCamera`'s sticky render origin, DEM
  requests, asynchronous terrain meshes, wireframe edges.
* Leaf refinement weighs a node's projected bounding-sphere size by its
  foreshortening (`mapGlobeQuadtreeNodeForeshortening()`): the square root of
  the cosine between the node's normal and the direction to the eye, at least
  0.125 (three levels), not applied within two radii of the eye and blended in
  by four. A sphere looks the same from every side, so before this a node
  near the horizon was refined as if seen head-on: cells several pixels wide
  and a fraction of a pixel tall (moiré in the wireframe, most of a tilted
  view's triangles). Coarser leaves draw coarser DEM tiles, so imagery and
  terrain follow together. CPU harness (1516x958, Alps; triangles at
  reduction 0 / 1): 2 km at 20 deg 1.05 M -> 0.64 M / 0.32 M -> 0.20 M,
  10 km at 10 deg 1.30 M -> 0.72 M / 0.37 M -> 0.24 M, 201 km at 34 deg
  0.71 M -> 0.44 M / 0.36 M -> 0.18 M; leaves -15..40 %; top-down views
  unchanged.
* Terrain level of detail is the DEM pyramid, never thinning inside a DEM
  tile (`map_globe_terrain_levels.h`). The terrain service's dataset catalog
  (`GET /terrain/v1/datasets`, `MapTerrainRepository::activeDataset()`) gives
  the dataset and its zoom range; nothing is requested before it arrives, and
  no provider or zoom is hardcoded. A leaf at imagery zoom z draws the DEM tile
  of zoom `clamp(z - reduction, max(8, minimum_zoom), maximum_zoom)` at its full
  resolution, `mapGlobeTerrainCellCount()` = 64 >> (z - DEM zoom) cells, so
  every vertex lies on a DEM sample. `reduction` is the "terrain detail
  reduction" setting (0..3, default 1): 0 draws every sample at the imagery's
  level of detail (about 4 px per cell), each step halves it. Past the
  dataset's `maximum_zoom` (its native detail) the DEM tile is subdivided, so
  the drawn terrain never claims finer detail than the source has. Mesh
  density depends on nothing camera-related, so a camera move rebuilds only
  when the leaves change; a catalog or setting change rebuilds the window
  (`MapGlobeSurfacePreparation::refreshTerrainLevels()`). Neighbours from the
  same DEM zoom share their edge vertices exactly; the edge stitching of the
  former in-tile LOD is gone.
* Every surface tile (terrain mesh, stand-in, flat) is its (cells + 1)^2 grid
  followed by a skirt: the four edges again, lowered by the tile's DEM
  elevation range plus one cell length (at least 20 m), times the vertical
  exaggeration (`mapGlobeAppendTileSkirt()` / `mapGlobeTileSkirtDepthM()` in
  `map_terrain_mesh_scheduler.h`). Neighbours disagree about a shared edge
  where their DEM zooms or stand-in state differ; the skirt
  hanging from the higher edge closes that crack and lies hidden below the
  neighbour elsewhere. It keeps the edge's texture coordinates, draws in the
  tile's one indexed draw, and is left out of ray picking, draping heights,
  terrain ray bounds and the wireframe (grid indices only).
* Sky (`vs_globe_sky.sc` / `fs_globe_sky.sc`, `MapBgfxGlobeSurface::drawSky()`):
  before the surface, one screen triangle whose corners carry their view
  rays adds the atmosphere's light onto the space colour (additive, no
  depth). Per pixel the ray is clipped to a spherical atmosphere about the
  Earth's centre (sea level: the radius under the eye; top 100 km) and to
  the ground; along it the optical depth of an exponential Rayleigh layer
  (8 km, per-channel coefficients: blue) and a Mie haze layer (1.2 km, grey:
  the white horizon) is integrated with 24 samples, in km, heights computed
  without cancelling Earth radii. The light of an evenly lit atmosphere,
  1 - exp(-beta * depth), is mapped so long paths reach the pale horizon
  colour and the short vertical path a deep blue. Hence, with no special
  case: a blue sky with a hazy horizon near the ground, darker with
  altitude, black above the atmosphere except for the thin bright band along
  the limb; the surface covers everything below the horizon. Drawn with the
  map tiles (not in the wireframe-only view); optional like the heatmap.
* Look-around panorama test mode (`MapBgfxMapView::setPanorama()`,
  `map_bgfx_panorama_grid.h`, `map_bgfx_panorama_gpu.h`): while a globe eye
  pose is set, an equirectangular 360-degree image replaces the globe, its
  overlay and the HUD; camera sampling, pacing, on-demand rendering and
  presentation are unchanged. It tells a slow map frame apart from uneven
  motion on a device. The image is drawn with the imagery program
  (`vs_imagery_2d` / `fs_imagery_2d`), so no shader binaries change: a
  96 x 54 grid over the screen, each vertex carrying the image coordinate of
  its view ray (the globe camera's, as the sky's; east/north/up at the eye),
  one transient vertex buffer, one draw. Cells across the image seam unwrap
  their u (the sampler repeats horizontally, clamps vertically); only the
  cell containing the zenith or nadir smears. Tests:
  `aowis-render-panorama-grid`, `aowis-render-panorama-gpu-mock`.
* Window vertices are a persistent pool (`MapGlobeSurfacePreparation`):
  a tile keeps its vertex range while it stays in the window, a dropped
  tile's range is reused by the next new tile of that size, the pool grows
  by half when full. A leaf change writes and uploads only the new tiles
  (`takeWindowVertexUploads()`); the whole pool goes up only after it was
  rebuilt (render-origin rebase, terrain levels change, free ranges
  outweighing used ones) or grown, since bgfx drops a dynamic buffer's
  content when it grows. Kept tiles are neither copied nor rescanned for
  their ray bounds. Before, every leaf change, i.e. nearly every frame of a
  camera move, copied and re-uploaded the whole window (4-5 MB per frame).
* A new tile gets its relief in the same frame: its own DEM's mesh when
  cached, else a stand-in resampled from the nearest cached coarser DEM
  (replaced on the mesh thread when its own DEM arrives); flat only before
  any DEM covering it is cached, or beyond an 8 ms meshing budget per
  rebuild (entering the globe, a render-origin rebase: those tiles start
  flat and get their mesh from the mesh thread instead of freezing a frame;
  flat terrain tiles come from the mesh builder too, the per-vertex
  geodetic conversion took ~45 ms for a full-detail window). New tiles
  waited for the mesh thread before
  and were drawn on the ellipsoid meanwhile, flickering between the relief
  around them during camera moves. Affordable because the mesh builder
  evaluates the Web Mercator inverse and the trigonometry per row and
  column (closed-form WGS84 per vertex): 64 cells 0.62 -> 0.09 ms.
  Frame harness (synthetic alps, 1920x1080, DEM 3 frames late, 8 per frame;
  zoom 60 -> 3 km, pan, orbit, zoom out; per phase): preparation CPU
  6-12 ms -> ~1 ms per frame, uploads 1.2-2.0 GB -> 17-44 MB, frames with
  flat relief tiles 150-180 -> 0 (only while the first coarse DEM loads).
* Hillshading (HUD toggle "hillshade", top left after "map", on by
  default, session state like the other toggles). Every surface vertex
  carries the terrain normal (ECEF) packed into four unsigned normalized
  bytes (`MapGlobePackedNormal`, vertex 20 -> 24 bytes). The mesh builder
  derives it from the DEM by central differences over one DEM cell, with a
  one-cell ring of samples around the grid taken from the tile's own DEM or,
  at DEM tile borders, from the four neighbour DEM tiles the request carries
  (`MapTerrainMeshRequest::terrain_neighbors`), so normals agree across
  tile borders (no shading seam). A neighbour not cached yet is reported in
  `missing_neighbor_mask` (edge normals one-sided meanwhile); its arrival
  rebuilds the mesh (`notifyTerrainTileAvailable()`). Flat tiles and caps
  carry the ellipsoid normal. Shaders (`globe_hillshade.sh`, used by
  `vs/fs_globe_surface` and `vs/fs_globe_surface_heatmap`): the vertex
  shader turns the light, given in east/north/up (`u_globe_light`:
  azimuth 315 deg, altitude 45 deg, the cartographic convention), into
  ECEF from the point's own up direction (position + `u_globe_origin`); the
  fragment shader scales the faded imagery by
  `mix(1, lambert / sin(altitude), strength)` (`u_globe_shading`: strength
  0.8, 0 when off): flat ground keeps its brightness, slopes facing the
  light brighten (up to 1.33x), the others darken; the heatmap colours stay
  unshaded on top. Verified: rendered with the compiled ESSL in WebGL 2
  (flat 100 -> 100, facing the light 133 (formula 133.1), facing away 20
  (20.0), off 100); normal tests (slope direction and size, no seam across
  a DEM tile border with the neighbour carried, missing-neighbour mask).
* GPU: window and cap vertices in dynamic buffers (window: the pool, patched
  per new tile and finished mesh; caps: full upload on a rebuild), one
  indexed draw per tile, depth test and write LessEqual. Window tiles have no index buffer
  of their own: the topology depends on the cell count only, so one static
  16-bit index buffer per cell count (topology from Render's
  `mapGlobeTileIndices()`, GPU cache in `MapBgfxGlobeTileIndexPool`) serves every tile, drawn with the tile's first vertex as start
  vertex (bgfx applies it as base vertex on GL/WebGL, D3D11, Vulkan and
  Metal). A window rebuild writes and uploads no indices (at 64 cells that
  was more than half of a tile's upload); picking and the wireframe read the
  same shared lists. The caps keep their own small index buffer. The camera's canonical OpenGL clip
  space gets the [0, 1] depth correction where bgfx reports no homogeneous
  depth (Direct3D, Vulkan, Metal). `QMatrix4x4::constData()` is column-major,
  as bgfx's `mul(matrix, vector)` expects.
* Imagery from the shared 2D atlas: a tile samples its own cell, or the
  covering part of its nearest resident ancestor (`map_bgfx_globe_imagery.h`,
  texture coordinates mapped into the ancestor's cell, no CPU compositing),
  or a solid colour (nothing loaded; polar caps). At most 12 uploads per
  frame (at 8 the backlog while zooming or turning showed as blank tiles);
  requests go to the tile repository in one batch per window.
* View-cone culling covers the frustum's corners: the cone's half-angle is
  the vertical half FOV widened by the aspect ratio, a node is kept when its
  direction is off-axis by at most that plus its angular radius asin(r/d)
  (also with its centre behind the eye), plus 0.1 rad slack (the leaves
  are selected every frame; the slack only prefetches). With the vertical FOV only, wide viewports
  lost terrain sections at their left and right edges.
* DEM still loading: culling bounds a quadtree node by its own DEM tile's
  elevation range, else by its nearest cached ancestor's, else by the whole
  possible range (ellipsoid to 9 km). The selection runs every frame:
  each DEM tile is looked up once per selection (by position; a repository
  lookup builds a string key), and the bounding spheres use the closed-form
  geodetic conversion. A node bounded on the ellipsoid under
  high terrain used to leave the view cone close to the camera, was never
  refined, so its DEM was never requested (a permanent hole next to the eye).
  A leaf whose own DEM is not cached is drawn from the nearest cached
  coarser DEM tile, resampled at its own DEM address
  (`mapTerrainTileFromAncestor()`, the mesh built on the mesh thread,
  `scheduleAncestorTerrainStandIns()`, again when a finer one arrives): same
  grid as the final mesh, which then replaces it in place; flat on the
  ellipsoid only where no DEM is cached at all.
* Globe shaders (`vs/fs_globe_surface`, `vs/fs_globe_wireframe`) declare
  their vec3 positions in `varying_globe.def.sc`; the 2D shaders keep
  `varying.def.sc` (and byte-identical binaries).

### Terrain-aware navigation

`src/map/render/map_globe_terrain_navigation.*`:

* Camera height: the orbit rig's vertical offset follows the DEM
  perpendicularly beneath the eye; while rotating (frozen pivot) an eye-only
  collision lift keeps `MinViewGlobeCameraGroundClearanceM`, released into the
  offset when the orbit ends. Monotonic exponential filter (0.22 s / 0.16 s),
  settled at a quarter pixel. Camera changes take a step at once (and request
  the DEM tile under the eye); the surface's frame loop continues the filter
  while `following()` (no extra timer).
* Rays against the drawn surface (`MapGlobeSurfacePreparation`'s retained
  meshes, ellipsoid elsewhere): the pointer coordinate with DEM altitude,
  `globeSurfaceRayHitAtScreen()`, and the crosshair's surface point for
  pans (`globeSurfacePointAtScreen()`). Hosts use them for their own
  navigation and readouts (the GUI's footer altitude, Mobile's pan radius).
* Pans (`MapModel::panByPixelsGlobe()`): the ground under the crosshair
  moves with the pointer. The rays at the crosshair minus the pointer delta
  and at the crosshair meet the sphere through the drawn surface at the
  crosshair (about the Earth's centre); the camera rig turns rigidly, in
  double precision, from one point onto the other (`rotateGlobe()`). Speed
  thus follows the terrain at the crosshair (height, distance, viewing
  angle), not a fixed angle per pixel; nearby rays never land on unrelated
  relief, and a ray past the horizon takes its closest approach to the
  sphere. Keys turn the rig the same way along the great circle of the
  pressed direction. A rigid turn has no preferred axis, so pans cross the
  poles: the longitude swings by 180 degrees there and the yaw, taken from
  the turned screen-right direction, swings with it. North-up (compass) holds
  the yaw against the turn's small heading drift up to 80 degrees latitude;
  closer to a pole north spins around the crosshair with every step, so the
  pan releases the lock there and stays rigid.
* A vertical exaggeration change rebuilds the terrain meshes (vertex
  patches); a network opened in Globe mode requests its DEM tiles at once.
* Eye pose: a host can replace the orbit rig by a free eye
  (`MapModel::setGlobeEyePose()`: ECEF eye, screen-axes orientation and a
  magnification of 0.5 to 8 that narrows the 45 degree field of view;
  `zoomGlobeEyeBy()`). Render, rays and picking then use that eye
  (`MapCameraViewState::eye_pose`, invalidated through
  `globe_pose_revision`); `std::nullopt` returns to the orbit rig.

### Network on the globe

`MapBgfxGlobeNetwork` (`map_bgfx_globe_network.*`) adapts the snapshot,
picks and fills the per-frame camera uniforms. `MapBgfxGlobeGpu` submits the
draws; `MapBgfxGlobeBufferPool` owns the GPU streams;
`MapBgfxGlobeResourcePool` owns programs, uniforms and the model textures
(Render builds their mip chain from the host's tank/reservoir images);
`MapBgfxGlobeVertexLayouts` builds the five vertex layouts.

* Geometry is `MapGlobeNetworkScene`
  (WGS84 placement relative to a retained network origin, adaptive geodesic
  subdivision, metre/pixel sizing, declutter, X-Ray classification against
  the DEM the globe shows). Its inputs are stored and applied on the next
  globe frame (`synchronize()`), so the 2D map never builds globe geometry.
  GPU streams are re-uploaded per category (links/nodes, highlights,
  chevrons, icons + models, X-Ray runs, junctions), but never for colours: the base streams (links, nodes,
  junctions, icons, chevrons, X-Ray runs) hold entity references in their
  colour fields (`mapNetworkSetEntityReference()`: render id, node/link,
  chevron orientation) and the shaders colour them from the shared entity
  data (GPU colouring above). Only the selection / diagnostic outlines keep
  vertex colours. Flow chevrons are built for every link in both
  orientations; the link's current direction shows one set, so a time step
  neither rebuilds nor uploads them.
* Shaders (`vs_globe_link`,
  `vs_globe_flow`, `fs_globe_link(_xray)`, `vs/fs_globe_node`,
  `vs/fs_globe_junction(_xray)`, `vs/fs_globe_icon`, `vs/fs_globe_model`),
  their own `varying_globe_network.def.sc` and shared uniforms/helpers in
  `globe_network.sh`. Pixel-space segment coordinates are interpolated
  screen-linearly (passed times w, divided in the fragment shader; WebGL 2
  has no `noperspective`).
* Junctions: sphere impostors writing the sphere's depth (`gl_FragDepth`,
  NDC to window depth per bgfx's homogeneous-depth cap). Expanded to six
  vertices each (no instancing); colour from the entity data in the vertex
  shader: error red / stale grey, else selection cyan, else the symbology;
  hidden junctions collapse. Selected icons take the selection colour.
* Draw order and states: Solid draws
  the network once without depth test first; X-Ray draws the buried runs
  (dashed) and junctions (dithered) through the terrain; Hide lets the
  terrain occlude them. Premultiplied blending; tank/reservoir models with
  back-face culling. The chevrons keep their 8 px pull towards the eye but
  have no extra depth bias (bgfx has no depth-bias state).
* Demand points have no elevation (not part of the EPANET model): they lie
  on the terrain exactly as it is drawn, the network lift above it
  (`MapGlobeNetworkScene::setSurfaceHeightResolver()`: the window tile
  covering the point, then a vertical ray against the two triangles of its
  grid cell, `MapGlobeSurfacePreparation::drawnSurfaceHeightAt()`; the bare
  ellipsoid where no relief is drawn yet). The former ray against every
  tile's mesh cost ~40 ms per frame for 3000 demand points while terrain
  was loading. Whenever the drawn terrain changes (window, applied meshes:
  `MapBgfxGlobeSurface::surfaceRevision()`) they are moved in place before
  the network is drawn in that same frame, without a network rebuild. Their
  attachment lines end on the junction / pipe at its own elevation
  (`NetworkRenderDemandPointAttachment::attachment_elevation_m`).
* The underground mode is set through `setUndergroundMode()` (default
  X-Ray) from the globe HUD's buttons. X-Ray
  classification runs 500 ms after the last camera change or DEM arrival,
  once no terrain mesh is being built.

### Globe heatmap

* Markers, radius and solid centre (node positions, ramp colours of the heatmap metric; a pixel radius is
  converted at the orbit distance when the heatmap data or style change).
  `MapGlobeHeatmapScene` gives each window tile its stamps in the tile's
  256 px space.
* GPU bake: atlas pages of 2048 x 2048 (64 cells, up to 4 pages, created as
  needed) rendered in four bake views reserved alongside the current map
  view. `bgfx::setViewOrder()` presents those bake passes before the map pass.
  View IDs are allocated process-wide, not permanently fixed to 0–4. Per cell a clear quad (no blending), then the stamp
  quads (`vs/fs_globe_heatmap_bake`, premultiplied source-over, clamped to
  the cell). The bake's Y follows `Caps::originBottomLeft`, so the cell is
  sampled with the tile's own v-down texture coordinates on every backend.
* Cells per tile and scene revision (LRU over pages, 24 bakes per frame);
  a tile without stamps has no cell. Tiles with a cell are drawn by
  `vs/fs_globe_surface_heatmap` (the imagery, then the heatmap with the
  symbology opacity: rgb * (1 - a * opacity) + heat * opacity).

### Globe picking

`MapBgfxGlobeNetwork::hitTest()`, used by `MapBgfxMapView::hitTest()` in
Globe mode for clicks and hover. Tank/reservoir 3D models by screen ray
against their triangles, then junction spheres (projected radius), icons
(projected billboard rectangle with the icon shader's depth-dependent size),
generic nodes, then link segments, with the globe's larger hit radii.
Positions are projected with the camera's matrix for the network origin, the
one the network is drawn with; pending inputs are applied first. Picking
deliberately reaches entities behind or under the terrain: buried and hidden parts of the network stay selectable.

### Globe HUD

* Layout: "wireframe", "map" and "hillshade" toggles next to the 2D/3D
  switch, the "Terrain mesh" progress next to the downloads (x / y, or
  "building..."; only after 0.6 s of continuous mesh building, gone 0.4 s
  after it ended, the HUD animating meanwhile so an idle view presents that
  frame), and bottom left the four camera sliders (network height, tilt,
  distance, vertical exaggeration), the X-Ray / Hide / Solid buttons, the
  compass with its heading, and the globe scale (above the controls when the
  map is too narrow).
* `MapBgfxHud` stays pure: it reports slider and compass interactions as
  commands with a phase (Begin / Move / End) and a number (slider fraction,
  pointer delta, wheel notches), collected after every pointer event and
  delivered queued, in order, through the HUD action handler. Right press on
  a slider resets it.
* The host applies those commands to `MapModel` and supplies the per-frame
  state (yaw, slider positions and labels in its units, the globe scale)
  through `setGlobeHudStateProvider()`. The GUI's
  `MapBgfxGlobeHudController` is that host side.
* Toggles and underground buttons map to `setTerrainWireframeVisible()`,
  `setMapTilesVisible()`, `setTerrainHillshadeVisible()` and
  `setUndergroundMode()`.

## Renderer selection

bgfx selects the renderer (`RendererType::Count`: platform ranking with
fall-through on init failure). AOWIS does not reimplement that; it only:

* compiles in the renderers it serves (`AOWIS_BGFX_RENDERERS`,
  `cmake/aowis_bgfx.cmake`): Linux OpenGL + Vulkan, Windows Direct3D 11 +
  OpenGL, macOS Metal, WASM and Android OpenGL ES 3 (WebGL 2 in the browser);
* embeds the matching pre-compiled shader families
  (`:/aowis/map/bgfx/<glsl|essl|spirv|dx11|metal>/`, see "bgfx build");
* rejects a pick without embedded shaders and retries with the next supported
  renderer that has them (`map_bgfx_backend.h`);
* honours `AOWIS_BGFX_RENDERER=opengl|vulkan|d3d11|metal|auto` for diagnosis
  (environment only, not a setting).

A Qt Quick host (`HostContext`) uses the API of its own context instead:
OpenGL ES on Android, desktop OpenGL on Linux.

A software rasterizer (llvmpipe / WARP) is not selected automatically yet.

## bgfx build

AOWIS does not use bgfx.cmake. `cmake/aowis_bgfx.cmake` builds bx, bimg and
bgfx as three upstream unity translation units (TUs) with only `AOWIS_BGFX_RENDERERS` compiled in,
no video, no software texture decoders (so no ASTC encoder), single-threaded
(`BGFX_CONFIG_MULTITHREADED=0`). Desktop Linux links only threads, `dl` and
`rt` (bgfx dlopen()s EGL/GL/wayland-egl/Vulkan); Android has no `librt`, and
the Android host links EGL, GLESv2 and `android` itself. Measured on Linux (OpenGL + Vulkan): 3 TUs / ~55 s CPU instead of
76 TUs / ~262 s.

The GUI checks the bgfx submodules out shallow and sparse
(`tools/bgfx_sparse_checkout.sh` in the GUI): ~15 MB of bgfx instead of
~270 MB.

**Shaders are pre-compiled and committed** in `src/map/bgfx/shaders/bin/
<family>/` (`cmake/aowis_bgfx_shaders.cmake`), so no desktop, WASM or Android
build builds a shader compiler (bgfx.cmake's shaderc was ~690 TUs incl.
Tint/Dawn). They are embedded as committed; nothing checks that they match
their sources, so after editing a `.sc` file, or after updating bgfx,
configure with `-DAOWIS_BGFX_SHADER_TOOLS=ON` (from a GUI checkout:
`./compile_linux.sh --shader-tools`, which also checks out the shader-compiler
sources). That builds `aowis-bgfx-shaderc` (`cmake/aowis_bgfx_shaderc.cmake`: shaderc
without Tint/Dawn/DXC/spirv-reduce, ~280 TUs, once), recompiles glsl, essl,
spirv, metal and dx11 into the build tree (again whenever a shader input
changes) and copies them into `src/map/bgfx/shaders/bin`. Commit that
directory in Render, together with the new bgfx gitlinks.

dx11 (DXBC, for the Windows build's Direct3D 11 renderer) is compiled by
Microsoft's `d3dcompiler_47.dll`, which bgfx ships in `tools/bin/windows`
(part of the `--shader-tools` sparse checkout). A Windows host loads it
directly (copied next to `aowis-bgfx-shaderc.exe`); Linux and macOS run it
through bgfx's d3d4linux helper under **Wine** (any `wine` on `PATH`, or
`-DAOWIS_BGFX_WINE=/path/to/wine`; it uses its own prefix in the build tree).
The build sets `WINEDLLOVERRIDES=d3dcompiler_47=n`: Wine otherwise loads its
own builtin compiler (vkd3d-shader) even for a DLL given by full path, which
fails on `bgfx_shader.sh` ("Function \"reversebits\" is not defined", error
codes E5xxx) and would not produce Microsoft's DXBC anyway.
Without Wine the dx11 family is skipped with a warning and the committed
binaries stay as they are; a Windows build without current dx11 binaries
warns and runs on OpenGL.

## Frame clock and smooth zoom (do not regress)

* **One clock.** Once bgfx is active, the view is the only navigation clock:
  each presented frame calls the host's navigation frame handler
  (`setNavigationFrameHandler()`; the GUI's
  `MapWidget::advanceNavigationFrame(dt)`) *before* sampling
  `MapModel::cameraViewState()`. The host starts no navigation timer of its
  own. Two independent
  timers plus the vsync-blocking `bgfx::frame()` on the GUI thread advanced the
  camera by 0, 1 or 2 steps per displayed frame (simulated: ~28 % frozen
  frames with only 0-3 ms dispatch jitter). That was the keyboard-zoom judder.
* **Paced dt.** `MapFramePacer` quantizes measured frame time to whole refresh
  periods with carry, so a vsync-locked stream gets exactly one period per frame
  and camera time never drifts from wall time by more than half a period.
* **Desktop** uses a PreciseTimer 1 ms shorter than the refresh period; the
  vsync swap then locks presentation. **WASM** uses
  `emscripten_request_animation_frame_loop` (browser vsync), never QTimer.
* **Zoom motion** (the GUI's `map_zoom_animation.h`): wheel, buttons and keys
  only move a target; the camera follows it as an exactly integrated critically damped
  spring. Velocity is continuous on press, tap->hold, reversal and release;
  results are frame-rate independent. No integer zoom anywhere in navigation.
* The camera uses **logical** pixels (`logicalSize()`); the view rectangle uses
  physical pixels. Using physical pixels for the camera was one octave off at
  devicePixelRatio 2.
