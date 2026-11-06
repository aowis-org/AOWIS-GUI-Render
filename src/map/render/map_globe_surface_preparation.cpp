#include "map/render/map_globe_surface_preparation.h"

#include "geo/geo_web_mercator.h"
#include "map/core/map_model.h"
#include "map/data/map_terrain_tile_source.h"
#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_picking.h"
#include "map/render/map_terrain_mesh_scheduler.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace
{
constexpr int GlobeImageryMaxZoom = MapModel::MaxZoom;
constexpr int GlobeTerrainReliefMinimumZoom =
    MapGlobeSurfaceScene::TerrainReliefMinimumZoom;
constexpr int GlobePolarCapSegments = 48;
// GUI-thread time a window rebuild may spend building new tiles' meshes in
// place (buildWindowTileVertices()). A camera move needs a few tiles, well
// below it; entering the globe or a render-origin rebase needs the whole
// window (~45 ms at full detail): beyond the budget new tiles start flat
// and get their mesh from the mesh thread instead of freezing a frame.
constexpr qint64 GlobeWindowMeshBudgetNs = 8000000;

int subdivisionsForZoom(int zoom)
{
    if (zoom <= 0)
        return 32;
    if (zoom <= 1)
        return 24;
    if (zoom <= 3)
        return 12;
    if (zoom <= 6)
        return 4;
    return 2;
}

// The wireframe's line list for triangles given as indices relative to
// base_vertex: each triangle's three edges.
template <typename Index>
void appendWireframeEdges(
    QVector<MapGlobeSurfaceWireframeVertex> *wireframe,
    const QVector<MapGlobeSurfaceVertex> &vertices, qsizetype base_vertex,
    const Index *indices, qsizetype index_count)
{
    for (qsizetype index = 0; index + 2 < index_count; index += 3)
    {
        const qsizetype a_index = base_vertex + qsizetype(indices[index]);
        const qsizetype b_index = base_vertex + qsizetype(indices[index + 1]);
        const qsizetype c_index = base_vertex + qsizetype(indices[index + 2]);
        if (a_index >= vertices.size() || b_index >= vertices.size() || c_index >= vertices.size())
            continue;

        const MapGlobeSurfaceVertex &a = vertices.at(a_index);
        const MapGlobeSurfaceVertex &b = vertices.at(b_index);
        const MapGlobeSurfaceVertex &c = vertices.at(c_index);
        const MapGlobeSurfaceWireframeVertex wa = {a.x, a.y, a.z};
        const MapGlobeSurfaceWireframeVertex wb = {b.x, b.y, b.z};
        const MapGlobeSurfaceWireframeVertex wc = {c.x, c.y, c.z};

        wireframe->append(wa);
        wireframe->append(wb);
        wireframe->append(wb);
        wireframe->append(wc);
        wireframe->append(wc);
        wireframe->append(wa);
    }
}

bool terrainDatumIsOrthometric(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

bool rayAabbIntersectionDistanceRange(
    const QVector3D &ray_origin, const QVector3D &ray_direction,
    const QVector3D &bounds_min, const QVector3D &bounds_max,
    double *entry_distance_m, double *exit_distance_m)
{
    if (entry_distance_m == nullptr || exit_distance_m == nullptr)
        return false;

    double entry = 0.0;
    double exit = std::numeric_limits<double>::infinity();

    for (int axis = 0; axis < 3; ++axis)
    {
        const double origin = double(ray_origin[axis]);
        const double direction = double(ray_direction[axis]);
        const double minimum = double(bounds_min[axis]);
        const double maximum = double(bounds_max[axis]);

        if (std::abs(direction) <= 1e-12)
        {
            if (origin < minimum || origin > maximum)
                return false;
            continue;
        }

        double near_distance = (minimum - origin) / direction;
        double far_distance = (maximum - origin) / direction;
        if (near_distance > far_distance)
            std::swap(near_distance, far_distance);

        entry = qMax(entry, near_distance);
        exit = qMin(exit, far_distance);
        if (exit < entry)
            return false;
    }

    if (!(exit > 0.0) || !std::isfinite(entry))
        return false;

    *entry_distance_m = entry;
    *exit_distance_m = exit;
    return true;
}
}

int mapGlobeSurfaceTileRequestPriority(
    int tile_x, int tile_y, int zoom,
    double center_lon_deg, double center_lat_deg)
{
    const double tile_lon_deg = GeoWebMercator::tileXToLon(
        double(tile_x) + 0.5, zoom);
    const double tile_lat_deg = GeoWebMercator::tileYToLat(
        double(tile_y) + 0.5, zoom);

    const double center_lat_rad = qDegreesToRadians(center_lat_deg);
    const double tile_lat_rad = qDegreesToRadians(tile_lat_deg);
    const double lon_delta_rad = qDegreesToRadians(
        GeoWebMercator::normalizeLongitude(tile_lon_deg - center_lon_deg));
    const double cosine_angle = qBound(
        -1.0,
        std::sin(center_lat_rad) * std::sin(tile_lat_rad)
            + std::cos(center_lat_rad) * std::cos(tile_lat_rad)
                * std::cos(lon_delta_rad),
        1.0);

    return int(std::lround((1.0 - cosine_angle) * 1000000.0));
}

int mapGlobeTerrainDetailReduction(const std::function<int()> &source)
{
    return source ? source() : MapGlobeTerrainDefaultDetailReduction;
}

MapGlobeTerrainLevels mapGlobeTerrainLevels(const MapTerrainTileSource *terrain_repository,
                                            int detail_reduction)
{
    MapGlobeTerrainLevels levels;
    levels.detail_reduction = qBound(
        0, detail_reduction, MapGlobeTerrainMaximumDetailReduction);

    const MapTerrainDatasetInfo *dataset =
        terrain_repository != nullptr ? terrain_repository->activeDataset() : nullptr;
    if (dataset == nullptr)
        return levels;

    // The provider's own range: nothing below what it generates, nothing
    // finer than its native detail (imagery past that subdivides the
    // maximum_zoom DEM tile).
    levels.dataset = dataset->dataset;
    levels.minimum_zoom = qMax(GlobeTerrainReliefMinimumZoom, dataset->minimum_zoom);
    levels.maximum_zoom = qMin(dataset->maximum_zoom, GlobeImageryMaxZoom);
    return levels;
}

bool mapGlobeTerrainDatumUsable(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Wgs84Ellipsoid
        || datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

namespace
{
// Orders tiles by mapGlobeSurfaceTileRequestPriority() (nearest the map
// centre first), then by tie_before. The priority is trigonometry: computed
// once per tile, not on every comparison of the sort (thousands per frame).
template <typename Tile>
void sortByRequestPriority(QVector<Tile *> *tiles, double center_lon_deg, double center_lat_deg,
                           const std::function<bool(const Tile *, const Tile *)> &tie_before)
{
    QVector<QPair<int, Tile *>> keyed;
    keyed.reserve(tiles->size());
    for (Tile *tile : *tiles)
    {
        keyed.append(qMakePair(mapGlobeSurfaceTileRequestPriority(
                                   tile->tile_x, tile->tile_y, tile->zoom, center_lon_deg, center_lat_deg),
                               tile));
    }
    std::sort(keyed.begin(), keyed.end(),
              [&tie_before](const QPair<int, Tile *> &first, const QPair<int, Tile *> &second)
              {
                  if (first.first != second.first)
                      return first.first < second.first;
                  return tie_before && tie_before(first.second, second.second);
              });
    for (qsizetype index = 0; index < keyed.size(); ++index)
        (*tiles)[index] = keyed.at(index).second;
}
}

MapGlobeSurfacePreparation::MapGlobeSurfacePreparation(MapModel *map_model)
    : map_model(map_model),
      terrain_mesh_scheduler(std::make_unique<MapTerrainMeshScheduler>())
{
}

MapGlobeSurfacePreparation::~MapGlobeSurfacePreparation() = default;

bool MapGlobeSurfacePreparation::setTerrainRepository(
    MapTerrainTileSource *terrain_repository)
{
    if (this->terrain_repository == terrain_repository)
        return false;

    this->terrain_repository = terrain_repository;
    this->reported_orthometric_datum_warning = false;
    this->reported_unusable_datum_warning = false;
    this->window_dirty = true;
    return true;
}

void MapGlobeSurfacePreparation::setTerrainDetailReductionSource(
    const std::function<int()> &source)
{
    this->terrain_detail_reduction_source = source;
}

MapTerrainTileSource *MapGlobeSurfacePreparation::terrainRepository() const
{
    return this->terrain_repository;
}

bool MapGlobeSurfacePreparation::requestTerrainForCurrentView(
    const QSize &viewport_size)
{
    if (this->map_model == nullptr || this->terrain_repository == nullptr
        || !viewport_size.isValid())
    {
        return false;
    }

    this->refreshTerrainLevels();
    const QVector<MapGlobeQuadtreeLeaf> desired_leaves =
        this->surface_scene.selectVisibleLeaves(
            *this->map_model, viewport_size, this->terrain_repository, this->terrain_levels);
    this->rebuildWindow(desired_leaves);
    this->requestMissingTerrainTiles();
    return true;
}

void MapGlobeSurfacePreparation::refreshImageryKeys()
{
    if (this->map_model == nullptr)
        return;
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.is_cap)
            continue;
        tile.imagery_key = this->map_model->tileCacheKey(tile.tile_x, tile.tile_y, tile.zoom);
    }
}

void MapGlobeSurfacePreparation::invalidateTerrainView()
{
    this->window_dirty = true;
    this->surface_scene.clearVisibilityHistory();
}

bool MapGlobeSurfacePreparation::refreshTerrainLevels()
{
    const MapGlobeTerrainLevels levels = mapGlobeTerrainLevels(
        this->terrain_repository,
        mapGlobeTerrainDetailReduction(this->terrain_detail_reduction_source));
    if (levels == this->terrain_levels)
        return false;

    // Another dataset, range or detail reduction: other DEM tiles and other
    // grids for every tile, so no geometry survives (the in-flight meshes
    // are dropped by their request ids).
    this->terrain_levels = levels;
    this->window_dirty = true;
    return true;
}

const MapGlobeTerrainLevels &MapGlobeSurfacePreparation::terrainLevels() const
{
    return this->terrain_levels;
}

bool MapGlobeSurfacePreparation::setRenderOriginEcef(
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef)
{
    if (this->render_origin_ecef.x == origin_ecef.x
        && this->render_origin_ecef.y == origin_ecef.y
        && this->render_origin_ecef.z == origin_ecef.z)
    {
        return false;
    }

    this->render_origin_ecef = origin_ecef;
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
        tile.terrain_mesh_request_id = 0;

    this->window_dirty = true;
    this->caps_built = false;
    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    return true;
}

void MapGlobeSurfacePreparation::notifyTerrainTileAvailable(const QString &key)
{
    if (key.isEmpty())
        return;

    // A neighbor DEM tile completes the edge normals of meshes built
    // without it (MapTerrainMeshResult::missing_neighbor_mask): those are
    // built again, drawn as they are until then.
    const MapTerrainTile *arrived =
        this->terrain_repository != nullptr ? this->terrain_repository->tile(key) : nullptr;
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key == key)
        {
            tile.terrain_mesh_request_id = 0;
            tile.terrain_mesh_applied = false;
            continue;
        }
        if (arrived == nullptr || tile.terrain_missing_neighbor_mask == 0
            || tile.terrain_zoom != arrived->address.zoom || !tile.terrain_mesh_applied)
        {
            continue;
        }
        const int shift = tile.zoom - tile.terrain_zoom;
        const qint64 tile_count = qint64(1) << tile.terrain_zoom;
        const qint64 terrain_x = qint64(quint32(tile.tile_x) >> shift);
        const qint64 terrain_y = qint64(quint32(tile.tile_y) >> shift);
        const qint64 arrived_x = qint64(arrived->address.x);
        const qint64 arrived_y = qint64(arrived->address.y);
        int neighbor = -1;
        if (arrived_y == terrain_y && arrived_x == (terrain_x + tile_count - 1) % tile_count)
            neighbor = MapTerrainMeshNeighborWest;
        else if (arrived_y == terrain_y && arrived_x == (terrain_x + 1) % tile_count)
            neighbor = MapTerrainMeshNeighborEast;
        else if (arrived_x == terrain_x && arrived_y == terrain_y - 1)
            neighbor = MapTerrainMeshNeighborNorth;
        else if (arrived_x == terrain_x && arrived_y == terrain_y + 1)
            neighbor = MapTerrainMeshNeighborSouth;
        if (neighbor >= 0 && (tile.terrain_missing_neighbor_mask & (1 << neighbor)) != 0)
        {
            tile.terrain_mesh_request_id = 0;
            tile.terrain_mesh_applied = false;
        }
    }
}

void MapGlobeSurfacePreparation::invalidateTerrain()
{
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;
        tile.terrain_mesh_request_id = 0;
        tile.terrain_mesh_applied = false;
        // Stand-in meshes are rebuilt as well (the next
        // scheduleAncestorTerrainStandIns()).
        tile.terrain_stand_in_zoom = -1;
    }
}

bool MapGlobeSurfacePreparation::setWireframeVisible(bool visible)
{
    if (this->wireframe_visible == visible)
        return false;

    this->wireframe_visible = visible;
    if (visible)
        this->rebuildWireframeVertices();
    return true;
}

bool MapGlobeSurfacePreparation::wireframeVisible() const
{
    return this->wireframe_visible;
}

MapGlobeSurfaceVertex MapGlobeSurfacePreparation::makeTileVertex(
    double lon_deg, double lat_deg, float u, float v) const
{
    const GeoWgs84Ellipsoid::EcefPositionD position =
        GeoWgs84Ellipsoid::geodeticToEcefD(lon_deg, lat_deg, 0.0);
    MapGlobeSurfaceVertex vertex;
    vertex.x = float(position.x - this->render_origin_ecef.x);
    vertex.y = float(position.y - this->render_origin_ecef.y);
    vertex.z = float(position.z - this->render_origin_ecef.z);
    vertex.u = u;
    vertex.v = v;
    // The ellipsoid's normal (flat tiles, polar caps).
    const double lat_rad = qDegreesToRadians(lat_deg);
    const double lon_rad = qDegreesToRadians(lon_deg);
    vertex.normal = mapGlobePackNormal(std::cos(lat_rad) * std::cos(lon_rad),
                                       std::cos(lat_rad) * std::sin(lon_rad), std::sin(lat_rad));
    return vertex;
}

void MapGlobeSurfacePreparation::buildPolarCap(bool north)
{
    const double ring_lat = north
        ? GeoWebMercator::MaximumLatitude
        : -GeoWebMercator::MaximumLatitude;
    const double pole_lat = north ? 90.0 : -90.0;

    MapGlobeSurfaceTile cap;
    cap.surface_tile_index = this->cap_tiles.size();
    cap.is_cap = true;
    cap.first_vertex = this->cap_vertices.size();
    cap.first_index = this->cap_indices.size();

    const MapGlobeSurfaceVertex pole_vertex =
        this->makeTileVertex(0.0, pole_lat, 0.5f, 0.5f);
    this->cap_vertices.append(pole_vertex);
    for (int segment = 0; segment <= GlobePolarCapSegments; ++segment)
    {
        const double longitude_deg = -180.0
            + 360.0 * double(segment) / double(GlobePolarCapSegments);
        this->cap_vertices.append(
            this->makeTileVertex(longitude_deg, ring_lat, 0.5f, 0.5f));
    }

    const quint32 pole_index = quint32(cap.first_vertex);
    for (int segment = 0; segment < GlobePolarCapSegments; ++segment)
    {
        const quint32 ring0 = pole_index + 1 + quint32(segment);
        const quint32 ring1 = ring0 + 1;
        this->cap_indices.append(pole_index);
        if (north)
        {
            this->cap_indices.append(ring0);
            this->cap_indices.append(ring1);
        }
        else
        {
            this->cap_indices.append(ring1);
            this->cap_indices.append(ring0);
        }
    }

    cap.vertex_count = this->cap_vertices.size() - cap.first_vertex;
    cap.index_count = this->cap_indices.size() - cap.first_index;
    this->cap_tiles.append(cap);
}

bool MapGlobeSurfacePreparation::ensureCapsBuilt()
{
    if (this->caps_built)
        return false;

    this->cap_vertices.clear();
    this->cap_indices.clear();
    this->cap_tiles.clear();
    this->buildPolarCap(true);
    this->buildPolarCap(false);
    this->caps_built = true;
    if (this->wireframe_visible)
        this->rebuildWireframeVertices();
    return true;
}

void MapGlobeSurfacePreparation::updateTerrainRayBounds(
    MapGlobeSurfaceTile *tile)
{
    if (tile == nullptr)
        return;

    tile->terrain_ray_bounds_valid = false;
    tile->terrain_ray_row_bounds.clear();
    if (!tile->terrain_mesh_has_relief
        || tile->first_vertex < 0 || tile->vertex_count <= 0)
    {
        return;
    }

    // Bounds of the grid only: the skirt hangs below it and is no surface
    // a ray may hit (mapGlobeAppendTileSkirt()).
    const int cell_count = tile->grid_cell_count;
    const qsizetype grid_width = qsizetype(cell_count) + 1;
    if (cell_count < 1 || tile->vertex_count != mapGlobeTileVertexCount(cell_count))
        return;
    const qsizetype vertex_begin = qsizetype(tile->first_vertex);
    const qsizetype vertex_end = vertex_begin + mapGlobeTileGridVertexCount(cell_count);
    if (vertex_begin < 0 || vertex_end > this->window_vertices.size())
        return;

    const MapGlobeSurfaceVertex &first_vertex =
        this->window_vertices.at(vertex_begin);
    QVector3D bounds_min(first_vertex.x, first_vertex.y, first_vertex.z);
    QVector3D bounds_max = bounds_min;

    for (qsizetype index = vertex_begin + 1; index < vertex_end; ++index)
    {
        const MapGlobeSurfaceVertex &vertex = this->window_vertices.at(index);
        bounds_min.setX(qMin(bounds_min.x(), vertex.x));
        bounds_min.setY(qMin(bounds_min.y(), vertex.y));
        bounds_min.setZ(qMin(bounds_min.z(), vertex.z));
        bounds_max.setX(qMax(bounds_max.x(), vertex.x));
        bounds_max.setY(qMax(bounds_max.y(), vertex.y));
        bounds_max.setZ(qMax(bounds_max.z(), vertex.z));
    }

    tile->terrain_ray_bounds_min = bounds_min;
    tile->terrain_ray_bounds_max = bounds_max;
    tile->terrain_ray_bounds_valid = true;

    tile->terrain_ray_row_bounds.reserve(cell_count);
    for (int row = 0; row < cell_count; ++row)
    {
        const qsizetype row_begin = vertex_begin
            + qsizetype(row) * grid_width;
        const qsizetype row_end = row_begin + grid_width * 2;
        if (row_begin < vertex_begin || row_end > vertex_end)
        {
            tile->terrain_ray_row_bounds.clear();
            return;
        }

        const MapGlobeSurfaceVertex &row_first =
            this->window_vertices.at(row_begin);
        QVector3D row_min(row_first.x, row_first.y, row_first.z);
        QVector3D row_max = row_min;
        for (qsizetype index = row_begin + 1; index < row_end; ++index)
        {
            const MapGlobeSurfaceVertex &vertex =
                this->window_vertices.at(index);
            row_min.setX(qMin(row_min.x(), vertex.x));
            row_min.setY(qMin(row_min.y(), vertex.y));
            row_min.setZ(qMin(row_min.z(), vertex.z));
            row_max.setX(qMax(row_max.x(), vertex.x));
            row_max.setY(qMax(row_max.y(), vertex.y));
            row_max.setZ(qMax(row_max.z(), vertex.z));
        }

        MapGlobeSurfaceTerrainRayRowBounds row_bounds;
        row_bounds.minimum = row_min;
        row_bounds.maximum = row_max;
        tile->terrain_ray_row_bounds.append(row_bounds);
    }
}

int MapGlobeSurfacePreparation::allocateWindowSlot(int vertex_count)
{
    // An exact-size free slot first (tiles of one cell count share a size).
    const QHash<int, QVector<int>>::iterator free_slots = this->window_free_slots.find(vertex_count);
    if (free_slots != this->window_free_slots.end() && !free_slots->isEmpty())
    {
        const int first_vertex = free_slots->takeLast();
        this->window_free_vertex_count -= vertex_count;
        return first_vertex;
    }

    const qsizetype first_vertex = this->window_pool_end;
    this->window_pool_end += vertex_count;
    if (this->window_pool_end > this->window_vertices.size())
    {
        // bgfx drops a dynamic buffer's content when it grows, so a grown
        // pool is uploaded whole; growing by half keeps that rare.
        this->window_vertices.resize(qMax(this->window_pool_end, this->window_vertices.size() * 3 / 2));
        this->window_full_upload = true;
    }
    return int(first_vertex);
}

void MapGlobeSurfacePreparation::releaseWindowSlot(int first_vertex, int vertex_count)
{
    if (first_vertex < 0 || vertex_count <= 0)
        return;
    this->window_free_slots[vertex_count].append(first_vertex);
    this->window_free_vertex_count += vertex_count;
}

void MapGlobeSurfacePreparation::buildWindowTileVertices(MapGlobeSurfaceTile *tile, int subdivisions,
                                                         bool build_mesh)
{
    const int vertex_count = mapGlobeTileVertexCount(subdivisions);
    MapGlobeSurfaceVertex *target = this->window_vertices.data() + tile->first_vertex;

    // The relief right away, built here (a 64-cell mesh takes ~0.1 ms): from
    // the tile's own DEM when it is cached, else resampled from the nearest
    // cached coarser DEM (a stand-in, replaced when the own DEM arrives).
    // Drawn flat for a frame or two until the mesh thread delivered, new
    // tiles on the ellipsoid flickered between the relief around them
    // whenever the camera moved.
    const MapTerrainTile *terrain_tile = nullptr;
    bool stand_in = false;
    if (build_mesh && !tile->terrain_key.isEmpty() && this->terrain_repository != nullptr)
    {
        terrain_tile = this->terrain_repository->tile(tile->terrain_key);
        if (terrain_tile == nullptr || terrain_tile->elevations_m.size() != MapTerrainTileSampleCount
            || !mapGlobeTerrainDatumUsable(terrain_tile->vertical_datum))
        {
            terrain_tile = this->cachedAncestorTerrainTile(*tile, -1);
            stand_in = terrain_tile != nullptr;
        }
    }

    if (terrain_tile != nullptr)
    {
        if (terrainDatumIsOrthometric(terrain_tile->vertical_datum)
            && !this->reported_orthometric_datum_warning)
        {
            qWarning().noquote()
                << QStringLiteral(
                       "Globe terrain tiles use an orthometric EGM vertical datum; "
                       "using it directly as local ellipsoid-normal displacement until "
                       "the terrain service exposes WGS84-ellipsoid tile heights.");
            this->reported_orthometric_datum_warning = true;
        }

        MapTerrainMeshRequest request = terrainMeshRequest(*tile, *terrain_tile);
        if (stand_in)
            request.stand_in_zoom = terrain_tile->address.zoom;
        const MapTerrainMeshResult result = buildTerrainMeshResult(request);
        if (result.vertices.size() == vertex_count)
        {
            for (const MapTerrainMeshVertex &vertex : result.vertices)
                *target++ = MapGlobeSurfaceVertex{vertex.x, vertex.y, vertex.z, vertex.u, vertex.v, vertex.normal};
            // A stand-in is not the tile's own mesh: that one is still
            // scheduled once its DEM arrives (scheduleReadyTerrainMeshes()).
            tile->terrain_mesh_applied = !stand_in;
            tile->terrain_missing_neighbor_mask = stand_in ? 0 : result.missing_neighbor_mask;
            tile->terrain_mesh_has_relief = true;
            tile->terrain_stand_in_zoom = stand_in ? terrain_tile->address.zoom : -1;
            return;
        }
    }

    // No DEM at all yet (or over the budget): the ellipsoid, from the mesh
    // builder without heights (its per-row/column trigonometry; the
    // per-vertex geodetic conversion below took ~45 ms for a full-detail
    // window on entering the globe).
    if (!tile->terrain_key.isEmpty())
    {
        const MapTerrainMeshResult result = buildTerrainMeshResult(terrainMeshRequest(*tile, MapTerrainTile()));
        if (result.vertices.size() == vertex_count)
        {
            for (const MapTerrainMeshVertex &vertex : result.vertices)
                *target++ = MapGlobeSurfaceVertex{vertex.x, vertex.y, vertex.z, vertex.u, vertex.v, vertex.normal};
            return;
        }
    }

    // Tiles without relief at their zoom: the ellipsoid, few vertices.
    QVector<MapGlobeSurfaceVertex> flat;
    flat.reserve(vertex_count);
    for (int row = 0; row <= subdivisions; ++row)
    {
        const double v = double(row) / double(subdivisions);
        const double latitude_deg = GeoWebMercator::tileYToLat(double(tile->tile_y) + v, tile->zoom);
        for (int column = 0; column <= subdivisions; ++column)
        {
            const double u = double(column) / double(subdivisions);
            const double longitude_deg = GeoWebMercator::tileXToLon(double(tile->virtual_x) + u, tile->zoom);
            flat.append(this->makeTileVertex(longitude_deg, latitude_deg, float(u), float(v)));
        }
    }
    mapGlobeAppendTileSkirt(
        &flat, 0, subdivisions,
        mapGlobeTileSkirtDepthM(tile->zoom, tile->tile_y, subdivisions,
                                this->map_model != nullptr ? this->map_model->view3dVerticalExaggeration() : 1.0,
                                0.0),
        this->render_origin_ecef.x, this->render_origin_ecef.y, this->render_origin_ecef.z);
    std::copy(flat.constBegin(), flat.constEnd(), target);
}

void MapGlobeSurfacePreparation::rebuildWindow(
    const QVector<MapGlobeQuadtreeLeaf> &leaves)
{
    QVector<MapGlobeSurfaceTile> previous_tiles = std::move(this->window_tiles);
    QHash<quint64, qsizetype> previous_tiles_by_position;
    if (!this->window_dirty)
    {
        previous_tiles_by_position.reserve(previous_tiles.size());
        for (qsizetype index = 0; index < previous_tiles.size(); ++index)
        {
            const MapGlobeSurfaceTile &tile = previous_tiles.at(index);
            previous_tiles_by_position.insert(
                MapGlobeSurfaceScene::positionKey(tile.zoom, tile.tile_x, tile.tile_y), index);
        }
    }

    QVector<MapGlobeSurfaceTile> next_tiles;
    next_tiles.reserve(leaves.size());
    QSet<quint64> seen_positions;
    seen_positions.reserve(leaves.size());

    for (const MapGlobeQuadtreeLeaf &leaf : leaves)
    {
        const quint64 position_key = MapGlobeSurfaceScene::positionKey(
            leaf.zoom, leaf.tile_x, leaf.tile_y);
        if (seen_positions.contains(position_key))
            continue;
        seen_positions.insert(position_key);

        const bool terrain_enabled =
            this->terrain_repository != nullptr
            && this->terrain_levels.reliefAtImageryZoom(leaf.zoom);
        const int terrain_zoom = terrain_enabled
            ? this->terrain_levels.terrainZoomForImageryZoom(leaf.zoom)
            : -1;

        MapGlobeSurfaceTile tile;
        tile.virtual_x = leaf.tile_x;
        tile.tile_x = leaf.tile_x;
        tile.tile_y = leaf.tile_y;
        tile.zoom = leaf.zoom;
        if (this->map_model != nullptr)
        {
            tile.imagery_key = this->map_model->tileCacheKey(
                leaf.tile_x, leaf.tile_y, leaf.zoom);
        }

        if (terrain_enabled)
        {
            const int zoom_delta = leaf.zoom - terrain_zoom;
            MapTerrainTileAddress terrain_address;
            terrain_address.zoom = terrain_zoom;
            terrain_address.x = quint32(leaf.tile_x) >> zoom_delta;
            terrain_address.y = quint32(leaf.tile_y) >> zoom_delta;
            tile.terrain_zoom = terrain_zoom;
            tile.terrain_key = mapTerrainTileKey(
                this->terrain_levels.dataset, terrain_address);
            // The DEM's full resolution: the level of detail is the DEM zoom.
            tile.terrain_cell_count = mapGlobeTerrainCellCount(leaf.zoom, terrain_zoom);
        }

        const int subdivisions = terrain_enabled
            ? qMax(1, tile.terrain_cell_count)
            : subdivisionsForZoom(tile.zoom);
        tile.grid_cell_count = subdivisions;
        tile.vertex_count = mapGlobeTileVertexCount(subdivisions);
        // The shared topology for its cell count (mapGlobeTileIndices()).
        tile.first_index = 0;
        tile.index_count = mapGlobeTileIndexCount(subdivisions);
        tile.first_vertex = -1;
        next_tiles.append(tile);
    }
    this->window_position_keys = std::move(seen_positions);

    // Which tiles stay: same position and the same geometry inputs. A kept
    // tile keeps everything, its vertex range and mesh state included.
    QVector<qsizetype> kept_from(next_tiles.size(), -1);
    QVector<bool> previous_kept(previous_tiles.size(), false);
    qsizetype total_vertex_count = 0;
    for (qsizetype index = 0; index < next_tiles.size(); ++index)
    {
        const MapGlobeSurfaceTile &tile = next_tiles.at(index);
        total_vertex_count += tile.vertex_count;
        const QHash<quint64, qsizetype>::const_iterator previous_iterator =
            previous_tiles_by_position.constFind(
                MapGlobeSurfaceScene::positionKey(tile.zoom, tile.tile_x, tile.tile_y));
        if (previous_iterator == previous_tiles_by_position.cend())
            continue;
        const MapGlobeSurfaceTile &previous_tile = previous_tiles.at(previous_iterator.value());
        if (previous_tile.zoom == tile.zoom
            && previous_tile.imagery_key == tile.imagery_key
            && previous_tile.terrain_key == tile.terrain_key
            && previous_tile.terrain_cell_count == tile.terrain_cell_count
            && previous_tile.grid_cell_count == tile.grid_cell_count
            && previous_tile.vertex_count == tile.vertex_count
            && previous_tile.first_vertex >= 0
            && previous_tile.first_vertex + previous_tile.vertex_count <= this->window_vertices.size())
        {
            kept_from[index] = previous_iterator.value();
            previous_kept[previous_iterator.value()] = true;
        }
    }

    // The vertex pool: a kept tile stays where it is, a dropped tile's range
    // becomes free, a new tile takes a free range of its size or the end. A
    // camera move then writes and uploads only the new tiles (it re-uploaded
    // the whole window, several MB, on nearly every frame). Rebuilt from
    // scratch, kept tiles copied together, when the window is dirty or when
    // free ranges outweigh the used ones (another cell count mix).
    const bool compact = this->window_dirty
        || this->window_free_vertex_count > qMax<qsizetype>(this->window_pool_end / 2, 262144);
    if (compact)
    {
        QVector<MapGlobeSurfaceVertex> previous_vertices = std::move(this->window_vertices);
        this->window_vertices = QVector<MapGlobeSurfaceVertex>();
        this->window_vertices.resize(total_vertex_count + total_vertex_count / 4);
        this->window_pool_end = 0;
        this->window_free_slots.clear();
        this->window_free_vertex_count = 0;
        this->window_full_upload = true;
        for (qsizetype index = 0; index < next_tiles.size(); ++index)
        {
            if (kept_from.at(index) < 0)
                continue;
            const MapGlobeSurfaceTile &previous_tile = previous_tiles.at(kept_from.at(index));
            MapGlobeSurfaceTile &tile = next_tiles[index];
            tile = previous_tile;
            tile.first_vertex = allocateWindowSlot(tile.vertex_count);
            const MapGlobeSurfaceVertex *source = previous_vertices.constData() + previous_tile.first_vertex;
            std::copy(source, source + tile.vertex_count, this->window_vertices.data() + tile.first_vertex);
            // Ray bounds are positions relative to the render origin, not to
            // the pool: unchanged.
        }
    }
    else
    {
        for (qsizetype index = 0; index < previous_tiles.size(); ++index)
        {
            if (!previous_kept.at(index))
                releaseWindowSlot(previous_tiles.at(index).first_vertex, previous_tiles.at(index).vertex_count);
        }
        for (qsizetype index = 0; index < next_tiles.size(); ++index)
        {
            if (kept_from.at(index) >= 0)
                next_tiles[index] = previous_tiles.at(kept_from.at(index));
        }
    }

    // New tiles: a range each, the mesh built right away (within the budget;
    // leaves come nearest first).
    QElapsedTimer mesh_budget;
    mesh_budget.start();
    for (qsizetype index = 0; index < next_tiles.size(); ++index)
    {
        if (kept_from.at(index) >= 0)
            continue;
        MapGlobeSurfaceTile &tile = next_tiles[index];
        tile.first_vertex = allocateWindowSlot(tile.vertex_count);
        this->buildWindowTileVertices(&tile, tile.grid_cell_count,
                                      mesh_budget.nsecsElapsed() < GlobeWindowMeshBudgetNs);
        this->updateTerrainRayBounds(&tile);
        if (!this->window_full_upload)
        {
            this->window_vertex_patches.append(
                MapGlobeSurfaceVertexPatch{qsizetype(tile.first_vertex), qsizetype(tile.vertex_count)});
        }
    }

    this->window_tiles.clear();
    this->window_tiles.reserve(next_tiles.size());
    this->window_tile_indices_by_position.clear();
    this->window_tile_indices_by_position.reserve(next_tiles.size());
    for (MapGlobeSurfaceTile &tile : next_tiles)
    {
        tile.surface_tile_index = this->window_tiles.size();
        this->window_tiles.append(tile);
        this->window_tile_indices_by_position.insert(
            MapGlobeSurfaceScene::positionKey(tile.zoom, tile.tile_x, tile.tile_y),
            this->window_tiles.size() - 1);
    }
    if (this->window_full_upload)
        this->window_vertex_patches.clear();

    this->window_dirty = false;

    if (this->wireframe_visible)
        this->rebuildWireframeVertices();
}

void MapGlobeSurfacePreparation::takeWindowVertexUploads(
    QVector<MapGlobeSurfaceVertexPatch> *patches, bool *full_upload)
{
    if (full_upload != nullptr)
        *full_upload = this->window_full_upload;
    if (patches != nullptr)
    {
        patches->clear();
        if (!this->window_full_upload)
            *patches = std::move(this->window_vertex_patches);
    }
    this->window_vertex_patches.clear();
    this->window_full_upload = false;
}

bool MapGlobeSurfacePreparation::prepareVisibleWindow(
    const QSize &viewport_size)
{
    if (this->map_model == nullptr || !viewport_size.isValid())
        return false;

    // A dataset catalog arriving or a changed detail setting rebuilds the
    // whole window. The meshes depend on nothing else: no camera-dependent
    // mesh density, so a camera move only rebuilds when the leaves change.
    this->refreshTerrainLevels();
    const QVector<MapGlobeQuadtreeLeaf> desired_leaves =
        this->surface_scene.selectVisibleLeaves(
            *this->map_model, viewport_size, this->terrain_repository, this->terrain_levels);

    bool leaves_match_window = !this->window_dirty
        && desired_leaves.size() == this->window_tiles.size()
        && this->window_position_keys.size() == this->window_tiles.size();
    if (leaves_match_window)
    {
        for (const MapGlobeQuadtreeLeaf &leaf : desired_leaves)
        {
            if (!this->window_position_keys.contains(
                    MapGlobeSurfaceScene::positionKey(
                        leaf.zoom, leaf.tile_x, leaf.tile_y)))
            {
                leaves_match_window = false;
                break;
            }
        }
    }

    if (leaves_match_window)
        return false;

    this->rebuildWindow(desired_leaves);
    return true;
}

void MapGlobeSurfacePreparation::requestMissingTerrainTiles()
{
    if (this->terrain_repository == nullptr || this->window_tiles.isEmpty()
        || this->map_model == nullptr)
    {
        return;
    }

    QVector<const MapGlobeSurfaceTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_zoom < 0 || tile.terrain_key.isEmpty())
            continue;
        candidates.append(&tile);
    }

    sortByRequestPriority<const MapGlobeSurfaceTile>(
        &candidates, this->map_model->centerLon(), this->map_model->centerLat(),
        [](const MapGlobeSurfaceTile *first, const MapGlobeSurfaceTile *second)
        {
            if (first->terrain_zoom != second->terrain_zoom)
                return first->terrain_zoom > second->terrain_zoom;
            if (first->tile_y != second->tile_y)
                return first->tile_y < second->tile_y;
            return first->tile_x < second->tile_x;
        });

    QSet<QString> requested_keys;
    requested_keys.reserve(candidates.size());
    for (const MapGlobeSurfaceTile *tile : candidates)
    {
        if (tile == nullptr
            || requested_keys.contains(tile->terrain_key)
            || this->terrain_repository->tile(tile->terrain_key) != nullptr)
        {
            continue;
        }
        requested_keys.insert(tile->terrain_key);

        const int zoom_delta = tile->zoom - tile->terrain_zoom;
        const quint32 terrain_x = quint32(tile->tile_x) >> zoom_delta;
        const quint32 terrain_y = quint32(tile->tile_y) >> zoom_delta;
        this->terrain_repository->requestTile(
            this->terrain_levels.dataset,
            tile->terrain_zoom, terrain_x, terrain_y);
    }
}

void MapGlobeSurfacePreparation::scheduleReadyTerrainMeshes()
{
    if (this->terrain_repository == nullptr
        || this->terrain_mesh_scheduler == nullptr
        || this->map_model == nullptr)
    {
        return;
    }

    QVector<MapGlobeSurfaceTile *> candidates;
    candidates.reserve(this->window_tiles.size());
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty()
            || tile.terrain_mesh_applied
            || tile.terrain_mesh_request_id != 0)
        {
            continue;
        }

        if (this->terrain_repository->tile(tile.terrain_key) == nullptr)
            continue;
        candidates.append(&tile);
    }

    sortByRequestPriority<MapGlobeSurfaceTile>(
        &candidates, this->map_model->centerLon(), this->map_model->centerLat(), nullptr);

    for (MapGlobeSurfaceTile *tile : candidates)
    {
        if (tile == nullptr)
            continue;

        const MapTerrainTile *terrain_tile =
            this->terrain_repository->tile(tile->terrain_key);
        if (terrain_tile == nullptr)
            continue;

        if (!mapGlobeTerrainDatumUsable(terrain_tile->vertical_datum))
        {
            if (!this->reported_unusable_datum_warning)
            {
                qWarning().noquote()
                    << QStringLiteral(
                           "Globe terrain is ignoring terrain tiles with an unknown/local "
                           "vertical datum because they cannot be interpreted as global height.");
                this->reported_unusable_datum_warning = true;
            }
            tile->terrain_mesh_applied = true;
            continue;
        }

        if (terrainDatumIsOrthometric(terrain_tile->vertical_datum)
            && !this->reported_orthometric_datum_warning)
        {
            qWarning().noquote()
                << QStringLiteral(
                       "Globe terrain tiles use an orthometric EGM vertical datum; "
                       "using it directly as local ellipsoid-normal displacement until "
                       "the terrain service exposes WGS84-ellipsoid tile heights.");
            this->reported_orthometric_datum_warning = true;
        }

        MapTerrainMeshRequest request = terrainMeshRequest(*tile, *terrain_tile);
        request.request_id = this->next_terrain_mesh_request_id++;
        tile->terrain_mesh_request_id = request.request_id;
        this->terrain_mesh_scheduler->submit(request);
    }

    // Tiles whose own DEM is not there yet: stand-ins after the real meshes.
    scheduleAncestorTerrainStandIns();
}

MapTerrainMeshRequest MapGlobeSurfacePreparation::terrainMeshRequest(
    const MapGlobeSurfaceTile &tile, const MapTerrainTile &terrain_tile) const
{
    MapTerrainMeshRequest request;
    request.terrain_key = tile.terrain_key;
    request.terrain_tile = terrain_tile;
    request.terrain_available =
        terrain_tile.elevations_m.size() == MapTerrainTileSampleCount;
    request.virtual_x = tile.virtual_x;
    request.tile_x = tile.tile_x;
    request.y = tile.tile_y;
    request.imagery_zoom = tile.zoom;
    request.terrain_zoom = tile.terrain_zoom;
    if (this->map_model != nullptr)
        request.globe_vertical_exaggeration = this->map_model->view3dVerticalExaggeration();
    request.globe_render_origin_x = this->render_origin_ecef.x;
    request.globe_render_origin_y = this->render_origin_ecef.y;
    request.globe_render_origin_z = this->render_origin_ecef.z;

    // The neighbors of the tile's own DEM tile, for seamless edge normals
    // (implicitly shared copies). A stand-in carries none: its own DEM's
    // mesh replaces it anyway.
    if (this->terrain_repository != nullptr && request.terrain_available
        && terrain_tile.address.zoom == tile.terrain_zoom)
    {
        const int zoom = terrain_tile.address.zoom;
        const qint64 tile_count = qint64(1) << zoom;
        const qint64 x = qint64(terrain_tile.address.x);
        const qint64 y = qint64(terrain_tile.address.y);
        const qint64 neighbor_x[MapTerrainMeshNeighborCount] = {
            (x + tile_count - 1) % tile_count, (x + 1) % tile_count, x, x};
        const qint64 neighbor_y[MapTerrainMeshNeighborCount] = {y, y, y - 1, y + 1};
        for (int neighbor = 0; neighbor < MapTerrainMeshNeighborCount; ++neighbor)
        {
            if (neighbor_y[neighbor] < 0 || neighbor_y[neighbor] >= tile_count)
                continue;
            const MapTerrainTile *neighbor_tile = this->terrain_repository->tile(
                this->terrain_levels.dataset, zoom, quint32(neighbor_x[neighbor]), quint32(neighbor_y[neighbor]));
            if (neighbor_tile != nullptr && neighbor_tile->elevations_m.size() == MapTerrainTileSampleCount
                && neighbor_tile->vertical_datum == terrain_tile.vertical_datum)
            {
                request.terrain_neighbors[neighbor] = *neighbor_tile;
            }
        }
    }
    return request;
}

const MapTerrainTile *MapGlobeSurfacePreparation::cachedAncestorTerrainTile(
    const MapGlobeSurfaceTile &tile, int coarsest_excluded_zoom) const
{
    if (this->terrain_repository == nullptr || tile.terrain_zoom < 0 || tile.zoom < tile.terrain_zoom)
        return nullptr;
    const quint32 terrain_x = quint32(tile.tile_x) >> (tile.zoom - tile.terrain_zoom);
    const quint32 terrain_y = quint32(tile.tile_y) >> (tile.zoom - tile.terrain_zoom);
    for (int ancestor_zoom = tile.terrain_zoom - 1;
         ancestor_zoom >= this->terrain_levels.minimum_zoom && ancestor_zoom > coarsest_excluded_zoom;
         --ancestor_zoom)
    {
        const int shift = tile.terrain_zoom - ancestor_zoom;
        const MapTerrainTile *candidate = this->terrain_repository->tile(
            this->terrain_levels.dataset, ancestor_zoom, terrain_x >> shift, terrain_y >> shift);
        if (candidate != nullptr && candidate->elevations_m.size() == MapTerrainTileSampleCount
            && mapGlobeTerrainDatumUsable(candidate->vertical_datum))
        {
            return candidate;
        }
    }
    return nullptr;
}

void MapGlobeSurfacePreparation::scheduleAncestorTerrainStandIns()
{
    // Candidates: meshes still waiting for their own DEM, nothing in flight.
    QVector<MapGlobeSurfaceTile *> candidates;
    for (MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (!tile.terrain_key.isEmpty() && !tile.terrain_mesh_applied
            && tile.terrain_mesh_request_id == 0 && tile.vertex_count > 0
            && tile.terrain_zoom - 1 > tile.terrain_stand_in_zoom
            && this->terrain_repository->tile(tile.terrain_key) == nullptr)
        {
            candidates.append(&tile);
        }
    }
    if (candidates.isEmpty())
        return;
    sortByRequestPriority<MapGlobeSurfaceTile>(
        &candidates, this->map_model->centerLon(), this->map_model->centerLat(), nullptr);

    // Leaves share ancestors: each repository lookup (a string key) once.
    QHash<quint64, const MapTerrainTile *> ancestors;
    for (MapGlobeSurfaceTile *tile : candidates)
    {
        // The finest cached ancestor, if finer than the current stand-in.
        MapTerrainTileAddress address;
        address.zoom = tile->terrain_zoom;
        address.x = quint32(tile->tile_x) >> (tile->zoom - tile->terrain_zoom);
        address.y = quint32(tile->tile_y) >> (tile->zoom - tile->terrain_zoom);
        const MapTerrainTile *ancestor = nullptr;
        for (int ancestor_zoom = tile->terrain_zoom - 1;
             ancestor_zoom >= this->terrain_levels.minimum_zoom
             && ancestor_zoom > tile->terrain_stand_in_zoom;
             --ancestor_zoom)
        {
            const int shift = tile->terrain_zoom - ancestor_zoom;
            const quint32 ancestor_x = address.x >> shift;
            const quint32 ancestor_y = address.y >> shift;
            const quint64 key = MapGlobeSurfaceScene::positionKey(
                ancestor_zoom, int(ancestor_x), int(ancestor_y));
            QHash<quint64, const MapTerrainTile *>::const_iterator found = ancestors.constFind(key);
            if (found == ancestors.cend())
            {
                found = ancestors.insert(key, this->terrain_repository->tile(
                    this->terrain_levels.dataset, ancestor_zoom, ancestor_x, ancestor_y));
            }
            const MapTerrainTile *candidate = found.value();
            if (candidate != nullptr
                && candidate->elevations_m.size() == MapTerrainTileSampleCount
                && mapGlobeTerrainDatumUsable(candidate->vertical_datum))
            {
                ancestor = candidate;
                break;
            }
        }
        if (ancestor == nullptr)
            continue;

        // Same grid as the final mesh: only the source heights are coarser. Resampled and built on the mesh thread.
        MapTerrainMeshRequest request = terrainMeshRequest(*tile, *ancestor);
        request.stand_in_zoom = ancestor->address.zoom;
        request.request_id = this->next_terrain_mesh_request_id++;
        tile->terrain_mesh_request_id = request.request_id;
        this->terrain_mesh_scheduler->submit(request);
    }
}

bool MapGlobeSurfacePreparation::applyReadyTerrainMeshes(
    QVector<MapGlobeSurfaceVertexPatch> *vertex_patches)
{
    if (vertex_patches != nullptr)
        vertex_patches->clear();

    QVector<MapTerrainMeshResult> results;
    if (this->terrain_mesh_scheduler != nullptr)
        this->terrain_mesh_scheduler->collectReady(&results);

    bool geometry_changed = false;
    for (const MapTerrainMeshResult &result : results)
    {
        for (MapGlobeSurfaceTile &tile : this->window_tiles)
        {
            if (tile.terrain_mesh_request_id != result.request_id)
                continue;

            tile.terrain_mesh_request_id = 0;
            if (!result.terrain_available
                || result.cell_count != tile.terrain_cell_count
                || result.vertices.size() != tile.vertex_count)
            {
                break;
            }

            const qsizetype first_vertex = tile.first_vertex;
            if (first_vertex < 0
                || first_vertex > this->window_vertices.size()
                || result.vertices.size()
                    > this->window_vertices.size() - first_vertex)
            {
                qWarning().noquote()
                    << QStringLiteral(
                           "Ignoring stale globe terrain mesh result outside "
                           "the current vertex window: request=%1 first=%2 "
                           "count=%3 window=%4")
                           .arg(result.request_id)
                           .arg(first_vertex)
                           .arg(result.vertices.size())
                           .arg(this->window_vertices.size());
                break;
            }

            for (qsizetype index = 0; index < result.vertices.size(); ++index)
            {
                const MapTerrainMeshVertex &vertex = result.vertices.at(index);
                MapGlobeSurfaceVertex &target =
                    this->window_vertices[first_vertex + index];
                target.x = vertex.x;
                target.y = vertex.y;
                target.z = vertex.z;
                target.u = vertex.u;
                target.v = vertex.v;
                target.normal = vertex.normal;
            }

            // A stand-in lies at the right height but is not the tile's own
            // mesh: that one is still scheduled once its DEM arrives.
            tile.terrain_mesh_applied = result.stand_in_zoom < 0;
            tile.terrain_missing_neighbor_mask = result.stand_in_zoom < 0 ? result.missing_neighbor_mask : 0;
            tile.terrain_mesh_has_relief = true;
            tile.terrain_stand_in_zoom = result.stand_in_zoom;
            this->updateTerrainRayBounds(&tile);
            geometry_changed = true;
            if (vertex_patches != nullptr)
            {
                vertex_patches->append(MapGlobeSurfaceVertexPatch{
                    first_vertex, result.vertices.size()});
            }
            break;
        }
    }


    if (geometry_changed && this->wireframe_visible)
        this->rebuildWireframeVertices();
    return geometry_changed;
}

bool MapGlobeSurfacePreparation::hasPendingTerrainMeshes() const
{
    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_mesh_request_id != 0)
            return true;
    }
    return false;
}

void MapGlobeSurfacePreparation::terrainMeshProgress(
    int *completed, int *total, bool *active) const
{
    int completed_count = 0;
    int total_count = 0;
    // Only meshes really being built count as activity.
    bool build_active = false;

    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (tile.terrain_key.isEmpty())
            continue;

        if (tile.terrain_mesh_applied)
        {
            ++completed_count;
            ++total_count;
        }
        else if (tile.terrain_mesh_request_id != 0)
        {
            ++total_count;
            build_active = true;
        }
    }

    if (completed != nullptr)
        *completed = completed_count;
    if (total != nullptr)
        *total = total_count;
    if (active != nullptr)
        *active = build_active;
}

bool MapGlobeSurfacePreparation::visibleTerrainRayIntersection(
    const GeoWgs84Ellipsoid::EcefPositionD &ray_origin_ecef,
    const QVector3D &ray_direction_ecef,
    GeoWgs84Ellipsoid::EcefPositionD *intersection_ecef,
    double *distance_m) const
{
    if (intersection_ecef == nullptr || distance_m == nullptr)
        return false;

    QVector3D direction = ray_direction_ecef;
    if (direction.lengthSquared() <= 1e-12f)
        return false;
    direction.normalize();

    const QVector3D relative_origin(
        float(ray_origin_ecef.x - this->render_origin_ecef.x),
        float(ray_origin_ecef.y - this->render_origin_ecef.y),
        float(ray_origin_ecef.z - this->render_origin_ecef.z));

    bool found = false;
    double nearest_distance_m = std::numeric_limits<double>::infinity();

    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        if (!tile.terrain_mesh_has_relief
            || !tile.terrain_ray_bounds_valid
            || tile.index_count < 3)
        {
            continue;
        }

        double bounds_entry_distance_m = 0.0;
        double bounds_exit_distance_m = 0.0;
        if (!rayAabbIntersectionDistanceRange(
                relative_origin, direction,
                tile.terrain_ray_bounds_min, tile.terrain_ray_bounds_max,
                &bounds_entry_distance_m, &bounds_exit_distance_m)
            || bounds_entry_distance_m >= nearest_distance_m)
        {
            continue;
        }

        // The grid's triangles only: the skirt below its edges is no
        // surface (mapGlobeTileIndices(): grid first, row by row).
        const int cell_count = tile.grid_cell_count;
        const QVector<quint16> &tile_indices = mapGlobeTileIndices(cell_count);
        if (cell_count < 1 || tile.index_count != mapGlobeTileIndexCount(cell_count)
            || tile_indices.size() != tile.index_count
            || tile.first_vertex < 0
            || qsizetype(tile.first_vertex) + tile.vertex_count > this->window_vertices.size())
        {
            continue;
        }
        const qsizetype index_begin = 0;
        const qsizetype index_end = mapGlobeTileGridIndexCount(cell_count);
        const qsizetype base_vertex = qsizetype(tile.first_vertex);

        const std::function<void(qsizetype, qsizetype)> test_index_range =
            [this, &tile_indices, base_vertex, &relative_origin, &direction, &nearest_distance_m, &found]
            (qsizetype range_begin, qsizetype range_end)
            {
                for (qsizetype index = range_begin;
                     index + 2 < range_end; index += 3)
                {
                    const qsizetype a_index = base_vertex + qsizetype(tile_indices.at(index));
                    const qsizetype b_index = base_vertex + qsizetype(tile_indices.at(index + 1));
                    const qsizetype c_index = base_vertex + qsizetype(tile_indices.at(index + 2));

                    const MapGlobeSurfaceVertex &a_vertex =
                        this->window_vertices.at(a_index);
                    const MapGlobeSurfaceVertex &b_vertex =
                        this->window_vertices.at(b_index);
                    const MapGlobeSurfaceVertex &c_vertex =
                        this->window_vertices.at(c_index);
                    const QVector3D a(
                        a_vertex.x, a_vertex.y, a_vertex.z);
                    const QVector3D b(
                        b_vertex.x, b_vertex.y, b_vertex.z);
                    const QVector3D c(
                        c_vertex.x, c_vertex.y, c_vertex.z);

                    double candidate_distance_m = 0.0;
                    if (!mapGlobeRayTriangleIntersectionDistance(
                            relative_origin, direction, a, b, c,
                            &candidate_distance_m)
                        || !(candidate_distance_m > 0.0)
                        || !mapGlobeUpdateNearestHitDistance(
                            candidate_distance_m, &nearest_distance_m))
                    {
                        continue;
                    }

                    found = true;
                }
            };

        const qsizetype row_index_count = qsizetype(cell_count) * 6;
        if (tile.terrain_ray_row_bounds.size() != cell_count)
        {
            test_index_range(index_begin, index_end);
            continue;
        }

        for (int row = 0; row < cell_count; ++row)
        {
            const MapGlobeSurfaceTerrainRayRowBounds &row_bounds =
                tile.terrain_ray_row_bounds.at(row);
            double row_entry_distance_m = 0.0;
            double row_exit_distance_m = 0.0;
            if (!rayAabbIntersectionDistanceRange(
                    relative_origin, direction,
                    row_bounds.minimum, row_bounds.maximum,
                    &row_entry_distance_m, &row_exit_distance_m)
                || row_entry_distance_m >= nearest_distance_m)
            {
                continue;
            }

            const qsizetype row_begin = index_begin
                + qsizetype(row) * row_index_count;
            const qsizetype row_end = qMin(
                row_begin + row_index_count, index_end);
            test_index_range(row_begin, row_end);
        }
    }

    if (!found)
        return false;

    intersection_ecef->x = ray_origin_ecef.x
        + double(direction.x()) * nearest_distance_m;
    intersection_ecef->y = ray_origin_ecef.y
        + double(direction.y()) * nearest_distance_m;
    intersection_ecef->z = ray_origin_ecef.z
        + double(direction.z()) * nearest_distance_m;
    *distance_m = nearest_distance_m;
    return true;
}

MapGlobeSurfacePreparation::DrawnSurfaceHeight MapGlobeSurfacePreparation::drawnSurfaceHeightAt(
    const CoordinateWGS84 &coordinate, double start_height_m, double *height_m) const
{
    // Normalised Web Mercator position (zoom 0); the tile at a zoom and the
    // position inside it follow by scaling.
    const double unit_x = GeoWebMercator::lonToTileX(
        GeoWebMercator::normalizeLongitude(coordinate.longitude_deg), 0);
    const double unit_y = GeoWebMercator::latToTileY(coordinate.latitude_deg, 0);
    if (!std::isfinite(unit_x) || !std::isfinite(unit_y))
        return DrawnSurfaceHeight::Unknown;

    for (int zoom = GlobeImageryMaxZoom; zoom >= 0; --zoom)
    {
        const double scale = std::ldexp(1.0, zoom);
        const double tile_position_x = unit_x * scale;
        const double tile_position_y = unit_y * scale;
        const int tile_count = 1 << zoom;
        const int tile_x = GeoWebMercator::wrapTileX(int(std::floor(tile_position_x)), zoom);
        const int tile_y = qBound(0, int(std::floor(tile_position_y)), tile_count - 1);
        const QHash<quint64, qsizetype>::const_iterator found =
            this->window_tile_indices_by_position.constFind(
                MapGlobeSurfaceScene::positionKey(zoom, tile_x, tile_y));
        if (found == this->window_tile_indices_by_position.cend())
            continue;

        const MapGlobeSurfaceTile &tile = this->window_tiles.at(found.value());
        if (!tile.terrain_mesh_has_relief)
            return DrawnSurfaceHeight::Flat;
        // The mesh is a row-major (cells + 1)^2 grid, uniform in the tile's
        // Mercator coordinates, followed by its skirt (mapGlobeTileIndices()).
        const int cell_count = tile.grid_cell_count;
        const int grid_width = cell_count + 1;
        if (cell_count < 1 || tile.vertex_count != mapGlobeTileVertexCount(cell_count)
            || tile.first_vertex + tile.vertex_count > this->window_vertices.size())
        {
            return DrawnSurfaceHeight::Unknown;
        }
        const double u = tile_position_x - std::floor(tile_position_x);
        const double v = qBound(0.0, tile_position_y - double(tile_y), 1.0);
        const int column = qBound(0, int(std::floor(u * cell_count)), cell_count - 1);
        const int row = qBound(0, int(std::floor(v * cell_count)), cell_count - 1);

        const GeoWgs84Ellipsoid::EcefPositionD start = GeoWgs84Ellipsoid::geodeticToEcefD(
            coordinate.longitude_deg, coordinate.latitude_deg, start_height_m);
        const QVector3D origin(float(start.x - this->render_origin_ecef.x),
                               float(start.y - this->render_origin_ecef.y),
                               float(start.z - this->render_origin_ecef.z));
        const QVector3D down = -GeoWgs84Ellipsoid::localFrameAtGeodetic(
            coordinate.longitude_deg, coordinate.latitude_deg, 0.0).up;
        const std::function<QVector3D(int, int)> vertex = [this, &tile, grid_width](int vertex_row, int vertex_column)
        {
            const MapGlobeSurfaceVertex &entry =
                this->window_vertices.at(tile.first_vertex + vertex_row * grid_width + vertex_column);
            return QVector3D(entry.x, entry.y, entry.z);
        };
        // The cell first; at a cell or tile edge (where float rounding can
        // let the ray slip between the triangles) its neighbours.
        double nearest_distance_m = std::numeric_limits<double>::infinity();
        for (int ring = 0; ring <= 1 && !std::isfinite(nearest_distance_m); ++ring)
        {
            for (int cell_row = qMax(0, row - ring); cell_row <= qMin(cell_count - 1, row + ring); ++cell_row)
            {
                for (int cell_column = qMax(0, column - ring);
                     cell_column <= qMin(cell_count - 1, column + ring); ++cell_column)
                {
                    if (ring > 0 && cell_row == row && cell_column == column)
                        continue;
                    const QVector3D p00 = vertex(cell_row, cell_column);
                    const QVector3D p10 = vertex(cell_row, cell_column + 1);
                    const QVector3D p01 = vertex(cell_row + 1, cell_column);
                    const QVector3D p11 = vertex(cell_row + 1, cell_column + 1);
                    double distance_m = 0.0;
                    if (mapGlobeRayTriangleIntersectionDistance(origin, down, p00, p01, p10, &distance_m)
                        && distance_m > 0.0)
                    {
                        nearest_distance_m = std::min(nearest_distance_m, distance_m);
                    }
                    if (mapGlobeRayTriangleIntersectionDistance(origin, down, p10, p01, p11, &distance_m)
                        && distance_m > 0.0)
                    {
                        nearest_distance_m = std::min(nearest_distance_m, distance_m);
                    }
                }
            }
        }
        if (!std::isfinite(nearest_distance_m))
            return DrawnSurfaceHeight::Unknown;
        *height_m = start_height_m - nearest_distance_m;
        return DrawnSurfaceHeight::Relief;
    }
    // No window tile covers the coordinate: nothing is drawn there (each
    // mesh is a height field over its own tile, so no other tile can be
    // above it either).
    return DrawnSurfaceHeight::Flat;
}

bool MapGlobeSurfacePreparation::visibleTerrainSamplingAtCoordinate(
    const CoordinateWGS84 &coordinate,
    int *terrain_zoom,
    double *cell_size_m) const
{
    if (terrain_zoom == nullptr || cell_size_m == nullptr
        || !std::isfinite(coordinate.longitude_deg)
        || !std::isfinite(coordinate.latitude_deg))
    {
        return false;
    }

    *terrain_zoom = -1;
    *cell_size_m = 0.0;

    const MapGlobeSurfaceTile *resolved_tile = nullptr;
    for (int zoom = GlobeImageryMaxZoom; zoom >= 0; --zoom)
    {
        const double coordinate_tile_x = GeoWebMercator::lonToTileX(
            coordinate.longitude_deg, zoom);
        const double coordinate_tile_y = GeoWebMercator::latToTileY(
            coordinate.latitude_deg, zoom);
        const int tile_count = 1 << zoom;
        const int coordinate_x = GeoWebMercator::wrapTileX(
            int(std::floor(coordinate_tile_x)), zoom);
        const int coordinate_y = qBound(
            0, int(std::floor(coordinate_tile_y)), tile_count - 1);
        const quint64 position_key = MapGlobeSurfaceScene::positionKey(
            zoom, coordinate_x, coordinate_y);
        const QHash<quint64, qsizetype>::const_iterator tile_iterator =
            this->window_tile_indices_by_position.constFind(position_key);
        if (tile_iterator == this->window_tile_indices_by_position.constEnd())
            continue;

        const qsizetype tile_index = tile_iterator.value();
        if (tile_index < 0 || tile_index >= this->window_tiles.size())
            return false;
        resolved_tile = &this->window_tiles.at(tile_index);
        break;
    }

    if (resolved_tile == nullptr || resolved_tile->is_cap
        || resolved_tile->terrain_key.isEmpty()
        || resolved_tile->terrain_zoom < GlobeTerrainReliefMinimumZoom
        || resolved_tile->terrain_cell_count <= 0
        || !resolved_tile->terrain_mesh_has_relief)
    {
        return false;
    }

    const int tile_count = 1 << resolved_tile->zoom;
    const double latitude_top_deg = GeoWebMercator::tileYToLat(
        double(resolved_tile->tile_y), resolved_tile->zoom);
    const double latitude_bottom_deg = GeoWebMercator::tileYToLat(
        double(resolved_tile->tile_y) + 1.0, resolved_tile->zoom);
    const double tile_width_m =
        (2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
         * qMax(0.0, std::cos(qDegreesToRadians(coordinate.latitude_deg))))
        / double(tile_count);
    const double tile_height_m = GeoWgs84Ellipsoid::EquatorialRadiusM
        * std::abs(qDegreesToRadians(
            latitude_top_deg - latitude_bottom_deg));
    const double reference_size_m = qMax(tile_width_m, tile_height_m);
    const double resolved_cell_size_m =
        reference_size_m / double(resolved_tile->terrain_cell_count);
    if (!std::isfinite(resolved_cell_size_m) || resolved_cell_size_m <= 0.0)
        return false;

    *terrain_zoom = resolved_tile->terrain_zoom;
    *cell_size_m = resolved_cell_size_m;
    return true;
}

void MapGlobeSurfacePreparation::rebuildWireframeVertices()
{
    this->wireframe_vertices.clear();
    qsizetype grid_index_count = this->cap_indices.size();
    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
        grid_index_count += mapGlobeTileGridIndexCount(qMax(0, tile.grid_cell_count));
    this->wireframe_vertices.reserve(grid_index_count * 2);
    // Each tile's grid; its skirt is a hidden crack filler, not mesh
    // structure worth showing.
    for (const MapGlobeSurfaceTile &tile : this->window_tiles)
    {
        const QVector<quint16> &tile_indices = mapGlobeTileIndices(tile.grid_cell_count);
        if (tile.grid_cell_count >= 1 && tile.index_count == tile_indices.size())
        {
            appendWireframeEdges(&this->wireframe_vertices, this->window_vertices, tile.first_vertex,
                                 tile_indices.constData(), mapGlobeTileGridIndexCount(tile.grid_cell_count));
        }
    }
    appendWireframeEdges(&this->wireframe_vertices, this->cap_vertices, 0,
                         this->cap_indices.constData(), this->cap_indices.size());
}

const GeoWgs84Ellipsoid::EcefPositionD &
MapGlobeSurfacePreparation::renderOriginEcef() const
{
    return this->render_origin_ecef;
}

const QVector<MapGlobeSurfaceVertex> &
MapGlobeSurfacePreparation::windowVertices() const
{
    return this->window_vertices;
}

const QVector<MapGlobeSurfaceTile> &
MapGlobeSurfacePreparation::windowTiles() const
{
    return this->window_tiles;
}

QVector<MapGlobeSurfaceTile> &MapGlobeSurfacePreparation::windowTiles()
{
    return this->window_tiles;
}

const QSet<quint64> &MapGlobeSurfacePreparation::windowPositionKeys() const
{
    return this->window_position_keys;
}

bool MapGlobeSurfacePreparation::windowDirty() const
{
    return this->window_dirty;
}

const QVector<MapGlobeSurfaceVertex> &
MapGlobeSurfacePreparation::capVertices() const
{
    return this->cap_vertices;
}

const QVector<quint32> &MapGlobeSurfacePreparation::capIndices() const
{
    return this->cap_indices;
}

const QVector<MapGlobeSurfaceTile> &
MapGlobeSurfacePreparation::capTiles() const
{
    return this->cap_tiles;
}

QVector<MapGlobeSurfaceTile> &MapGlobeSurfacePreparation::capTiles()
{
    return this->cap_tiles;
}

const QVector<MapGlobeSurfaceWireframeVertex> &
MapGlobeSurfacePreparation::wireframeVertices() const
{
    return this->wireframe_vertices;
}
