#include "map/render/map_globe_terrain_navigation.h"

#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "map/core/map_model.h"
#include "map/data/map_terrain_tile_source.h"
#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_camera.h"
#include "map/render/map_globe_picking.h"
#include "map/render/map_globe_surface_preparation.h"
#include "map/render/map_globe_vertical_transform.h"
#include "map/render/map_render_diagnostics.h"

#include <QDebug>
#include <QScopedValueRollback>
#include <QtMath>

#include <cmath>

// Everything below is the removed QRhi map widget's terrain code with its
// renderer replaced by MapGlobeSurfacePreparation (which the QRhi globe
// renderer only forwarded to). Constants and comments are kept as they were.
namespace
{
// One symmetric, frame-rate-independent low-pass replaces the previous
// velocity-carrying spring. It can never overshoot or rebound when the terrain
// target reverses at a ridge/valley. The terrain-follow path uses this same
// monotonic exponential shape, but with asymmetric rise/fall constants; the
// Globe value deliberately treats both directions alike.
constexpr double GlobeTerrainFollowTimeConstantSeconds = 0.22;
constexpr double GlobeCollisionFollowTimeConstantSeconds = 0.16;
// Stop once the remaining motion is below a quarter of a logical screen
// pixel. A fixed one-centimetre threshold kept the complete Globe renderer
// awake for dozens of visually indistinguishable tail frames when zoomed
// out. Retain a small metric floor for very close views.
constexpr double GlobeTerrainFollowSettlePixels = 0.25;
constexpr double GlobeTerrainFollowMinimumSettleDistanceM = 0.01;
constexpr double GlobeTerrainMaximumTimeStepSeconds = 1.0 / 30.0;

double approachExponentially(double current, double target, double time_constant_seconds,
                             double elapsed_seconds)
{
    if (!(time_constant_seconds > 0.0) || !(elapsed_seconds > 0.0))
        return current;

    const double blend = 1.0 - std::exp(-elapsed_seconds / time_constant_seconds);
    return current + (target - current) * blend;
}

double terrainSample(const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    return double(terrain_tile.elevations_m.at(bounded_row * MapTerrainTileGridSize + bounded_column));
}

// Nearest finite sample in growing square rings (DEM voids).
double nearestFiniteTerrainElevation(const MapTerrainTile &terrain_tile, int row, int column)
{
    const int bounded_row = qBound(0, row, MapTerrainTileGridSize - 1);
    const int bounded_column = qBound(0, column, MapTerrainTileGridSize - 1);
    const double direct = terrainSample(terrain_tile, bounded_row, bounded_column);
    if (std::isfinite(direct))
        return direct;

    for (int ring = 1; ring < MapTerrainTileGridSize; ++ring)
    {
        const int row_minimum = qMax(0, bounded_row - ring);
        const int row_maximum = qMin(MapTerrainTileGridSize - 1, bounded_row + ring);
        const int column_minimum = qMax(0, bounded_column - ring);
        const int column_maximum = qMin(MapTerrainTileGridSize - 1, bounded_column + ring);
        for (int sample_column = column_minimum; sample_column <= column_maximum; ++sample_column)
        {
            const double top = terrainSample(terrain_tile, row_minimum, sample_column);
            if (std::isfinite(top))
                return top;
            const double bottom = terrainSample(terrain_tile, row_maximum, sample_column);
            if (std::isfinite(bottom))
                return bottom;
        }
        for (int sample_row = row_minimum + 1; sample_row < row_maximum; ++sample_row)
        {
            const double left = terrainSample(terrain_tile, sample_row, column_minimum);
            if (std::isfinite(left))
                return left;
            const double right = terrainSample(terrain_tile, sample_row, column_maximum);
            if (std::isfinite(right))
                return right;
        }
    }

    return qQNaN();
}

double bilinearTerrainElevation(const MapTerrainTile &terrain_tile, double u, double v)
{
    if (terrain_tile.elevations_m.size() != MapTerrainTileSampleCount)
        return qQNaN();

    const double sample_x = qBound(0.0, u, 1.0) * MapTerrainTileCellCount;
    const double sample_y = qBound(0.0, v, 1.0) * MapTerrainTileCellCount;
    const int x0 = qBound(0, int(std::floor(sample_x)), MapTerrainTileGridSize - 1);
    const int y0 = qBound(0, int(std::floor(sample_y)), MapTerrainTileGridSize - 1);
    const int x1 = qMin(x0 + 1, MapTerrainTileGridSize - 1);
    const int y1 = qMin(y0 + 1, MapTerrainTileGridSize - 1);
    const double tx = sample_x - x0;
    const double ty = sample_y - y0;
    const double samples[4] = {
        terrainSample(terrain_tile, y0, x0),
        terrainSample(terrain_tile, y0, x1),
        terrainSample(terrain_tile, y1, x0),
        terrainSample(terrain_tile, y1, x1)
    };
    const double weights[4] = {
        (1.0 - tx) * (1.0 - ty),
        tx * (1.0 - ty),
        (1.0 - tx) * ty,
        tx * ty
    };

    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    for (int index = 0; index < 4; ++index)
    {
        if (!std::isfinite(samples[index]))
            continue;
        weighted_sum += samples[index] * weights[index];
        weight_sum += weights[index];
    }
    if (weight_sum > 0.0)
        return weighted_sum / weight_sum;
    return nearestFiniteTerrainElevation(terrain_tile, int(std::lround(sample_y)),
                                         int(std::lround(sample_x)));
}

// Given a terrain tile and a normalized (0..1) position within it, samples
// elevation using the same z00->z11 triangle split the terrain mesh
// renderer actually builds (see buildTerrainMeshResult()), rather than a
// plain bilinear blend across the untriangulated quad -- the two disagree
// slightly inside each cell, which previously let an otherwise-correct pick
// ray appear to land inside a steep slope.
bool sampleTerrainTileElevationAt(const MapTerrainTile &terrain_tile, double local_u, double local_v,
                                  double *elevation_m)
{
    const double sample_x = qBound(0.0, local_u, 1.0) * MapTerrainTileCellCount;
    const double sample_y = qBound(0.0, local_v, 1.0) * MapTerrainTileCellCount;
    const int cell_x = qBound(0, int(std::floor(sample_x)), MapTerrainTileCellCount - 1);
    const int cell_y = qBound(0, int(std::floor(sample_y)), MapTerrainTileCellCount - 1);
    const double tx = qBound(0.0, sample_x - cell_x, 1.0);
    const double ty = qBound(0.0, sample_y - cell_y, 1.0);
    const double u0 = double(cell_x) / MapTerrainTileCellCount;
    const double u1 = double(cell_x + 1) / MapTerrainTileCellCount;
    const double v0 = double(cell_y) / MapTerrainTileCellCount;
    const double v1 = double(cell_y + 1) / MapTerrainTileCellCount;
    const double z00 = bilinearTerrainElevation(terrain_tile, u0, v0);
    const double z10 = bilinearTerrainElevation(terrain_tile, u1, v0);
    const double z11 = bilinearTerrainElevation(terrain_tile, u1, v1);
    const double z01 = bilinearTerrainElevation(terrain_tile, u0, v1);
    if (!std::isfinite(z00) || !std::isfinite(z10) || !std::isfinite(z11) || !std::isfinite(z01))
        return false;

    double sampled_elevation_m = 0.0;
    if (ty <= tx)
        sampled_elevation_m = z00 * (1.0 - tx) + z10 * (tx - ty) + z11 * ty;
    else
        sampled_elevation_m = z00 * (1.0 - ty) + z11 * tx + z01 * (ty - tx);

    if (!std::isfinite(sampled_elevation_m))
        return false;

    *elevation_m = sampled_elevation_m;
    return true;
}

// Eye position of the model's orbit rig (pitch and distance clamped like
// MapGlobeCamera), with the current vertical offset and collision lift.
GeoWgs84Ellipsoid::EcefPositionD currentEyeEcef(const MapModel &map_model)
{
    const double pitch_deg = qBound(MapModel::MinViewGlobeFreeLookPitchDeg, map_model.viewGlobePitchDeg(),
                                    MapModel::MaxViewGlobePitchDeg);
    const double distance_m = qMax(MapModel::MinViewGlobeDistanceM, map_model.viewGlobeDistanceM());
    return GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
        map_model.centerLon(), map_model.centerLat(), map_model.viewGlobeYawDeg(), pitch_deg,
        distance_m, map_model.viewGlobeVerticalOffsetM(), map_model.viewGlobeCameraCollisionLiftM());
}


GeoWgs84Ellipsoid::EcefPositionD rayPoint(const GeoWgs84Ellipsoid::EcefPositionD &origin,
                                          double direction_x, double direction_y, double direction_z,
                                          double distance_m)
{
    GeoWgs84Ellipsoid::EcefPositionD point;
    point.x = origin.x + direction_x * distance_m;
    point.y = origin.y + direction_y * distance_m;
    point.z = origin.z + direction_z * distance_m;
    return point;
}
}

MapGlobeTerrainNavigation::MapGlobeTerrainNavigation(MapModel *map_model)
    : map_model(map_model)
{
}

void MapGlobeTerrainNavigation::setTerrainDetailReductionSource(const std::function<int()> &source)
{
    this->terrain_detail_reduction_source = source;
}

void MapGlobeTerrainNavigation::setTerrainRepository(MapTerrainTileSource *terrain_repository)
{
    this->terrain_repository = terrain_repository;
}

void MapGlobeTerrainNavigation::setSurfacePreparation(
    const MapGlobeSurfacePreparation *surface_preparation)
{
    this->surface_preparation = surface_preparation;
}

void MapGlobeTerrainNavigation::setViewportSize(const QSize &viewport_size)
{
    this->viewport_size = viewport_size;
}

bool MapGlobeTerrainNavigation::terrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                                             double *elevation_m,
                                                             double *cell_size_m) const
{
    if (elevation_m == nullptr || cell_size_m == nullptr || this->terrain_repository == nullptr
        || !std::isfinite(coordinate.longitude_deg) || !std::isfinite(coordinate.latitude_deg))
    {
        return false;
    }

    *cell_size_m = 0.0;

    // Prefer the terrain mesh LOD that is actually retained under this
    // coordinate. This keeps X-Ray classification density tied to the same
    // surface detail the user sees, instead of a fixed metre interval.
    int visible_terrain_zoom = -1;
    double visible_cell_size_m = 0.0;
    if (this->surface_preparation != nullptr)
    {
        this->surface_preparation->visibleTerrainSamplingAtCoordinate(
            coordinate, &visible_terrain_zoom, &visible_cell_size_m);
    }

    // The dataset's own zoom range (its catalog), nothing hardcoded.
    const MapGlobeTerrainLevels levels = mapGlobeTerrainLevels(
        this->terrain_repository,
        mapGlobeTerrainDetailReduction(this->terrain_detail_reduction_source));
    if (!levels.enabled())
        return false;
    const QString &dataset = levels.dataset;
    for (int pass = 0; pass < 2; ++pass)
    {
        const int first_zoom = pass == 0 && visible_terrain_zoom >= levels.minimum_zoom
            ? visible_terrain_zoom
            : levels.maximum_zoom;
        const int last_zoom = pass == 0 && visible_terrain_zoom >= levels.minimum_zoom
            ? visible_terrain_zoom
            : levels.minimum_zoom;

        for (int terrain_zoom = first_zoom; terrain_zoom >= last_zoom; --terrain_zoom)
        {
            if (pass == 1 && terrain_zoom == visible_terrain_zoom)
                continue;

            const double terrain_tile_x = GeoWebMercator::lonToTileX(coordinate.longitude_deg, terrain_zoom);
            const double terrain_tile_y = GeoWebMercator::latToTileY(coordinate.latitude_deg, terrain_zoom);
            const int terrain_tile_count = 1 << terrain_zoom;
            const int tile_x_unwrapped = int(std::floor(terrain_tile_x));
            const int tile_y = qBound(0, int(std::floor(terrain_tile_y)), terrain_tile_count - 1);
            const int tile_x = GeoWebMercator::wrapTileX(tile_x_unwrapped, terrain_zoom);

            const MapTerrainTile *terrain_tile = this->terrain_repository->tile(
                dataset, terrain_zoom, quint32(tile_x), quint32(tile_y));
            if (terrain_tile == nullptr)
                continue;

            const double local_u = terrain_tile_x - std::floor(terrain_tile_x);
            const double local_v = terrain_tile_y - std::floor(terrain_tile_y);
            double sampled_elevation_m = 0.0;
            if (!sampleTerrainTileElevationAt(*terrain_tile, local_u, local_v, &sampled_elevation_m))
                continue;

            double resolved_cell_size_m = 0.0;
            if (terrain_zoom == visible_terrain_zoom && std::isfinite(visible_cell_size_m)
                && visible_cell_size_m > 0.0)
            {
                resolved_cell_size_m = visible_cell_size_m;
            }
            else
            {
                const double latitude_top_deg = GeoWebMercator::tileYToLat(double(tile_y), terrain_zoom);
                const double latitude_bottom_deg = GeoWebMercator::tileYToLat(double(tile_y) + 1.0,
                                                                              terrain_zoom);
                const double tile_width_m =
                    (2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM
                     * qMax(0.0, std::cos(qDegreesToRadians(coordinate.latitude_deg))))
                    / double(terrain_tile_count);
                const double tile_height_m = GeoWgs84Ellipsoid::EquatorialRadiusM
                    * std::abs(qDegreesToRadians(latitude_top_deg - latitude_bottom_deg));
                resolved_cell_size_m = qMax(tile_width_m, tile_height_m) / double(MapTerrainTileCellCount);
            }

            if (std::isfinite(terrain_tile->nominal_resolution_m) && terrain_tile->nominal_resolution_m > 0.0)
                resolved_cell_size_m = qMax(resolved_cell_size_m, terrain_tile->nominal_resolution_m);

            *elevation_m = sampled_elevation_m;
            *cell_size_m = resolved_cell_size_m;
            return true;
        }

        if (visible_terrain_zoom < levels.minimum_zoom)
            break;
    }

    return false;
}

bool MapGlobeTerrainNavigation::drawnSurfaceHeightAtCoordinate(const CoordinateWGS84 &coordinate,
                                                               double *height_m) const
{
    *height_m = 0.0;
    if (this->surface_preparation == nullptr || !std::isfinite(coordinate.longitude_deg)
        || !std::isfinite(coordinate.latitude_deg))
    {
        return false;
    }
    // A ray down the ellipsoid normal from above any exaggerated terrain.
    // Geodetic height changes linearly along the normal, so the surface
    // height is the start height minus the distance to the first (top)
    // triangle the ray meets.
    constexpr double HighestTerrainM = 9000.0;
    const double start_height_m = HighestTerrainM * MapModel::MaxView3dVerticalExaggeration + 1000.0;
    // The tile under the coordinate directly (cheap: demand points are
    // draped after every terrain change); the scan over all tiles only when
    // that cannot place the coordinate.
    switch (this->surface_preparation->drawnSurfaceHeightAt(coordinate, start_height_m, height_m))
    {
    case MapGlobeSurfacePreparation::DrawnSurfaceHeight::Relief:
        return true;
    case MapGlobeSurfacePreparation::DrawnSurfaceHeight::Flat:
        *height_m = 0.0;
        return false;
    case MapGlobeSurfacePreparation::DrawnSurfaceHeight::Unknown:
        break;
    }
    const GeoWgs84Ellipsoid::EcefPositionD ray_origin = GeoWgs84Ellipsoid::geodeticToEcefD(
        coordinate.longitude_deg, coordinate.latitude_deg, start_height_m);
    const GeoWgs84Ellipsoid::LocalFrame frame = GeoWgs84Ellipsoid::localFrameAtGeodetic(
        coordinate.longitude_deg, coordinate.latitude_deg, 0.0);
    GeoWgs84Ellipsoid::EcefPositionD intersection;
    double distance_m = 0.0;
    if (!this->surface_preparation->visibleTerrainRayIntersection(ray_origin, -frame.up, &intersection,
                                                                  &distance_m))
    {
        return false;
    }
    *height_m = start_height_m - distance_m;
    return true;
}

bool MapGlobeTerrainNavigation::cachedTerrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                                                   double *elevation_m) const
{
    if (elevation_m == nullptr)
        return false;

    double cell_size_m = 0.0;
    return terrainElevationAtCoordinate(coordinate, elevation_m, &cell_size_m);
}

bool MapGlobeTerrainNavigation::cameraTerrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                                                   double *elevation_m,
                                                                   bool request_missing_tile) const
{
    if (elevation_m == nullptr || this->terrain_repository == nullptr || this->map_model == nullptr
        || !this->viewport_size.isValid() || !std::isfinite(coordinate.longitude_deg)
        || !std::isfinite(coordinate.latitude_deg))
    {
        return false;
    }

    // The target leaf changes at the midpoint between continuous zoom
    // levels (the 512 px projected-diagonal threshold in the Globe
    // quadtree), so qRound() selects the terrain zoom belonging to the tile
    // beneath the camera. The 2D zoom (view2dContinuousZoom()) belongs to
    // the flat view and can be completely unrelated to Globe distance.
    const int imagery_zoom = qBound(0, qRound(this->map_model->viewGlobeZoomLevel(this->viewport_size)),
                                    MapModel::MaxZoom);
    const MapGlobeTerrainLevels levels = mapGlobeTerrainLevels(
        this->terrain_repository,
        mapGlobeTerrainDetailReduction(this->terrain_detail_reduction_source));
    if (!levels.reliefAtImageryZoom(imagery_zoom))
    {
        // Globe relief is deliberately disabled below this LOD (and before
        // the terrain dataset is known), where the rendered surface is the
        // zero-height ellipsoid.
        *elevation_m = 0.0;
        return true;
    }

    // The DEM zoom the surface draws under the camera (the same levels).
    const int target_terrain_zoom = levels.terrainZoomForImageryZoom(imagery_zoom);
    const QString &dataset = levels.dataset;

    // Request only the exact current LOD. While it is loading, a cached
    // coarser tile gives the filter a stable provisional surface instead of
    // snapping the rig to zero and back as tile boundaries are crossed.
    for (int terrain_zoom = target_terrain_zoom; terrain_zoom >= levels.minimum_zoom; --terrain_zoom)
    {
        const double terrain_tile_x = GeoWebMercator::lonToTileX(coordinate.longitude_deg, terrain_zoom);
        const double terrain_tile_y = GeoWebMercator::latToTileY(coordinate.latitude_deg, terrain_zoom);
        const int terrain_tile_count = 1 << terrain_zoom;
        const int tile_x = GeoWebMercator::wrapTileX(int(std::floor(terrain_tile_x)), terrain_zoom);
        const int tile_y = qBound(0, int(std::floor(terrain_tile_y)), terrain_tile_count - 1);

        const MapTerrainTile *terrain_tile = this->terrain_repository->tile(
            dataset, terrain_zoom, quint32(tile_x), quint32(tile_y));
        if (terrain_tile == nullptr)
        {
            if (request_missing_tile && terrain_zoom == target_terrain_zoom)
                this->terrain_repository->requestTile(dataset, terrain_zoom, quint32(tile_x), quint32(tile_y));
            continue;
        }
        // Like the surface: unknown/local heights cannot be interpreted as
        // displacement from the global ellipsoid.
        if (!mapGlobeTerrainDatumUsable(terrain_tile->vertical_datum))
            continue;

        const double local_u = terrain_tile_x - std::floor(terrain_tile_x);
        const double local_v = terrain_tile_y - std::floor(terrain_tile_y);
        if (sampleTerrainTileElevationAt(*terrain_tile, local_u, local_v, elevation_m))
            return true;
    }

    return false;
}

bool MapGlobeTerrainNavigation::surfaceRayHitAtScreen(const QPointF &screen_position,
                                                      MapGlobeSurfaceHit *hit) const
{
    if (hit == nullptr)
        return false;

    *hit = MapGlobeSurfaceHit{};
    if (this->map_model == nullptr || !this->viewport_size.isValid())
        return false;

    // The camera as the model has it now (input arrives between frames).
    MapGlobeCamera camera;
    camera.setViewportSize(this->viewport_size);
    camera.syncFromMapModel(*this->map_model);
    MapGlobeScreenRay ray;
    if (!camera.screenRay(screen_position, &ray))
        return false;

    GeoWgs84Ellipsoid::EcefPositionD intersection;
    double hit_distance_m = 0.0;
    MapGlobeSurfaceHitSource source = MapGlobeSurfaceHitSource::EllipsoidFallback;
    if (this->surface_preparation != nullptr
        && this->surface_preparation->visibleTerrainRayIntersection(
            ray.origin_ecef, ray.direction, &intersection, &hit_distance_m))
    {
        source = MapGlobeSurfaceHitSource::TerrainMesh;
    }
    else if (!GeoWgs84Ellipsoid::rayIntersection(ray.origin_ecef, ray.direction, &intersection,
                                                 &hit_distance_m))
    {
        return false;
    }

    double lon_deg = 0.0;
    double lat_deg = 0.0;
    double surface_height_m = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(intersection, &lon_deg, &lat_deg, &surface_height_m))
        return false;

    hit->coordinate.longitude_deg = lon_deg;
    hit->coordinate.latitude_deg = lat_deg;
    hit->ecef_position = intersection;
    hit->surface_height_m = surface_height_m;
    hit->distance_m = hit_distance_m;
    hit->source = source;
    return hit->isValid();
}

bool MapGlobeTerrainNavigation::terrainCoordinateAtScreen(const QPointF &screen_position,
                                                          CoordinateWGS84 *coordinate)
{
    if (coordinate == nullptr || this->map_model == nullptr)
        return false;

    if (this->map_model->viewMode() == MapViewMode::Globe)
    {
        MapGlobeSurfaceHit hit;
        if (!surfaceRayHitAtScreen(screen_position, &hit))
            return false;

        *coordinate = hit.coordinate;
        double altitude_m = 0.0;
        double cell_size_m = 0.0;
        if (hit.isTerrainMesh() && terrainElevationAtCoordinate(*coordinate, &altitude_m, &cell_size_m))
            coordinate->altitude_m = altitude_m;
        else
            coordinate->altitude_m.reset();
        return true;
    }

    if (!this->viewport_size.isValid())
        return false;

    *coordinate = this->map_model->wgs84FromScreen(screen_position.toPoint(), this->viewport_size);
    if (!std::isfinite(coordinate->longitude_deg) || !std::isfinite(coordinate->latitude_deg))
        return false;

    // The 2D map uses the 3D terrain cache for its footer height, without
    // activating globe rendering or requesting missing DEM tiles.
    double altitude_m = 0.0;
    double cell_size_m = 0.0;
    if (terrainElevationAtCoordinate(*coordinate, &altitude_m, &cell_size_m))
        coordinate->altitude_m = altitude_m;
    else
        coordinate->altitude_m.reset();
    return true;
}

void MapGlobeTerrainNavigation::stopFollowing()
{
    this->follow_active = false;
    this->follow_clock.invalidate();
}

void MapGlobeTerrainNavigation::syncCameraHeight(bool request_missing_tile)
{
    if (this->sync_active)
        return;

    if (this->map_model == nullptr || this->map_model->viewMode() != MapViewMode::Globe
        || this->map_model->globeEyePose() || !this->viewport_size.isValid())
    {
        stopFollowing();
        return;
    }

    const QScopedValueRollback<bool> sync_guard(this->sync_active, true);

    const double pitch_deg = qBound(MapModel::MinViewGlobeFreeLookPitchDeg, this->map_model->viewGlobePitchDeg(),
                                    MapModel::MaxViewGlobePitchDeg);
    const double distance_m = qMax(MapModel::MinViewGlobeDistanceM, this->map_model->viewGlobeDistanceM());
    const double current_offset_m = this->map_model->viewGlobeVerticalOffsetM();
    const double current_collision_lift_m = this->map_model->viewGlobeCameraCollisionLiftM();
    // Free look has no gravity: the target (and with it the eye's height)
    // stays where the look left it, only the clearance lift still keeps the
    // eye above the terrain, as while orbiting.
    const bool orbit_target_frozen =
        this->map_model->view3dNavigationState() == MapView3dNavigationState::Rotate
        || this->map_model->viewGlobeCameraMode() == MapGlobeCameraMode::FreeLook;
    const double viewport_height_px = double(qMax(1, this->viewport_size.height()));
    const double meters_per_pixel = 2.0 * distance_m
        * std::tan(qDegreesToRadians(MapModel::GlobeFieldOfViewDeg * 0.5)) / viewport_height_px;
    const double settle_distance_m = qMax(GlobeTerrainFollowMinimumSettleDistanceM,
                                          GlobeTerrainFollowSettlePixels * meters_per_pixel);

    // GeographicLib's ECEF reverse conversion returns the point reached by
    // dropping the WGS84-normal from the actual eye. That is the terrain
    // perpendicularly beneath the camera; centerLon()/centerLat() is the
    // crosshair/orbit target and can be hundreds of meters away at low pitch.
    const GeoWgs84Ellipsoid::EcefPositionD current_eye = currentEyeEcef(*this->map_model);
    CoordinateWGS84 camera_coordinate;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(current_eye, &camera_coordinate.longitude_deg,
                                           &camera_coordinate.latitude_deg))
    {
        return;
    }

    double camera_terrain_elevation_m = 0.0;
    const bool terrain_available = this->terrain_repository != nullptr
        && cameraTerrainElevationAtCoordinate(camera_coordinate, &camera_terrain_elevation_m,
                                              request_missing_tile);
    if (!terrain_available)
    {
        // Match the surface actually rendered while no cached DEM level is
        // available. The monotonic filter approaches this zero-height fallback
        // instead of snapping to it; a later DEM arrival is filtered likewise.
        camera_terrain_elevation_m = 0.0;
    }

    const MapGlobeVerticalTransform vertical_transform(this->map_model->view3dVerticalExaggeration(),
                                                       this->map_model->view3dNetworkGroundOffsetM());
    const double rendered_camera_terrain_elevation_m =
        vertical_transform.terrainHeightM(camera_terrain_elevation_m);

    // Compute the height naturally provided by yaw/pitch/distance over the
    // bare ellipsoid. Usually it is far above the minimum; retaining this
    // exact curvature-aware calculation also makes low-pitch close views
    // safe without treating distance*sin(pitch) as exact Globe altitude.
    const GeoWgs84Ellipsoid::EcefPositionD zero_height_eye = GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
        this->map_model->centerLon(), this->map_model->centerLat(), this->map_model->viewGlobeYawDeg(),
        pitch_deg, distance_m);
    double nominal_lon_deg = 0.0;
    double nominal_lat_deg = 0.0;
    double nominal_eye_height_m = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(zero_height_eye, &nominal_lon_deg, &nominal_lat_deg,
                                           &nominal_eye_height_m))
    {
        return;
    }
    // This is deliberately the one exact DEM sample perpendicularly beneath
    // the eye. A footprint combining independently cached LODs changes
    // membership at tile boundaries, creating discontinuous targets.
    const double target_offset_m = rendered_camera_terrain_elevation_m
        + qMax(0.0, MapModel::MinViewGlobeCameraGroundClearanceM - nominal_eye_height_m);

    double elapsed_seconds = 1.0 / 60.0;
    if (this->follow_clock.isValid())
    {
        elapsed_seconds = qBound(0.0, double(this->follow_clock.nsecsElapsed()) * 1e-9,
                                 GlobeTerrainMaximumTimeStepSeconds);
        this->follow_clock.restart();
    }
    else
    {
        this->follow_clock.start();
    }

    double next_offset_m = current_offset_m;
    double next_collision_lift_m = current_collision_lift_m;
    double required_collision_lift_m = 0.0;

    if (orbit_target_frozen)
    {
        // Keep the captured orbit target fixed. Only the eye-only clearance
        // target changes while rotating, and that path is filtered below by
        // the same monotonic mechanism as ordinary terrain following.
        const GeoWgs84Ellipsoid::EcefPositionD unlifted_eye = GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
            this->map_model->centerLon(), this->map_model->centerLat(), this->map_model->viewGlobeYawDeg(),
            pitch_deg, distance_m, next_offset_m);
        double unlifted_lon_deg = 0.0;
        double unlifted_lat_deg = 0.0;
        double unlifted_eye_height_m = 0.0;
        if (!GeoWgs84Ellipsoid::ecefToGeodetic(unlifted_eye, &unlifted_lon_deg, &unlifted_lat_deg,
                                               &unlifted_eye_height_m))
        {
            return;
        }
        required_collision_lift_m = qMax(0.0, rendered_camera_terrain_elevation_m
                                                  + MapModel::MinViewGlobeCameraGroundClearanceM
                                                  - unlifted_eye_height_m);
    }

    // The eye-only lift is needed only while rotating around a frozen pivot.
    // On return to Pan, ease it to zero and transfer exactly the released
    // amount into the whole-rig offset. That keeps the eye position continuous
    // while the target itself also returns smoothly to ordinary terrain follow.
    const double collision_lift_before_step_m = next_collision_lift_m;
    next_collision_lift_m = approachExponentially(next_collision_lift_m, required_collision_lift_m,
                                                  GlobeCollisionFollowTimeConstantSeconds, elapsed_seconds);
    if (std::abs(required_collision_lift_m - next_collision_lift_m) <= settle_distance_m)
        next_collision_lift_m = required_collision_lift_m;
    if (!orbit_target_frozen)
    {
        next_offset_m += collision_lift_before_step_m - next_collision_lift_m;
        next_offset_m = approachExponentially(next_offset_m, target_offset_m,
                                              GlobeTerrainFollowTimeConstantSeconds, elapsed_seconds);
        if (std::abs(target_offset_m - next_offset_m) <= settle_distance_m)
            next_offset_m = target_offset_m;
    }

    this->map_model->setViewGlobeTerrainHeightOffsetsM(next_offset_m, next_collision_lift_m);

    const bool follow_animating = !orbit_target_frozen
        && std::abs(target_offset_m - next_offset_m) > settle_distance_m;
    const bool collision_animating =
        std::abs(required_collision_lift_m - next_collision_lift_m) > settle_distance_m;
    if (follow_animating || collision_animating)
        this->follow_active = true;
    else
        stopFollowing();
}

bool MapGlobeTerrainNavigation::anchorOrbitAtCrosshair()
{
    if (this->map_model == nullptr || this->map_model->viewMode() != MapViewMode::Globe
        || this->map_model->globeEyePose() || !this->viewport_size.isValid())
    {
        return false;
    }

    const QPointF crosshair(double(this->viewport_size.width()) * 0.5,
                            double(this->viewport_size.height()) * 0.5);
    MapGlobeCamera camera;
    camera.setViewportSize(this->viewport_size);
    camera.syncFromMapModel(*this->map_model);
    MapGlobeScreenRay ray;
    if (!camera.screenRay(crosshair, &ray))
        return false;
    // The same ray against the same drawn surface the pointer coordinate and
    // the pan use (surfaceRayHitAtScreen() builds this camera too).
    MapGlobeSurfaceHit hit;
    if (!surfaceRayHitAtScreen(crosshair, &hit) || !hit.isValid())
        return false;

    const bool anchored = this->map_model->setViewGlobeOrbitEyeAndTarget(ray.origin_ecef,
                                                                         hit.ecef_position);
    if (MapRenderDiagnostics::enabled())
        qInfo() << "AOWIS globe orbit: pivot at crosshair"
                << (hit.isTerrainMesh() ? "terrain" : "ellipsoid")
                << "lon" << hit.coordinate.longitude_deg << "lat" << hit.coordinate.latitude_deg
                << "height" << hit.surface_height_m << (anchored ? "anchored" : "kept");
    return anchored;
}

bool MapGlobeTerrainNavigation::releaseOrbitAnchor()
{
    if (this->map_model == nullptr || this->map_model->viewMode() != MapViewMode::Globe
        || this->map_model->globeEyePose() || !this->viewport_size.isValid())
    {
        return false;
    }

    const QPointF crosshair(double(this->viewport_size.width()) * 0.5,
                            double(this->viewport_size.height()) * 0.5);
    MapGlobeCamera camera;
    camera.setViewportSize(this->viewport_size);
    camera.syncFromMapModel(*this->map_model);
    MapGlobeScreenRay ray;
    if (!camera.screenRay(crosshair, &ray))
        return false;
    const GeoWgs84Ellipsoid::EcefPositionD eye = ray.origin_ecef;

    // Following's goal (syncCameraHeight()): the drawn DEM under the eye,
    // 0 while no DEM is cached there. Its small clearance term for very
    // close, flat views is left to the filter.
    CoordinateWGS84 eye_coordinate;
    double eye_height_m = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(eye, &eye_coordinate.longitude_deg,
                                           &eye_coordinate.latitude_deg, &eye_height_m))
    {
        return false;
    }
    double eye_terrain_elevation_m = 0.0;
    if (this->terrain_repository == nullptr
        || !cameraTerrainElevationAtCoordinate(eye_coordinate, &eye_terrain_elevation_m, false))
    {
        eye_terrain_elevation_m = 0.0;
    }
    const MapGlobeVerticalTransform vertical_transform(this->map_model->view3dVerticalExaggeration(),
                                                       this->map_model->view3dNetworkGroundOffsetM());
    const double goal_height_m = vertical_transform.terrainHeightM(eye_terrain_elevation_m);

    // The point of the crosshair ray at that height: between the eye (above
    // it) and the ellipsoid hit (height 0, below a positive goal). Heights at
    // or below sea level, or a ray past the limb, are left to the filter.
    const double direction_length = double(ray.direction.length());
    if (!(direction_length > 0.0) || !(goal_height_m > 0.0) || !(eye_height_m > goal_height_m))
        return false;
    const double direction_x = double(ray.direction.x()) / direction_length;
    const double direction_y = double(ray.direction.y()) / direction_length;
    const double direction_z = double(ray.direction.z()) / direction_length;
    GeoWgs84Ellipsoid::EcefPositionD ellipsoid_hit;
    double ellipsoid_distance_m = 0.0;
    if (!GeoWgs84Ellipsoid::rayIntersection(eye, ray.direction, &ellipsoid_hit, &ellipsoid_distance_m))
        return false;

    // Height along the ray falls monotonically from the eye to the ellipsoid
    // hit (the ray descends to it): bisect to well below a millimetre.
    double near_m = 0.0;
    double far_m = ellipsoid_distance_m;
    for (int iteration = 0; iteration < 64; ++iteration)
    {
        const double middle_m = 0.5 * (near_m + far_m);
        double lon_deg = 0.0;
        double lat_deg = 0.0;
        double height_m = 0.0;
        if (!GeoWgs84Ellipsoid::ecefToGeodetic(
                rayPoint(eye, direction_x, direction_y, direction_z, middle_m),
                &lon_deg, &lat_deg, &height_m))
        {
            return false;
        }
        if (height_m > goal_height_m)
            near_m = middle_m;
        else
            far_m = middle_m;
    }
    const GeoWgs84Ellipsoid::EcefPositionD target =
        rayPoint(eye, direction_x, direction_y, direction_z, 0.5 * (near_m + far_m));
    const bool anchored = this->map_model->setViewGlobeOrbitEyeAndTarget(eye, target);
    if (MapRenderDiagnostics::enabled())
        qInfo() << "AOWIS globe orbit: released at follow height" << goal_height_m
                << (anchored ? "re-anchored" : "kept");
    return anchored;
}
