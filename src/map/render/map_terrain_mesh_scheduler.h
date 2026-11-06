#ifndef MAP_TERRAIN_MESH_SCHEDULER_H
#define MAP_TERRAIN_MESH_SCHEDULER_H

#include "map/data/map_terrain_tile.h"
#include "map/render/map_globe_surface_vertex.h"
#include "map/render/map_globe_terrain_levels.h"
#include "map/render/map_globe_tile_topology.h"

#include <QMutex>
#include <QString>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <cmath>
#include <deque>

static_assert(MapTerrainTileCellCount == MapGlobeTileMaximumCellCount,
              "Terrain DEM cell count must match Render tile topology");

// One Globe terrain-mesh vertex. Results contain one row-major
// (cell_count + 1)^2 ECEF-relative vertex grid; the Globe renderer owns the
// shared deterministic index topology, so terrain updates only transfer the
// positions/UVs that can actually change.
struct MapTerrainMeshVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    MapGlobePackedNormal normal;
};

// The four DEM tiles beside a mesh's own DEM tile (same zoom), in this
// order: their samples next to the shared edge give the surface normals of
// the edge vertices the same central differences as everywhere else, so the
// hillshading has no seam along DEM tile borders.
enum MapTerrainMeshNeighbor
{
    MapTerrainMeshNeighborWest = 0,
    MapTerrainMeshNeighborEast = 1,
    MapTerrainMeshNeighborNorth = 2,
    MapTerrainMeshNeighborSouth = 3,
    MapTerrainMeshNeighborCount = 4
};

// Everything needed to build one Globe terrain tile, carried entirely by
// value so the background thread never touches a live main-thread-owned
// object (MapTerrainTileSource, MapModel, or any GPU resource).
//
// terrain_tile is a value copy of the cached DEM tile. Copying it on the main
// thread before submitting is safe per Qt's implicit-sharing/copy-on-write
// thread-safety guarantee.
struct MapTerrainMeshRequest
{
    quint64 request_id = 0;
    QString terrain_key;
    MapTerrainTile terrain_tile;
    bool terrain_available = false;
    // MapTerrainMeshNeighbor order; a neighbor without samples is not cached
    // (its edge normals are one-sided then). Only for the tile's own DEM.
    MapTerrainTile terrain_neighbors[MapTerrainMeshNeighborCount];
    // While the tile's own DEM is loading: terrain_tile is the cached coarser
    // DEM tile at this zoom, resampled at terrain_zoom by
    // buildTerrainMeshResult() (mapTerrainTileFromAncestor()); -1: the
    // tile's own DEM.
    int stand_in_zoom = -1;

    // Identifies the rendered imagery tile. virtual_x remains unwrapped so
    // tiles on opposite sides of the antimeridian stay distinct even when
    // tile_x wraps to the same dataset address.
    int virtual_x = 0;
    int tile_x = 0;
    int y = 0;
    int imagery_zoom = 0;
    int terrain_zoom = 0;

    // Generate geodetic WGS84/ECEF positions relative to the same sticky ECEF
    // render origin used by the Globe camera/network. The subtraction happens
    // in double precision before narrowing into float vertices.
    double globe_vertical_exaggeration = 1.0;
    double globe_render_origin_x = 0.0;
    double globe_render_origin_y = 0.0;
    double globe_render_origin_z = 0.0;
};

struct MapTerrainMeshResult
{
    quint64 request_id = 0;
    QString terrain_key;
    int virtual_x = 0;
    int y = 0;
    bool terrain_available = false;
    // The request's stand_in_zoom.
    int stand_in_zoom = -1;
    // mapGlobeTerrainCellCount(imagery_zoom, terrain_zoom): the grid always
    // has exactly the DEM resolution, every vertex on a DEM sample.
    int cell_count = 0;
    // Bit (1 << MapTerrainMeshNeighbor) per neighbor DEM tile the edge
    // normals needed but the request did not carry (not cached yet); a
    // neighbor beyond the map's edge is never missing. The mesh is built
    // again when one of them arrives.
    int missing_neighbor_mask = 0;
    QVector<MapTerrainMeshVertex> vertices;
};

// Builds one Globe terrain vertex grid and its skirt. Pure and thread-safe.
MapTerrainMeshResult buildTerrainMeshResult(const MapTerrainMeshRequest &request);

// Stand-in DEM tile at `address`, bilinearly resampled from the matching
// sub-rectangle of a cached coarser `ancestor` tile (same dataset, lower
// zoom, covering address). Used while the tile's own DEM is still loading:
// a mesh built from it has the same grid as the final one (so the real mesh
// later replaces it in place) and lies at the right height instead of on
// the ellipsoid. Pure and thread-safe.
MapTerrainTile mapTerrainTileFromAncestor(
    const MapTerrainTile &ancestor, const MapTerrainTileAddress &address);

// Runs buildTerrainMeshResult() on a dedicated background thread so DEM
// resampling for newly-needed Globe terrain never blocks the render thread.
class MapTerrainMeshScheduler : public QThread
{
public:
    MapTerrainMeshScheduler();
    ~MapTerrainMeshScheduler() override;

    void submit(const MapTerrainMeshRequest &request);
    void collectReady(QVector<MapTerrainMeshResult> *results);

protected:
    void run() override;

private:
    QMutex mutex;
    QWaitCondition wait_condition;
    std::deque<MapTerrainMeshRequest> pending_requests;
    std::deque<MapTerrainMeshResult> completed_results;
    bool shutting_down = false;
};

#endif // MAP_TERRAIN_MESH_SCHEDULER_H
