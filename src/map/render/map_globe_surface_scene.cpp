#include "map/render/map_globe_surface_scene.h"

#include "geo/geo_web_mercator.h"
#include "map/core/map_model.h"
#include "map/data/map_terrain_tile_source.h"
#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_vertical_transform.h"

#include <QHash>
#include <QString>
#include <QVector3D>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr int GlobeQuadtreeRootZoom = 0;
constexpr double GlobeQuadtreeSubdivideScreenPx = 512.0;
constexpr double GlobeQuadtreeMergeScreenPx = 256.0;
constexpr int GlobeQuadtreeMaxVisitedNodes = 20000;
// Slack around the exact view cone (quadtreeNodeInViewCone()): the leaves
// are selected again every frame, so this only prefetches tiles about to
// rotate into view. It was 0.5 while the cone ignored the aspect ratio;
// every extra leaf costs a mesh, imagery and atlas space.
constexpr double GlobeQuadtreeViewConeMarginRad = 0.1;
constexpr double GlobeQuadtreeHorizonOcclusionMarginFactor = 3.0;
constexpr int GlobeQuadtreeViewConeCullMinZoom = 2;
// Relief assumed where no DEM covering a node is cached yet: from the
// ellipsoid to above the highest summit (raw DEM metres).
constexpr double UnknownTerrainMaximumElevationM = 9000.0;

bool terrainDatumUsable(MapTerrainVerticalDatum datum)
{
    return datum == MapTerrainVerticalDatum::Wgs84Ellipsoid
        || datum == MapTerrainVerticalDatum::Egm96
        || datum == MapTerrainVerticalDatum::Egm2008;
}

// Closed-form WGS84 geodetic -> ECEF, the same formula GeographicLib
// evaluates, without its exact-degree trigonometry (sub-millimetre apart):
// the bounding spheres need ten conversions per visited node, every frame.
QVector3D boundsEcef(double lon_deg, double lat_deg, double height_m)
{
    constexpr double SemiMajorM = GeoWgs84Ellipsoid::EquatorialRadiusM;
    constexpr double Flattening = 1.0 / GeoWgs84Ellipsoid::FlatteningInverse;
    constexpr double EccentricitySquared = Flattening * (2.0 - Flattening);
    const double lat_rad = qDegreesToRadians(lat_deg);
    const double lon_rad = qDegreesToRadians(lon_deg);
    const double sin_lat = std::sin(lat_rad);
    const double cos_lat = std::cos(lat_rad);
    const double prime_vertical_m = SemiMajorM / std::sqrt(1.0 - EccentricitySquared * sin_lat * sin_lat);
    const double horizontal_m = (prime_vertical_m + height_m) * cos_lat;
    return QVector3D(float(horizontal_m * std::cos(lon_rad)), float(horizontal_m * std::sin(lon_rad)),
                     float((prime_vertical_m * (1.0 - EccentricitySquared) + height_m) * sin_lat));
}

void quadtreeNodeBoundingSphere(
    int zoom,
    int tile_x,
    int tile_y,
    double reference_elevation_m,
    QVector3D *center,
    double *radius_m)
{
    if (center == nullptr || radius_m == nullptr)
        return;

    if (zoom <= 1)
    {
        *center = QVector3D(0.0f, 0.0f, 0.0f);
        *radius_m = GeoWgs84Ellipsoid::EquatorialRadiusM
            + qMax(0.0, reference_elevation_m);
        return;
    }

    const double lon0 = GeoWebMercator::tileXToLon(double(tile_x), zoom);
    const double lon1 = GeoWebMercator::tileXToLon(double(tile_x + 1), zoom);
    const double lat0 = GeoWebMercator::tileYToLat(double(tile_y), zoom);
    const double lat1 = GeoWebMercator::tileYToLat(double(tile_y + 1), zoom);
    const double lon_mid = 0.5 * (lon0 + lon1);
    const double lat_mid = 0.5 * (lat0 + lat1);

    const QVector3D samples[5] = {
        boundsEcef(lon0, lat0, reference_elevation_m),
        boundsEcef(lon1, lat0, reference_elevation_m),
        boundsEcef(lon0, lat1, reference_elevation_m),
        boundsEcef(lon1, lat1, reference_elevation_m),
        boundsEcef(lon_mid, lat_mid, reference_elevation_m),
    };

    constexpr int SampleCount = int(sizeof(samples) / sizeof(samples[0]));
    QVector3D centroid(0.0f, 0.0f, 0.0f);
    for (const QVector3D &sample : samples)
        centroid += sample;
    centroid /= float(SampleCount);

    double max_distance = 0.0;
    for (const QVector3D &sample : samples)
        max_distance = qMax(max_distance, double((sample - centroid).length()));

    *center = centroid;
    *radius_m = max_distance;
}

// DEM tiles looked up during one leaf selection, by position. A repository
// lookup builds a string key; the selection asks for the same tiles over and
// over (nodes beyond the DEM zoom share their terrain tile, every uncached
// node walks the same ancestors), which cost up to 16 ms per frame.
class TerrainTileLookup
{
public:
    TerrainTileLookup(const MapTerrainTileSource *terrain_repository,
                      const MapGlobeTerrainLevels &terrain_levels)
        : terrain_repository(terrain_repository),
          terrain_levels(terrain_levels)
    {
    }

    const MapGlobeTerrainLevels &levels() const
    {
        return this->terrain_levels;
    }

    const MapTerrainTile *tile(int zoom, quint32 x, quint32 y)
    {
        const quint64 key = MapGlobeSurfaceScene::positionKey(zoom, int(x), int(y));
        QHash<quint64, const MapTerrainTile *>::const_iterator found = this->tiles.constFind(key);
        if (found == this->tiles.cend())
        {
            found = this->tiles.insert(
                key, this->terrain_repository->tile(this->terrain_levels.dataset, zoom, x, y));
        }
        return found.value();
    }

private:
    const MapTerrainTileSource *terrain_repository = nullptr;
    const MapGlobeTerrainLevels &terrain_levels;
    QHash<quint64, const MapTerrainTile *> tiles;
};

// Elevation range (raw DEM metres) of the finest cached DEM tile covering a
// node: its own terrain tile, else the nearest cached ancestor, whose range
// contains that of every descendant. False when none is cached.
bool cachedTerrainElevationRange(
    int zoom,
    int tile_x,
    int tile_y,
    TerrainTileLookup *terrain_tiles,
    double *minimum_elevation_m,
    double *maximum_elevation_m)
{
    const int terrain_zoom = terrain_tiles->levels().terrainZoomForImageryZoom(zoom);
    for (int candidate_zoom = terrain_zoom;
         candidate_zoom >= terrain_tiles->levels().minimum_zoom;
         --candidate_zoom)
    {
        const int shift = zoom - candidate_zoom;
        const MapTerrainTile *terrain_tile = terrain_tiles->tile(
            candidate_zoom, quint32(tile_x) >> shift, quint32(tile_y) >> shift);
        if (terrain_tile != nullptr
            && terrainDatumUsable(terrain_tile->vertical_datum)
            && std::isfinite(terrain_tile->minimum_elevation_m)
            && std::isfinite(terrain_tile->maximum_elevation_m))
        {
            *minimum_elevation_m = terrain_tile->minimum_elevation_m;
            *maximum_elevation_m = terrain_tile->maximum_elevation_m;
            return true;
        }
    }
    return false;
}

// Bounding sphere for horizon and view-cone culling: the node's surface
// patch between the lowest and highest relief that can be drawn there.
//
// The relief range must hold before the node's own DEM tile is cached. A
// node bounded on the ellipsoid under high terrain lies kilometres below
// the drawn surface; close to the camera it then left the view cone, was
// never refined into leaves, so its DEM was never requested either: a
// permanent hole next to the camera. Hence the nearest cached ancestor's
// range, and without any cached DEM the whole possible range.
// terrain_tiles: nullptr without a terrain repository or terrain dataset (no
// relief drawn).
void quadtreeNodeVisibilityBoundingSphere(
    int zoom,
    int tile_x,
    int tile_y,
    TerrainTileLookup *terrain_tiles,
    double vertical_exaggeration,
    QVector3D *center,
    double *radius_m)
{
    const MapGlobeVerticalTransform vertical_transform(vertical_exaggeration);
    double minimum_elevation_m = 0.0;
    double maximum_elevation_m = 0.0;

    if (terrain_tiles != nullptr
        && terrain_tiles->levels().reliefAtImageryZoom(zoom))
    {
        double dem_minimum_m = 0.0;
        double dem_maximum_m = UnknownTerrainMaximumElevationM;
        cachedTerrainElevationRange(
            zoom, tile_x, tile_y, terrain_tiles, &dem_minimum_m, &dem_maximum_m);
        const double first_elevation_m = vertical_transform.terrainHeightM(dem_minimum_m);
        const double second_elevation_m = vertical_transform.terrainHeightM(dem_maximum_m);
        // The flat ellipsoid stays inside: drawn where relief is not
        // available at all (no DEM anywhere above the node).
        minimum_elevation_m = qMin(
            0.0, qMin(first_elevation_m, second_elevation_m));
        maximum_elevation_m = qMax(
            0.0, qMax(first_elevation_m, second_elevation_m));
    }

    const double reference_elevation_m =
        0.5 * (minimum_elevation_m + maximum_elevation_m);
    quadtreeNodeBoundingSphere(
        zoom, tile_x, tile_y, reference_elevation_m, center, radius_m);
    *radius_m += 0.5 * (maximum_elevation_m - minimum_elevation_m);
}

bool quadtreeNodeOccludedByHorizon(
    const QVector3D &node_center,
    double node_radius_m,
    const QVector3D &eye)
{
    const QVector3D to_node = node_center - eye;
    const double distance_to_node = double(to_node.length());
    if (distance_to_node <= qMax(1.0, node_radius_m))
        return false;

    QVector3D direction = to_node;
    direction.normalize();
    QVector3D intersection;
    if (!GeoWgs84Ellipsoid::rayIntersection(eye, direction, &intersection))
        return false;

    const double distance_to_surface = double((intersection - eye).length());
    return distance_to_surface
        < distance_to_node
            - qMax(1.0, node_radius_m * GlobeQuadtreeHorizonOcclusionMarginFactor);
}

// view_cone_half_angle_rad: the frustum's half-angle around the view axis
// at its corners (the vertical half FOV widened by the aspect ratio). A
// sphere meets the cone when its centre direction is off the axis by at most
// that half-angle plus its own angular radius asin(r / d), also when the
// centre lies behind the eye (a large node can reach into a frustum corner
// from there).
bool quadtreeNodeInViewCone(
    const QVector3D &node_center,
    double node_radius_m,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &camera_basis,
    double view_cone_half_angle_rad)
{
    const QVector3D to_node = node_center - camera_basis.eye;
    const double distance = double(to_node.length());
    if (distance <= node_radius_m)
        return true;

    const double forward_component = double(
        QVector3D::dotProduct(to_node, camera_basis.forward));
    const double angular_radius_rad = std::asin(node_radius_m / distance);
    const double view_angle_rad = std::acos(
        qBound(-1.0, forward_component / distance, 1.0));
    return view_angle_rad
        <= view_cone_half_angle_rad + angular_radius_rad + GlobeQuadtreeViewConeMarginRad;
}

double quadtreeNodeProjectedSizePx(
    const QVector3D &node_center,
    double node_radius_m,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &camera_basis,
    double viewport_height_px,
    double tan_half_fov)
{
    const double center_distance_m =
        double((node_center - camera_basis.eye).length());
    if (center_distance_m <= node_radius_m + 1e-6)
        return std::numeric_limits<double>::infinity();

    const double tangent_distance_m = std::sqrt(qMax(
        1e-12,
        center_distance_m * center_distance_m - node_radius_m * node_radius_m));
    const double projected_diameter = 2.0 * node_radius_m / tangent_distance_m;
    return projected_diameter * (viewport_height_px / (2.0 * tan_half_fov));
}

struct QuadtreeChild
{
    int x = 0;
    int y = 0;
    double distance_sq = 0.0;
    QVector3D visibility_center;
    double visibility_radius_m = 0.0;
};

// visibility_node_center / _radius_m: the node's
// quadtreeNodeVisibilityBoundingSphere(), computed by the caller (the parent
// needs it anyway to order its children).
void collectQuadtreeLeaves(
    int zoom,
    int tile_x,
    int tile_y,
    const QVector3D &visibility_node_center,
    double visibility_node_radius_m,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &visibility_camera_basis,
    const GeoWgs84Ellipsoid::OrbitCameraBasis &lod_camera_basis,
    TerrainTileLookup *terrain_tiles,
    double vertical_exaggeration,
    double viewport_height_px,
    double tan_half_fov,
    double view_cone_half_angle_rad,
    const QSet<quint64> &previously_subdivided_nodes,
    QSet<quint64> *currently_subdivided_nodes,
    QVector<MapGlobeQuadtreeLeaf> *leaves,
    int *visit_budget)
{
    if (leaves == nullptr || visit_budget == nullptr
        || *visit_budget <= 0
        || leaves->size() >= MapGlobeSurfaceScene::MaximumLeafCount)
    {
        return;
    }
    --(*visit_budget);

    if (quadtreeNodeOccludedByHorizon(
            visibility_node_center,
            visibility_node_radius_m,
            visibility_camera_basis.eye))
    {
        return;
    }
    if (zoom >= GlobeQuadtreeViewConeCullMinZoom
        && !quadtreeNodeInViewCone(
            visibility_node_center,
            visibility_node_radius_m,
            visibility_camera_basis,
            view_cone_half_angle_rad))
    {
        return;
    }

    QVector3D lod_node_center;
    double lod_node_radius_m = 0.0;
    quadtreeNodeBoundingSphere(
        zoom, tile_x, tile_y, 0.0, &lod_node_center, &lod_node_radius_m);
    const double projected_size_px = quadtreeNodeProjectedSizePx(
        lod_node_center,
        lod_node_radius_m,
        lod_camera_basis,
        viewport_height_px,
        tan_half_fov)
        * mapGlobeQuadtreeNodeForeshortening(lod_node_center, lod_node_radius_m, lod_camera_basis.eye);
    const bool was_subdivided = previously_subdivided_nodes.contains(
        MapGlobeSurfaceScene::positionKey(zoom, tile_x, tile_y));
    const double subdivide_threshold_px = was_subdivided
        ? GlobeQuadtreeMergeScreenPx
        : GlobeQuadtreeSubdivideScreenPx;
    const bool can_subdivide = zoom < MapModel::MaxZoom;

    if (!can_subdivide || projected_size_px <= subdivide_threshold_px)
    {
        leaves->append(MapGlobeQuadtreeLeaf{zoom, tile_x, tile_y});
        return;
    }

    if (currently_subdivided_nodes != nullptr)
    {
        currently_subdivided_nodes->insert(
            MapGlobeSurfaceScene::positionKey(zoom, tile_x, tile_y));
    }

    const int child_zoom = zoom + 1;
    const int child_tile_span = 1 << child_zoom;
    const int child_x = tile_x * 2;
    const int child_y = tile_y * 2;
    QuadtreeChild children[4];
    int child_count = 0;
    for (int dx = 0; dx < 2; ++dx)
    {
        for (int dy = 0; dy < 2; ++dy)
        {
            const int cy = child_y + dy;
            if (cy < 0 || cy >= child_tile_span)
                continue;

            QVector3D child_center;
            double child_radius_m = 0.0;
            quadtreeNodeVisibilityBoundingSphere(
                child_zoom,
                child_x + dx,
                cy,
                terrain_tiles,
                vertical_exaggeration,
                &child_center,
                &child_radius_m);
            children[child_count] = QuadtreeChild{
                child_x + dx,
                cy,
                double((child_center - visibility_camera_basis.eye).lengthSquared()),
                child_center,
                child_radius_m};
            ++child_count;
        }
    }

    std::sort(
        children,
        children + child_count,
        [](const QuadtreeChild &first, const QuadtreeChild &second)
        {
            return first.distance_sq < second.distance_sq;
        });

    for (int index = 0; index < child_count; ++index)
    {
        collectQuadtreeLeaves(
            child_zoom,
            children[index].x,
            children[index].y,
            children[index].visibility_center,
            children[index].visibility_radius_m,
            visibility_camera_basis,
            lod_camera_basis,
            terrain_tiles,
            vertical_exaggeration,
            viewport_height_px,
            tan_half_fov,
            view_cone_half_angle_rad,
            previously_subdivided_nodes,
            currently_subdivided_nodes,
            leaves,
            visit_budget);
    }
}
}

QVector<MapGlobeQuadtreeLeaf> MapGlobeSurfaceScene::selectVisibleLeaves(
    const MapModel &map_model,
    const QSize &viewport_size,
    const MapTerrainTileSource *terrain_repository,
    const MapGlobeTerrainLevels &terrain_levels)
{
    QVector<MapGlobeQuadtreeLeaf> leaves;
    if (!viewport_size.isValid())
        return leaves;

    const double pitch_deg = qBound(
        MapModel::MinViewGlobeFreeLookPitchDeg,
        map_model.viewGlobePitchDeg(),
        MapModel::MaxViewGlobePitchDeg);
    const double distance_m = qMax(
        MapModel::MinViewGlobeDistanceM,
        map_model.viewGlobeDistanceM());

    GeoWgs84Ellipsoid::OrbitCameraBasis visibility_camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            map_model.centerLon(),
            map_model.centerLat(),
            map_model.viewGlobeYawDeg(),
            pitch_deg,
            distance_m,
            map_model.viewGlobeVerticalOffsetM(),
            map_model.viewGlobeCameraCollisionLiftM());

    GeoWgs84Ellipsoid::OrbitCameraBasis lod_camera_basis =
        GeoWgs84Ellipsoid::orbitCameraBasis(
            map_model.centerLon(),
            map_model.centerLat(),
            map_model.viewGlobeYawDeg(),
            pitch_deg,
            distance_m);
    if (map_model.globeEyePose())
    {
        const MapGlobeEyePose &pose = *map_model.globeEyePose();
        visibility_camera_basis.eye = QVector3D(float(pose.eye.x), float(pose.eye.y), float(pose.eye.z));
        visibility_camera_basis.forward = pose.orientation.rotatedVector(QVector3D(0, 0, -1));
        visibility_camera_basis.right = pose.orientation.rotatedVector(QVector3D(1, 0, 0));
        visibility_camera_basis.up = pose.orientation.rotatedVector(QVector3D(0, 1, 0));
        lod_camera_basis = visibility_camera_basis;
    }
    const double viewport_height_px = double(qMax(1, viewport_size.height()));
    const double half_fov_rad = qDegreesToRadians(
        map_model.globeFieldOfViewDeg() * 0.5);
    const double tan_half_fov = std::tan(half_fov_rad);
    // The view cone must contain the whole frustum, i.e. its corners. The
    // vertical half FOV alone culled visible terrain at the left and right
    // edges of wide viewports (21:9, a wide map pane: at 3:1 the side edges
    // are 51 degrees off axis, the corners 53).
    const double aspect = double(qMax(1, viewport_size.width())) / viewport_height_px;
    const double view_cone_half_angle_rad = std::atan(tan_half_fov * std::hypot(1.0, aspect));

    QSet<quint64> currently_subdivided_nodes;
    int visit_budget = GlobeQuadtreeMaxVisitedNodes;
    TerrainTileLookup terrain_tile_lookup(terrain_repository, terrain_levels);
    TerrainTileLookup *terrain_tiles = terrain_repository != nullptr && terrain_levels.enabled()
        ? &terrain_tile_lookup
        : nullptr;
    QVector3D root_center;
    double root_radius_m = 0.0;
    quadtreeNodeVisibilityBoundingSphere(
        GlobeQuadtreeRootZoom, 0, 0, terrain_tiles, map_model.view3dVerticalExaggeration(),
        &root_center, &root_radius_m);
    collectQuadtreeLeaves(
        GlobeQuadtreeRootZoom,
        0,
        0,
        root_center,
        root_radius_m,
        visibility_camera_basis,
        lod_camera_basis,
        terrain_tiles,
        map_model.view3dVerticalExaggeration(),
        viewport_height_px,
        tan_half_fov,
        view_cone_half_angle_rad,
        this->previously_subdivided_quadtree_nodes,
        &currently_subdivided_nodes,
        &leaves,
        &visit_budget);

    this->previously_subdivided_quadtree_nodes =
        std::move(currently_subdivided_nodes);

    if (leaves.isEmpty())
    {
        const int fallback_zoom = qBound(
            0,
            int(std::lround(MapModel::viewGlobeZoomLevelForDistanceM(
                qMax(1.0, map_model.viewGlobeDistanceM()),
                map_model.centerLat(),
                int(viewport_height_px)))),
            MapModel::MaxZoom);
        const int fallback_tile_span = 1 << fallback_zoom;
        const int fallback_x = qBound(
            0,
            int(std::floor(GeoWebMercator::lonToTileX(
                GeoWebMercator::normalizeLongitude(map_model.centerLon()),
                fallback_zoom))),
            fallback_tile_span - 1);
        const int fallback_y = qBound(
            0,
            int(std::floor(GeoWebMercator::latToTileY(
                map_model.centerLat(), fallback_zoom))),
            fallback_tile_span - 1);
        leaves.append(MapGlobeQuadtreeLeaf{
            fallback_zoom, fallback_x, fallback_y});
    }

    return leaves;
}

void MapGlobeSurfaceScene::clearVisibilityHistory()
{
    this->previously_subdivided_quadtree_nodes.clear();
}

quint64 MapGlobeSurfaceScene::positionKey(int zoom, int tile_x, int tile_y)
{
    return (quint64(quint32(zoom)) << 48)
        | (quint64(quint32(tile_x)) << 24)
        | quint64(quint32(tile_y));
}
