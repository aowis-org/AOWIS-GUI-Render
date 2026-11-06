#ifndef MAP_GLOBE_SURFACE_SCENE_H
#define MAP_GLOBE_SURFACE_SCENE_H

#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_terrain_levels.h"

#include <QSet>
#include <QSize>
#include <QVector>
#include <QVector3D>
#include <QtGlobal>

#include <cmath>

class MapModel;
class MapTerrainTileSource;

// Lowest foreshortening weight of a node's projected size
// (mapGlobeQuadtreeNodeForeshortening()): at most three quadtree levels
// coarser than the same node seen head-on.
inline constexpr double MapGlobeQuadtreeMinimumForeshortening = 0.125;

// Weight (0..1] of a node's projected size for the refinement decision:
// how far the node is foreshortened as seen from the eye.
//
// The quadtree measures a node by the projected size of its bounding
// sphere, the same from every direction, so a node seen at a grazing angle
// towards the horizon was refined as if seen head-on: its cells came out several pixels wide but a
// fraction of a pixel tall (the moiré in the wireframe, most of the
// triangles of a tilted view). On screen such a node is foreshortened along
// the view by the cosine between its surface normal and the direction to the
// eye; the weight is the square root of that cosine, the geometric mean of
// the unchanged width and the shortened height, so neither the imagery's
// horizontal sharpness nor the mesh's vertical density goes far off. The
// node then becomes a coarser imagery leaf, which draws a coarser DEM tile
// at its full resolution (MapGlobeTerrainLevels): the terrain follows.
//
// The normal is the geocentric direction of the node's centre (within a
// fraction of a degree of the ellipsoid normal; relief is ignored). A node
// whose sphere comes close to the eye spans many viewing angles, so the
// centre's angle says little there: within two radii the weight is 1, and
// it blends to the centre's angle by four radii. The weight never drops
// below MapGlobeQuadtreeMinimumForeshortening.
inline double mapGlobeQuadtreeNodeForeshortening(
    const QVector3D &node_center,
    double node_radius_m,
    const QVector3D &eye)
{
    const QVector3D to_eye = eye - node_center;
    const double distance_m = double(to_eye.length());
    const double center_length_m = double(node_center.length());
    if (distance_m <= 1e-6 || center_length_m <= 1e-6 || node_radius_m <= 0.0)
        return 1.0;

    const double facing = qBound(
        0.0, double(QVector3D::dotProduct(to_eye, node_center)) / (distance_m * center_length_m), 1.0);
    const double weight = qMax(MapGlobeQuadtreeMinimumForeshortening, std::sqrt(facing));
    const double angle_validity = qBound(0.0, (distance_m / node_radius_m - 2.0) * 0.5, 1.0);
    return 1.0 + (weight - 1.0) * angle_validity;
}

struct MapGlobeQuadtreeLeaf
{
    int zoom = 0;
    int tile_x = 0;
    int tile_y = 0;
};

// Backend-neutral visible-surface decision layer. It owns the quadtree
// hysteresis that persists between frames and the policy for which imagery
// leaves are visible. The terrain level of detail follows from it: each
// leaf draws the DEM zoom MapGlobeTerrainLevels assigns to its imagery zoom,
// at that DEM's full resolution (mapGlobeTerrainCellCount()). No GPU
// resources or renderer-backend types live here.
class MapGlobeSurfaceScene
{
public:
    static constexpr int MaximumLeafCount = 3000;
    static constexpr int TerrainReliefMinimumZoom = MapGlobeTerrainReliefMinimumZoom;

    // terrain_levels: which DEM tiles bound the leaves' relief (only read
    // when terrain_repository is set).
    QVector<MapGlobeQuadtreeLeaf> selectVisibleLeaves(
        const MapModel &map_model,
        const QSize &viewport_size,
        const MapTerrainTileSource *terrain_repository,
        const MapGlobeTerrainLevels &terrain_levels);

    void clearVisibilityHistory();

    static quint64 positionKey(int zoom, int tile_x, int tile_y);

private:
    QSet<quint64> previously_subdivided_quadtree_nodes;
};

#endif // MAP_GLOBE_SURFACE_SCENE_H
