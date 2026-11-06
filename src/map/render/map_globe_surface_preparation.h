#ifndef MAP_GLOBE_SURFACE_PREPARATION_H
#define MAP_GLOBE_SURFACE_PREPARATION_H

#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_surface_vertex.h"
#include "map/render/map_globe_surface_scene.h"
#include "map/render/map_globe_terrain_levels.h"

#include <aowis/model/gis.h>

#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <functional>
#include <memory>

class MapModel;
class MapTerrainTileSource;
class MapTerrainMeshScheduler;
struct MapTerrainMeshRequest;
struct MapTerrainTile;
enum class MapTerrainVerticalDatum;

struct MapGlobeSurfaceTerrainRayRowBounds
{
    QVector3D minimum;
    QVector3D maximum;
};

// Backend-neutral retained state for one visible Globe surface tile. This is
// CPU preparation state only: there are deliberately no texture handles,
// descriptor/binding objects, array-page assignments, or backend resource
// pointers here.
struct MapGlobeSurfaceTile
{
    int surface_tile_index = -1;
    int virtual_x = 0;
    int tile_x = 0;
    int tile_y = 0;
    int zoom = 0;
    bool is_cap = false;

    int first_vertex = 0;
    int vertex_count = 0;
    // Caps: the range in capIndices(). Window tiles share their topology
    // (mapGlobeTileIndices(grid_cell_count), relative to first_vertex):
    // first_index is 0 and index_count that list's size.
    int first_index = 0;
    int index_count = 0;
    // Cells per side of the tile's grid; vertex_count and index_count
    // include the skirt below its edges (mapGlobeTileVertexCount()).
    int grid_cell_count = 0;
    QString imagery_key;

    int terrain_zoom = -1;
    QString terrain_key;
    // mapGlobeTerrainCellCount(zoom, terrain_zoom): the DEM's resolution.
    int terrain_cell_count = 1;
    quint64 terrain_mesh_request_id = 0;
    bool terrain_mesh_applied = false;
    // MapTerrainMeshResult::missing_neighbor_mask of the applied own-DEM
    // mesh: neighbor DEM tiles whose arrival rebuilds it.
    int terrain_missing_neighbor_mask = 0;
    bool terrain_mesh_has_relief = false;
    // While the tile's own DEM is loading, its mesh is built from the
    // nearest cached coarser DEM tile (scheduleAncestorTerrainStandIns()):
    // the zoom of that tile, -1 when the mesh is flat or from the own DEM.
    int terrain_stand_in_zoom = -1;

    QVector3D terrain_ray_bounds_min;
    QVector3D terrain_ray_bounds_max;
    bool terrain_ray_bounds_valid = false;
    QVector<MapGlobeSurfaceTerrainRayRowBounds> terrain_ray_row_bounds;
};

struct MapGlobeSurfaceVertexPatch
{
    qsizetype first_vertex = 0;
    qsizetype vertex_count = 0;
};

// Owns all backend-independent preparation of the retained Globe surface:
// visible quadtree selection, ellipsoid/cap geometry, the DEM zoom of each
// tile (MapGlobeTerrainLevels), asynchronous terrain mesh
// generation/application, wireframe generation,
// and exact retained-DEM picking. Concrete GPU backends consume the prepared
// vertices/indices/tiles and attach their own resource state separately.
class MapGlobeSurfacePreparation
{
public:
    explicit MapGlobeSurfacePreparation(MapModel *map_model);
    ~MapGlobeSurfacePreparation();

    bool setTerrainRepository(MapTerrainTileSource *terrain_repository);
    MapTerrainTileSource *terrainRepository() const;
    // The user's terrain detail reduction (0..MapGlobeTerrainMaximumDetailReduction),
    // read whenever the terrain levels are refreshed; default
    // MapGlobeTerrainDefaultDetailReduction.
    void setTerrainDetailReductionSource(const std::function<int()> &source);

    bool requestTerrainForCurrentView(const QSize &viewport_size);
    void invalidateTerrainView();
    // Map provider switch: the window tiles' imagery keys name the provider
    // (MapModel::tileCacheKey()); they are computed when a tile enters the
    // window, so a switch would otherwise keep looking up (and uploading)
    // the old provider's cached images until the window is rebuilt.
    void refreshImageryKeys();
    bool setRenderOriginEcef(
        const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef);
    void notifyTerrainTileAvailable(const QString &key);
    void invalidateTerrain();

    bool setWireframeVisible(bool visible);
    bool wireframeVisible() const;

    bool ensureCapsBuilt();
    bool prepareVisibleWindow(const QSize &viewport_size);
    bool applyReadyTerrainMeshes(
        QVector<MapGlobeSurfaceVertexPatch> *vertex_patches);
    // The window vertex ranges written by window rebuilds since the last
    // call (new tiles in the persistent pool); full_upload: upload
    // windowVertices() whole instead (the pool was rebuilt or grew).
    void takeWindowVertexUploads(
        QVector<MapGlobeSurfaceVertexPatch> *patches, bool *full_upload);
    void requestMissingTerrainTiles();
    void scheduleReadyTerrainMeshes();

    bool hasPendingTerrainMeshes() const;
    void terrainMeshProgress(int *completed, int *total, bool *active) const;

    bool visibleTerrainRayIntersection(
        const GeoWgs84Ellipsoid::EcefPositionD &ray_origin_ecef,
        const QVector3D &ray_direction_ecef,
        GeoWgs84Ellipsoid::EcefPositionD *intersection_ecef,
        double *distance_m) const;
    // The drawn surface's height at a coordinate without a scan over all
    // tiles: the window tile covering it, then a vertical ray (from
    // start_height_m down the ellipsoid normal) against the two triangles of
    // its grid cell (and the neighbouring cells at an edge). Relief: height
    // set; Flat: no relief drawn there (the covering tile lies on the
    // ellipsoid, or no window tile covers the coordinate); Unknown: the
    // ray missed the cell and its neighbours (use
    // visibleTerrainRayIntersection()). Demand points are draped with this
    // after every terrain change, thousands of them per frame.
    enum class DrawnSurfaceHeight
    {
        Relief,
        Flat,
        Unknown
    };
    DrawnSurfaceHeight drawnSurfaceHeightAt(
        const CoordinateWGS84 &coordinate, double start_height_m, double *height_m) const;
    bool visibleTerrainSamplingAtCoordinate(
        const CoordinateWGS84 &coordinate,
        int *terrain_zoom,
        double *cell_size_m) const;

    // The terrain levels the current window was built with.
    const MapGlobeTerrainLevels &terrainLevels() const;
    const GeoWgs84Ellipsoid::EcefPositionD &renderOriginEcef() const;
    const QVector<MapGlobeSurfaceVertex> &windowVertices() const;
    const QVector<MapGlobeSurfaceTile> &windowTiles() const;
    QVector<MapGlobeSurfaceTile> &windowTiles();
    const QSet<quint64> &windowPositionKeys() const;
    bool windowDirty() const;

    const QVector<MapGlobeSurfaceVertex> &capVertices() const;
    const QVector<quint32> &capIndices() const;
    const QVector<MapGlobeSurfaceTile> &capTiles() const;
    QVector<MapGlobeSurfaceTile> &capTiles();

    const QVector<MapGlobeSurfaceWireframeVertex> &wireframeVertices() const;

private:
    MapGlobeSurfaceVertex makeTileVertex(
        double lon_deg, double lat_deg, float u, float v) const;
    void buildPolarCap(bool north);
    void rebuildWindow(const QVector<MapGlobeQuadtreeLeaf> &leaves);
    // A free range of vertex_count vertices in the pool (grows the pool when
    // none fits); its first vertex.
    int allocateWindowSlot(int vertex_count);
    void releaseWindowSlot(int first_vertex, int vertex_count);
    // Writes a new tile's grid and skirt into its pool range: its own DEM's
    // mesh, else a stand-in from the nearest cached coarser DEM, else (or
    // without build_mesh: over the frame's budget) flat for the mesh thread.
    void buildWindowTileVertices(MapGlobeSurfaceTile *tile, int subdivisions, bool build_mesh);
    // The finest cached, usable DEM tile above the tile's own DEM zoom and
    // finer than coarsest_excluded_zoom; nullptr when there is none.
    const MapTerrainTile *cachedAncestorTerrainTile(
        const MapGlobeSurfaceTile &tile, int coarsest_excluded_zoom) const;
    // Takes the current terrain levels (catalog and detail setting); true
    // when they changed, which invalidates every tile's terrain.
    bool refreshTerrainLevels();
    void updateTerrainRayBounds(MapGlobeSurfaceTile *tile);
    // Mesh request for a window tile built from terrain_tile (its own DEM or
    // a stand-in at its DEM address).
    MapTerrainMeshRequest terrainMeshRequest(
        const MapGlobeSurfaceTile &tile, const MapTerrainTile &terrain_tile) const;
    // Tiles whose own DEM is not cached yet get their mesh from the nearest
    // cached coarser DEM tile (again when a finer one arrives), built on the
    // mesh thread and applied like any mesh (applyReadyTerrainMeshes()), but
    // without finishing the tile: its own DEM's mesh replaces it.
    void scheduleAncestorTerrainStandIns();
    void rebuildWireframeVertices();

    MapModel *map_model = nullptr;
    MapTerrainTileSource *terrain_repository = nullptr;
    std::function<int()> terrain_detail_reduction_source;
    MapGlobeTerrainLevels terrain_levels;
    GeoWgs84Ellipsoid::EcefPositionD render_origin_ecef;

    // The window's vertex pool: every tile keeps its range (first_vertex)
    // while it stays in the window; window_vertices is the whole pool
    // (window_pool_end used, the rest spare capacity), ranges of dropped
    // tiles wait in window_free_slots by size for a new tile of that size.
    QVector<MapGlobeSurfaceVertex> window_vertices;
    QHash<int, QVector<int>> window_free_slots;
    qsizetype window_pool_end = 0;
    qsizetype window_free_vertex_count = 0;
    QVector<MapGlobeSurfaceVertexPatch> window_vertex_patches;
    bool window_full_upload = true;
    QVector<MapGlobeSurfaceTile> window_tiles;
    QSet<quint64> window_position_keys;
    QHash<quint64, qsizetype> window_tile_indices_by_position;
    bool window_dirty = true;
    MapGlobeSurfaceScene surface_scene;

    QVector<MapGlobeSurfaceWireframeVertex> wireframe_vertices;
    bool wireframe_visible = false;

    QVector<MapGlobeSurfaceVertex> cap_vertices;
    QVector<quint32> cap_indices;
    QVector<MapGlobeSurfaceTile> cap_tiles;
    bool caps_built = false;

    std::unique_ptr<MapTerrainMeshScheduler> terrain_mesh_scheduler;
    quint64 next_terrain_mesh_request_id = 1;
    bool reported_orthometric_datum_warning = false;
    bool reported_unusable_datum_warning = false;
};

int mapGlobeSurfaceTileRequestPriority(
    int tile_x, int tile_y, int zoom,
    double center_lon_deg, double center_lat_deg);
// The terrain levels for the source's active dataset (none until its
// catalog arrived) and the user's terrain detail reduction (clamped).
MapGlobeTerrainLevels mapGlobeTerrainLevels(const MapTerrainTileSource *terrain_repository,
                                            int detail_reduction);
// detail_reduction from source, MapGlobeTerrainDefaultDetailReduction without one.
int mapGlobeTerrainDetailReduction(const std::function<int()> &source);
bool mapGlobeTerrainDatumUsable(MapTerrainVerticalDatum datum);

#endif // MAP_GLOBE_SURFACE_PREPARATION_H
