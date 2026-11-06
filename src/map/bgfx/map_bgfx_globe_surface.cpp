#include "map/bgfx/map_bgfx_globe_surface.h"
#include "map/render/map_render_diagnostics.h"

#include "map/bgfx/map_bgfx_backend.h"
#include "map/bgfx/map_bgfx_globe_imagery.h"
#include "map/core/map_model.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_tile_repository.h"

#include <QDebug>
#include <QImage>
#include <QMatrix4x4>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
// The QRhi globe's colours: tiles whose imagery (and every ancestor's) is
// not loaded yet, and the polar caps beyond Web Mercator's latitude limit.
const QColor GlobeMissingTileColor(18, 58, 72);
const QColor GlobePolarCapColor(235, 240, 245);
// Hillshading light (cartographic convention) and how far it modulates the
// imagery (0: not at all, 1: full lambert / sin(altitude)).
constexpr double GlobeHillshadeAzimuthDeg = 315.0;
constexpr double GlobeHillshadeAltitudeDeg = 45.0;
constexpr double GlobeHillshadeStrength = 0.8;
const float kWholeTexture[4] = {0.0f, 0.0f, 1.0f, 1.0f};
// Heatmap atlas pages: 2048 x 2048, 8 x 8 cells of the scene's 256 px.
constexpr int kHeatmapPageSize = MapBgfxGlobeHeatmapGpu::PageSize;
constexpr int kHeatmapCellSize = MapGlobeHeatmapScene::TextureSize;
constexpr int kHeatmapCellsPerRow = kHeatmapPageSize / kHeatmapCellSize;
constexpr int kHeatmapCellsPerPage = kHeatmapCellsPerRow * kHeatmapCellsPerRow;
// Tiles (re)baked per frame: the stamp layout is CPU work per tile.
constexpr int kHeatmapBakesPerFrame = 24;
// The six corners of a stamp quad.
const float kStampCorners[6][2] = {
    {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

// Heatmap cell key of a window tile: zoom, unwrapped x, y.
quint64 heatmapTileKey(int zoom, int virtual_x, int tile_y)
{
    return (quint64(quint32(zoom)) << 58) | ((quint64(quint32(virtual_x)) & 0x1fffffffu) << 29)
        | (quint64(quint32(tile_y)) & 0x1fffffffu);
}
}

MapBgfxGlobeSurface::MapBgfxGlobeSurface(MapModel *map_model)
    : map_model(map_model),
      surface_preparation(map_model),
      heatmap_scene(MapModel::MaxZoom)
{
}

MapBgfxGlobeSurface::~MapBgfxGlobeSurface() = default;

bool MapBgfxGlobeSurface::initialize()
{
    // MapGlobeSurfaceVertex: x, y, z, u, v (floats), the packed normal
    // (4 normalized bytes); wireframe: x, y, z.
    this->surface_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Uint8, true)
        .end();
    if (this->surface_layout.getStride() != sizeof(MapGlobeSurfaceVertex))
        qWarning() << "bgfx globe surface: vertex layout does not match MapGlobeSurfaceVertex";
    this->wireframe_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .end();
    this->terrain_gpu.initialize(
        &MapBgfxBackend::loadProgram,
        {std::uint8_t(GlobeMissingTileColor.red()), std::uint8_t(GlobeMissingTileColor.green()),
         std::uint8_t(GlobeMissingTileColor.blue()), std::uint8_t(255)},
        {std::uint8_t(GlobePolarCapColor.red()), std::uint8_t(GlobePolarCapColor.green()),
         std::uint8_t(GlobePolarCapColor.blue()), std::uint8_t(255)});
    // Both optional overlays are owned by Render. Failure retains the
    // previous plain-terrain fallback behavior.
    if (!this->heatmap_gpu.initialize(&MapBgfxBackend::loadProgram))
        qWarning() << "bgfx globe heatmap unavailable";
    if (!this->sky_gpu.initialize(&MapBgfxBackend::loadProgram))
        qWarning() << "bgfx globe sky unavailable";
    // Everything goes up again on the (new) device.
    this->window_upload_pending = true;
    this->cap_upload_pending = true;
    this->wireframe_upload_pending = true;
    this->window_imagery.clear();
    if (!ready())
    {
        qWarning() << "bgfx globe surface unavailable: programs"
                   << this->terrain_gpu.ready();
        release();
        return false;
    }
    return true;
}

bool MapBgfxGlobeSurface::ready() const
{
    return this->terrain_gpu.ready();
}

void MapBgfxGlobeSurface::destroyBuffers()
{
    this->terrain_buffers.release();
    this->tile_index_pool.release();
}

void MapBgfxGlobeSurface::releaseHeatmap()
{
    for (QVector<quint64> &owners : this->heatmap_slot_owners)
        owners.clear();
    this->heatmap_cells.clear();
    this->heatmap_gpu.release();
}

void MapBgfxGlobeSurface::drawSky(bgfx::ViewId view, const MapGlobeCameraBasis &basis,
                                  const GeoWgs84Ellipsoid::EcefPositionD &render_origin,
                                  const QSize &logical_viewport)
{
    // The eye in ECEF and its height: the atmosphere is a sphere about the
    // Earth's centre whose sea level is the radius under the eye.
    GeoWgs84Ellipsoid::EcefPositionD eye;
    eye.x = render_origin.x + double(basis.eye.x());
    eye.y = render_origin.y + double(basis.eye.y());
    eye.z = render_origin.z + double(basis.eye.z());
    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
    double height_m = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(eye, &longitude_deg, &latitude_deg, &height_m))
        return;
    const double eye_radius_m = std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z);
    // View rays: forward (up x right, the lookAt basis) plus the screen's
    // half-extents along right and up (vertical FOV, aspect ratio).
    const QVector3D forward = QVector3D::crossProduct(basis.up, basis.right).normalized();
    const double tan_half_height = std::tan(qDegreesToRadians(MapModel::GlobeFieldOfViewDeg * 0.5));
    const double aspect = double(std::max(1, logical_viewport.width()))
        / double(std::max(1, logical_viewport.height()));
    const QVector3D right = basis.right.normalized() * float(tan_half_height * aspect);
    const QVector3D up = basis.up.normalized() * float(tan_half_height);
    const float forward_uniform[4] = {forward.x(), forward.y(), forward.z(), 0.0f};
    const float right_uniform[4] = {right.x(), right.y(), right.z(), 0.0f};
    const float up_uniform[4] = {up.x(), up.y(), up.z(), 0.0f};
    const float vertical_uniform[4] = {float(eye.x / eye_radius_m), float(eye.y / eye_radius_m),
                                       float(eye.z / eye_radius_m), 0.0f};
    const float params_uniform[4] = {float(height_m / 1000.0), float((eye_radius_m - height_m) / 1000.0),
                                     0.0f, 0.0f};
    MapBgfxGlobeSkyGpu::Frame frame;
    std::copy_n(forward_uniform, 4, frame.forward.begin());
    std::copy_n(right_uniform, 4, frame.right.begin());
    std::copy_n(up_uniform, 4, frame.up.begin());
    std::copy_n(vertical_uniform, 4, frame.vertical.begin());
    std::copy_n(params_uniform, 4, frame.params.begin());
    this->sky_gpu.draw(view, frame);
}

void MapBgfxGlobeSurface::release()
{
    this->sky_gpu.release();
    releaseHeatmap();
    destroyBuffers();
    this->terrain_gpu.release();
    this->window_imagery.clear();
}

void MapBgfxGlobeSurface::setTileRepository(MapTileRepository *tile_repository)
{
    this->tile_repository = tile_repository;
    this->window_tiles_requested = false;
}

void MapBgfxGlobeSurface::setTerrainRepository(MapTerrainRepository *terrain_repository)
{
    this->surface_preparation.setTerrainRepository(terrain_repository);
}

void MapBgfxGlobeSurface::notifyTerrainTileAvailable(const QString &key)
{
    this->surface_preparation.notifyTerrainTileAvailable(key);
}

void MapBgfxGlobeSurface::requestAgain()
{
    this->window_tiles_requested = false;
}

void MapBgfxGlobeSurface::providerChanged()
{
    this->surface_preparation.refreshImageryKeys();
    this->window_tiles_requested = false;
}

void MapBgfxGlobeSurface::requestTerrainForCurrentView(const QSize &logical_viewport)
{
    if (!this->surface_preparation.requestTerrainForCurrentView(logical_viewport))
        return;
    // The window was rebuilt: new imagery requests; its new vertex ranges go
    // up with the next frame (takeWindowVertexUploads()).
    this->wireframe_upload_pending = true;
    this->window_tiles_requested = false;
}

void MapBgfxGlobeSurface::invalidateTerrainView()
{
    this->surface_preparation.invalidateTerrainView();
}

void MapBgfxGlobeSurface::invalidateTerrain()
{
    // The finished meshes arrive as vertex patches (render()).
    this->surface_preparation.invalidateTerrain();
}

void MapBgfxGlobeSurface::setHeatmapOverlay(const QVector<MapGlobeHeatmapMarker> &markers, double radius_m,
                                            double solid_fraction)
{
    // A new revision re-bakes the cells as the tiles are drawn.
    this->heatmap_scene.setOverlay(markers, radius_m, solid_fraction);
}

void MapBgfxGlobeSurface::setWireframeVisible(bool visible)
{
    if (this->surface_preparation.setWireframeVisible(visible))
        this->wireframe_upload_pending = true;
}

void MapBgfxGlobeSurface::setMapVisible(bool visible)
{
    if (this->map_visible == visible)
        return;
    this->map_visible = visible;
    if (visible)
        this->window_tiles_requested = false;
}

void MapBgfxGlobeSurface::setHillshadeVisible(bool visible)
{
    this->hillshade_visible = visible;
}

bool MapBgfxGlobeSurface::hasPendingTerrainMeshes() const
{
    return this->surface_preparation.hasPendingTerrainMeshes();
}

void MapBgfxGlobeSurface::terrainMeshProgress(int *completed, int *total, bool *active) const
{
    this->surface_preparation.terrainMeshProgress(completed, total, active);
}

void MapBgfxGlobeSurface::uploadWindowGeometry()
{
    const QVector<MapGlobeSurfaceVertex> &vertices = this->surface_preparation.windowVertices();
    if (this->terrain_buffers.uploadWindow(this->surface_layout, vertices.constData(),
                                           std::size_t(vertices.size()), sizeof(MapGlobeSurfaceVertex)))
        this->window_upload_pending = false;
}

void MapBgfxGlobeSurface::uploadCapGeometry()
{
    const QVector<MapGlobeSurfaceVertex> &vertices = this->surface_preparation.capVertices();
    const QVector<quint32> &indices = this->surface_preparation.capIndices();
    if (this->terrain_buffers.uploadCaps(this->surface_layout, vertices.constData(),
                                         std::size_t(vertices.size()), sizeof(MapGlobeSurfaceVertex),
                                         indices.constData(), std::size_t(indices.size())))
        this->cap_upload_pending = false;
}

void MapBgfxGlobeSurface::uploadWireframe()
{
    const QVector<MapGlobeSurfaceWireframeVertex> &vertices = this->surface_preparation.wireframeVertices();
    if (this->terrain_buffers.uploadWireframe(this->wireframe_layout, vertices.constData(),
                                              std::size_t(vertices.size()),
                                              sizeof(MapGlobeSurfaceWireframeVertex)))
        this->wireframe_upload_pending = false;
}

void MapBgfxGlobeSurface::requestImagery()
{
    if (this->tile_repository == nullptr || this->window_tiles_requested)
        return;
    // One batch per provider: a switch starts a new one, so the new
    // provider's requests outrank the old one's. Not one per window
    // rebuild: while the camera moves the window is rebuilt nearly every
    // frame, and a new batch each time re-sorted and pruned the request
    // queue and dropped still-wanted requests (uneven tile streaming).
    const quint64 batch = this->tile_repository->beginTileRequestBatch(
        this, QStringLiteral("bgfx-globe|%1").arg(this->map_model->tileSourcePath(MapModel::MinZoom)));
    for (const MapGlobeSurfaceTile &tile : this->surface_preparation.windowTiles())
    {
        const int source_zoom = this->map_model->imageryTileZoom(tile.zoom);
        if (source_zoom > tile.zoom)
            continue;
        const int offset = tile.zoom - source_zoom;
        const int source_x = tile.tile_x >> offset;
        const int source_y = tile.tile_y >> offset;
        if (this->map_model->tileSourcePath(source_zoom).isEmpty())
            continue;
        const QString source_key = this->map_model->tileCacheKey(source_x, source_y, source_zoom);
        if (this->tile_repository->tile(source_key) != nullptr)
            continue;
        const int priority = mapGlobeSurfaceTileRequestPriority(
            tile.tile_x, tile.tile_y, tile.zoom,
            this->map_model->centerLon(), this->map_model->centerLat());
        this->tile_repository->requestTile(
            this->map_model->tileEndpoint(source_x, source_y, source_zoom),
            source_key, source_x, source_y, priority, batch, true);
    }
    this->window_tiles_requested = true;
}

void MapBgfxGlobeSurface::resolveImagery(MapBgfx2dImageryGpu *imagery, FrameResult *result,
                                         int upload_budget)
{
    const QVector<MapGlobeSurfaceTile> &tiles = this->surface_preparation.windowTiles();
    this->window_imagery.resize(tiles.size());
    // Pass 1: everything already resident is marked as used in this frame
    // first, so the uploads of pass 2 never take a cell a tile draws.
    struct Unresolved
    {
        int index = 0;
        // Levels above the tile of the resident source found in pass 1
        // (tile.zoom + 1: none); pass 2 only tries nearer ones.
        int resident_level = 0;
    };
    QVector<Unresolved> unresolved;
    for (qsizetype index = 0; index < tiles.size(); ++index)
    {
        const MapGlobeSurfaceTile &tile = tiles.at(index);
        TileImagery &state = this->window_imagery[index];
        state.texture = this->terrain_gpu.missingTexture();
        std::copy_n(kWholeTexture, 4, state.uniform);
        MapBgfx2dImagery::TileId id;
        id.zoom = tile.zoom;
        id.x = tile.tile_x;
        id.y = tile.tile_y;
        float rect[4];
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        if (imagery->atlasRect(id, &texture, rect))
        {
            state.texture = texture;
            MapBgfxGlobeImagery::tileUniform(rect, MapBgfxGlobeImagery::ancestor(id, 0),
                                             state.uniform);
            continue;
        }
        // Meanwhile the nearest resident ancestor (atlas lookups only).
        int resident_level = 1;
        for (; resident_level <= tile.zoom; ++resident_level)
        {
            const MapBgfxGlobeImagery::Source source =
                MapBgfxGlobeImagery::ancestor(id, resident_level);
            if (imagery->atlasRect(source.tile, &texture, rect))
            {
                state.texture = texture;
                MapBgfxGlobeImagery::tileUniform(rect, source, state.uniform);
                break;
            }
        }
        unresolved.append(Unresolved{int(index), resident_level});
    }
    if (this->tile_repository == nullptr)
        return;
    // Pass 2: uploads, nearest-to-the-camera order is the window's order
    // (the requests carry priorities; the budget only spreads the GPU work).
    // A tile's own image first; without one, the nearest ancestor the
    // repository has (zooming in: the parent level is usually cached).
    for (const Unresolved &entry : unresolved)
    {
        const MapGlobeSurfaceTile &tile = tiles.at(entry.index);
        TileImagery &state = this->window_imagery[entry.index];
        MapBgfx2dImagery::TileId id;
        id.zoom = tile.zoom;
        id.x = tile.tile_x;
        id.y = tile.tile_y;
        for (int levels_up = 0; levels_up < entry.resident_level; ++levels_up)
        {
            const MapBgfxGlobeImagery::Source source = MapBgfxGlobeImagery::ancestor(id, levels_up);
            float rect[4];
            bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
            // Uploaded meanwhile for a neighbour sharing this ancestor.
            if (imagery->atlasRect(source.tile, &texture, rect))
            {
                state.texture = texture;
                MapBgfxGlobeImagery::tileUniform(rect, source, state.uniform);
                break;
            }
            const QString key = levels_up == 0
                ? tile.imagery_key
                : this->map_model->tileCacheKey(source.tile.x, source.tile.y, source.tile.zoom);
            const QImage *image = this->tile_repository->tile(key);
            if (image == nullptr)
                continue;
            if (result->uploads >= upload_budget)
            {
                result->pending = true;
                break;
            }
            if (imagery->uploadTexture(source.tile, *image)
                && imagery->atlasRect(source.tile, &texture, rect))
            {
                ++result->uploads;
                MapRenderDiagnostics::add(MapRenderDiagnostics::Counter::TileGlobeUpload);
                state.texture = texture;
                MapBgfxGlobeImagery::tileUniform(rect, source, state.uniform);
                // With an ancestor standing in, the tile's own image is
                // taken as soon as it arrives (signalTileAvailable redraws).
            }
            break;
        }
    }
}

MapBgfxGlobeSurface::FrameResult MapBgfxGlobeSurface::render(
    bgfx::ViewId view, const MapGlobeCamera &camera, MapBgfx2dImageryGpu *imagery,
    const FrameInputs &inputs)
{
    FrameResult result;
    if (!ready() || this->map_model == nullptr || !inputs.logical_viewport.isValid())
        return result;

    // --- Preparation (backend-neutral, shared with the QRhi globe) ---------
    // The surface follows the camera's sticky origin; a rebase rebuilds the
    // window and the caps relative to it.
    if (this->surface_preparation.setRenderOriginEcef(camera.renderOriginEcef()))
    {
        this->cap_upload_pending = true;
        this->wireframe_upload_pending = true;
        ++this->surface_revision;
    }
    if (this->surface_preparation.ensureCapsBuilt())
    {
        this->cap_upload_pending = true;
        this->wireframe_upload_pending = true;
    }
    if (this->surface_preparation.prepareVisibleWindow(inputs.logical_viewport))
    {
        this->wireframe_upload_pending = true;
        this->window_tiles_requested = false;
        ++this->surface_revision;
    }
    // Window rebuilds write only their new tiles' ranges of the persistent
    // vertex pool; the whole pool goes up only when it was rebuilt or grew.
    QVector<MapGlobeSurfaceVertexPatch> patches;
    bool full_upload = false;
    this->surface_preparation.takeWindowVertexUploads(&patches, &full_upload);
    if (full_upload)
        this->window_upload_pending = true;
    // Finished terrain meshes replace their tile's vertex range in place.
    QVector<MapGlobeSurfaceVertexPatch> mesh_patches;
    if (this->surface_preparation.applyReadyTerrainMeshes(&mesh_patches))
    {
        patches += mesh_patches;
        this->wireframe_upload_pending = true;
        ++this->surface_revision;
    }
    this->surface_preparation.requestMissingTerrainTiles();
    this->surface_preparation.scheduleReadyTerrainMeshes();

    if (!this->window_upload_pending && bgfx::isValid(this->terrain_buffers.window()))
    {
        const QVector<MapGlobeSurfaceVertex> &vertices = this->surface_preparation.windowVertices();
        for (const MapGlobeSurfaceVertexPatch &patch : patches)
        {
            // Render validates retained-window identity and GPU byte bounds.
            // A rejected patch requests a complete refresh on this frame.
            if (patch.first_vertex < 0 || patch.vertex_count < 0
                || !this->terrain_buffers.patchWindow(
                    vertices.constData(), std::size_t(vertices.size()),
                    std::size_t(patch.first_vertex), std::size_t(patch.vertex_count),
                    sizeof(MapGlobeSurfaceVertex)))
            {
                this->window_upload_pending = true;
                break;
            }
        }
    }
    if (this->window_upload_pending)
        uploadWindowGeometry();
    if (this->cap_upload_pending)
        uploadCapGeometry();
    if (this->surface_preparation.wireframeVisible() && this->wireframe_upload_pending)
        uploadWireframe();
    if (this->map_visible)
    {
        requestImagery();
        resolveImagery(imagery, &result, inputs.upload_budget);
        resolveHeatmap(inputs.heatmap_opacity, &result);
    }

    // --- Draw --------------------------------------------------------------
    // Canonical OpenGL clip space (z in [-w, w]); Direct3D, Vulkan and Metal
    // expect [0, w]: z' = (z + w) / 2.
    MapGlobeCameraBasis camera_basis;
    QMatrix4x4 view_projection = camera.viewProjectionMatrix(&camera_basis);
    const bgfx::Caps *caps = bgfx::getCaps();
    if (caps != nullptr && !caps->homogeneousDepth)
    {
        const QMatrix4x4 depth_correction(1.0f, 0.0f, 0.0f, 0.0f,
                                          0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 0.5f, 0.5f,
                                          0.0f, 0.0f, 0.0f, 1.0f);
        view_projection = depth_correction * view_projection;
    }
    // QMatrix4x4 stores column-major, as bgfx's mul(matrix, vector) expects.
    const float *matrix = view_projection.constData();
    const float heatmap_style[4] = {float(std::clamp(inputs.heatmap_opacity, 0.0, 1.0)), 0.0f, 0.0f, 0.0f};
    // The heatmap bake views run before this map's main view.
    this->heatmap_gpu.orderViews(view, this->heatmap_bake_first_view);
    const float background[4] = {
        float(inputs.background.redF()), float(inputs.background.greenF()),
        float(inputs.background.blueF()),
        float(std::clamp(inputs.background_opacity, 0.0, 1.0))};
    // Hillshading: the cartographic convention, light from the north-west
    // (azimuth 315 deg) 45 deg above each point's own horizon; the shaders
    // build the point's east/north/up frame from its position (relative to
    // the render origin) and scale the imagery by lambert / sin(45 deg), so
    // flat ground keeps its brightness and only slopes lighten or darken.
    const double light_azimuth_rad = qDegreesToRadians(GlobeHillshadeAzimuthDeg);
    const double light_altitude_rad = qDegreesToRadians(GlobeHillshadeAltitudeDeg);
    const float light[4] = {float(std::cos(light_altitude_rad) * std::sin(light_azimuth_rad)),
                            float(std::cos(light_altitude_rad) * std::cos(light_azimuth_rad)),
                            float(std::sin(light_altitude_rad)), 0.0f};
    const float shading[4] = {this->hillshade_visible ? float(GlobeHillshadeStrength) : 0.0f,
                              float(std::sin(light_altitude_rad)), 0.0f, 0.0f};
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef = camera.renderOriginEcef();
    const float origin[4] = {float(origin_ecef.x), float(origin_ecef.y), float(origin_ecef.z), 0.0f};
    MapBgfxGlobeTerrainGpu::Frame gpu_frame;
    std::copy_n(matrix, 16, gpu_frame.view_projection.begin());
    std::copy_n(background, 4, gpu_frame.background.begin());
    std::copy_n(origin, 4, gpu_frame.origin.begin());
    std::copy_n(light, 4, gpu_frame.light.begin());
    std::copy_n(shading, 4, gpu_frame.shading.begin());

    if (this->map_visible)
    {
        // The sky first (the view is sequential): the surface covers it.
        if (this->sky_gpu.ready())
            drawSky(view, camera_basis, camera.renderOriginEcef(), inputs.logical_viewport);
        // Caps and per-tile imagery now submit through the shared Render GPU.
        for (const MapGlobeSurfaceTile &tile : this->surface_preparation.capTiles())
        {
            if (tile.index_count <= 0)
                continue;
            this->terrain_gpu.drawCap(view, this->terrain_buffers.caps(),
                                      this->terrain_buffers.capIndices(),
                                      std::uint32_t(tile.first_index),
                                      std::uint32_t(tile.index_count), gpu_frame);
        }
        const QVector<MapGlobeSurfaceTile> &tiles = this->surface_preparation.windowTiles();
        if (bgfx::isValid(this->terrain_buffers.window()))
        {
            for (qsizetype index = 0; index < tiles.size(); ++index)
            {
                const MapGlobeSurfaceTile &tile = tiles.at(index);
                if (tile.index_count <= 0 || tile.vertex_count <= 0)
                    continue;
                const bgfx::IndexBufferHandle tile_indices = this->tile_index_pool.get(tile.grid_cell_count);
                if (!bgfx::isValid(tile_indices))
                    continue;
                const TileImagery &state = this->window_imagery.at(index);
                MapBgfxGlobeTerrainGpu::Heatmap heatmap;
                const MapBgfxGlobeTerrainGpu::Heatmap *overlay = nullptr;
                if (state.heatmap_page >= 0)
                {
                    heatmap = this->heatmap_gpu.composite(state.heatmap_page,
                                                          state.heatmap_uniform, heatmap_style);
                    if (bgfx::isValid(heatmap.program))
                        overlay = &heatmap;
                }
                if (this->terrain_gpu.drawTile(view, this->terrain_buffers.window(),
                                               std::uint32_t(tile.first_vertex),
                                               std::uint32_t(tile.vertex_count),
                                               tile_indices, state.texture,
                                               state.uniform, gpu_frame, overlay))
                    ++result.tiles_drawn;
            }
        }
    }
    // Wireframe is also supported when map imagery is hidden.
    const QVector<MapGlobeSurfaceWireframeVertex> &wireframe =
        this->surface_preparation.wireframeVertices();
    if (this->surface_preparation.wireframeVisible() && !wireframe.isEmpty())
        this->terrain_gpu.drawWireframe(view, this->terrain_buffers.wireframe(),
                                        std::uint32_t(wireframe.size()), gpu_frame);

    result.pending = result.pending || this->surface_preparation.hasPendingTerrainMeshes();
    return result;
}

// --- Heatmap (slice 3b) -----------------------------------------------------

bool MapBgfxGlobeSurface::allocateHeatmapCell(HeatmapCell *cell)
{
    // A free slot in an existing page, then a new page.
    for (int page = 0; page < HeatmapPageCount; ++page)
    {
        QVector<quint64> &owners = this->heatmap_slot_owners[page];
        if (!bgfx::isValid(this->heatmap_gpu.pageTexture(page)))
        {
            if (!this->heatmap_gpu.ensurePage(page))
            {
                qWarning() << "bgfx globe heatmap: atlas page" << page << "could not be created";
                return false;
            }
            owners.fill(0, kHeatmapCellsPerPage);
        }
        const qsizetype slot = owners.indexOf(quint64(0));
        if (slot >= 0)
        {
            cell->page = page;
            cell->slot = int(slot);
            return true;
        }
    }
    // All pages full: the least recently used cell no tile drew this frame.
    quint64 victim_key = 0;
    quint64 oldest_frame = this->heatmap_frame;
    for (QHash<quint64, HeatmapCell>::const_iterator it = this->heatmap_cells.cbegin();
         it != this->heatmap_cells.cend(); ++it)
    {
        if (it.value().page >= 0 && it.value().last_used_frame < oldest_frame)
        {
            oldest_frame = it.value().last_used_frame;
            victim_key = it.key();
        }
    }
    if (oldest_frame == this->heatmap_frame)
        return false;
    HeatmapCell &victim = this->heatmap_cells[victim_key];
    cell->page = victim.page;
    cell->slot = victim.slot;
    // Evicted: drawn again only after a new bake.
    victim.page = -1;
    victim.slot = -1;
    victim.revision = 0;
    return true;
}

void MapBgfxGlobeSurface::resolveHeatmap(double opacity, FrameResult *result)
{
    ++this->heatmap_frame;
    // Tiles seen long ago without a cell are forgotten now and then.
    if ((this->heatmap_frame & 255u) == 0)
    {
        for (QHash<quint64, HeatmapCell>::iterator it = this->heatmap_cells.begin();
             it != this->heatmap_cells.end();)
        {
            if (it.value().page < 0 && it.value().last_used_frame + 256u < this->heatmap_frame)
                it = this->heatmap_cells.erase(it);
            else
                ++it;
        }
    }
    for (TileImagery &state : this->window_imagery)
        state.heatmap_page = -1;
    if (!this->heatmap_gpu.ready() || this->heatmap_scene.isEmpty() || opacity <= 0.0)
        return;

    const quint64 revision = this->heatmap_scene.revision();
    const float solid_fraction = float(this->heatmap_scene.solidFraction());
    QVector<HeatmapBakeVertex> clears[HeatmapPageCount];
    QVector<HeatmapBakeVertex> stamps[HeatmapPageCount];
    QVector<quint64> baked_keys[HeatmapPageCount];
    int bakes = 0;
    const QVector<MapGlobeSurfaceTile> &tiles = this->surface_preparation.windowTiles();
    for (qsizetype index = 0; index < tiles.size(); ++index)
    {
        const MapGlobeSurfaceTile &tile = tiles.at(index);
        if (tile.is_cap)
            continue;
        const quint64 key = heatmapTileKey(tile.zoom, tile.virtual_x, tile.tile_y);
        HeatmapCell &cell = this->heatmap_cells[key];
        cell.last_used_frame = this->heatmap_frame;
        if (cell.revision != revision)
        {
            if (bakes >= kHeatmapBakesPerFrame)
            {
                // Shows its previous revision meanwhile (if any).
                result->pending = true;
            }
            else
            {
                MapGlobeHeatmapTile heatmap_tile;
                heatmap_tile.zoom = tile.zoom;
                heatmap_tile.virtual_x = tile.virtual_x;
                heatmap_tile.tile_y = tile.tile_y;
                const QVector<MapGlobeHeatmapStamp> tile_stamps =
                    this->heatmap_scene.stampsForTile(heatmap_tile, &cell.layout_cache);
                if (tile_stamps.isEmpty())
                {
                    // Nothing on this tile: no cell.
                    if (cell.page >= 0)
                        this->heatmap_slot_owners[cell.page][cell.slot] = 0;
                    cell.page = -1;
                    cell.slot = -1;
                    cell.revision = revision;
                }
                else if (cell.page >= 0 || allocateHeatmapCell(&cell))
                {
                    this->heatmap_slot_owners[cell.page][cell.slot] = key;
                    const float cell_x = float((cell.slot % kHeatmapCellsPerRow) * kHeatmapCellSize);
                    const float cell_y = float((cell.slot / kHeatmapCellsPerRow) * kHeatmapCellSize);
                    // The whole cell, cleared first (negative solid fraction).
                    for (const float (&corner)[2] : kStampCorners)
                    {
                        HeatmapBakeVertex vertex;
                        vertex.corner_x = corner[0];
                        vertex.corner_y = corner[1];
                        vertex.center_x = 0.5f * float(kHeatmapCellSize);
                        vertex.center_y = 0.5f * float(kHeatmapCellSize);
                        vertex.radius = 0.5f * float(kHeatmapCellSize);
                        vertex.solid_fraction = -1.0f;
                        vertex.cell_x = cell_x;
                        vertex.cell_y = cell_y;
                        clears[cell.page].append(vertex);
                    }
                    for (const MapGlobeHeatmapStamp &stamp : tile_stamps)
                    {
                        for (const float (&corner)[2] : kStampCorners)
                        {
                            HeatmapBakeVertex vertex;
                            vertex.corner_x = corner[0];
                            vertex.corner_y = corner[1];
                            vertex.center_x = float(stamp.center_x_pixels);
                            vertex.center_y = float(stamp.center_y_pixels);
                            vertex.radius = float(stamp.radius_pixels);
                            vertex.solid_fraction = solid_fraction;
                            vertex.red = float(stamp.color.redF());
                            vertex.green = float(stamp.color.greenF());
                            vertex.blue = float(stamp.color.blueF());
                            vertex.cell_x = cell_x;
                            vertex.cell_y = cell_y;
                            stamps[cell.page].append(vertex);
                        }
                    }
                    baked_keys[cell.page].append(key);
                    cell.revision = revision;
                    ++bakes;
                }
                else
                {
                    // Every cell is drawn this frame: try again next frame.
                    result->pending = true;
                }
            }
        }
        if (cell.page >= 0 && index < this->window_imagery.size())
        {
            TileImagery &state = this->window_imagery[index];
            state.heatmap_page = cell.page;
            // Texel centres of the cell's border: no neighbour bleeds in.
            const float cell_x = float((cell.slot % kHeatmapCellsPerRow) * kHeatmapCellSize);
            const float cell_y = float((cell.slot / kHeatmapCellsPerRow) * kHeatmapCellSize);
            state.heatmap_uniform[0] = (cell_x + 0.5f) / float(kHeatmapPageSize);
            state.heatmap_uniform[1] = (cell_y + 0.5f) / float(kHeatmapPageSize);
            state.heatmap_uniform[2] = float(kHeatmapCellSize - 1) / float(kHeatmapPageSize);
            state.heatmap_uniform[3] = float(kHeatmapCellSize - 1) / float(kHeatmapPageSize);
            ++result->heatmap_tiles;
        }
    }

    // Bake views (ordered before the map view in render()). Render owns
    // transient allocations, render targets and GPU submissions.
    for (int page = 0; page < HeatmapPageCount; ++page)
    {
        if (clears[page].isEmpty())
            continue;
        const bool baked = this->heatmap_gpu.bakePage(
            bgfx::ViewId(this->heatmap_bake_first_view + page), page,
            clears[page].constData(), std::uint32_t(clears[page].size()),
            stamps[page].constData(), std::uint32_t(stamps[page].size()));
        if (!baked)
        {
            // Roll back revision stamps: retry the page on a later frame.
            for (const quint64 key : baked_keys[page])
                this->heatmap_cells[key].revision = 0;
            result->pending = true;
            continue;
        }
        result->heatmap_bakes += baked_keys[page].size();
    }
}
