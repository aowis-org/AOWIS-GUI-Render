#ifndef MAP_GLOBE_TERRAIN_NAVIGATION_H
#define MAP_GLOBE_TERRAIN_NAVIGATION_H

#include "map/render/map_globe_surface_hit.h"

#include <QElapsedTimer>

#include <functional>
#include <QPointF>
#include <QSize>

class MapGlobeSurfacePreparation;
class MapModel;
class MapTerrainTileSource;

// Terrain-aware Globe navigation, backend-neutral (roadmap Phase 3, slice 2):
//
// - the camera height: the orbit rig follows the DEM perpendicularly beneath
//   the eye (MapModel's vertical offset) and, while rotating around a frozen
//   pivot, an eye-only collision lift keeps the minimum ground clearance.
//   Both approach their targets with a monotonic, frame-rate-independent
//   exponential (no overshoot at ridges or valleys);
// - screen rays against the rendered surface: the retained DEM meshes of
//   MapGlobeSurfacePreparation (the exact triangles on screen), the WGS84
//   ellipsoid where no mesh is loaded;
// - terrain-anchored panning: the drawn surface at the crosshair sets the
//   sphere MapModel::panByPixelsGlobe() turns the globe on, so the ground
//   under the crosshair follows the pointer;
// - the pointer coordinate with its DEM altitude, and cached DEM elevations
//   at the LOD the surface shows (2D footer, X-Ray classification later).
//
// Ported from the removed QRhi map widget (same constants, same sampling,
// same filter). The owner keeps it fed: setSurfacePreparation() with the globe it
// draws, setViewportSize() with the logical size of the map, and calls
// syncCameraHeight() on camera changes and on every frame while following().
class MapGlobeTerrainNavigation
{
public:
    explicit MapGlobeTerrainNavigation(MapModel *map_model);

    void setTerrainRepository(MapTerrainTileSource *terrain_repository);
    // Same setting as MapGlobeSurfacePreparation::setTerrainDetailReductionSource().
    void setTerrainDetailReductionSource(const std::function<int()> &source);
    // The rendered globe whose retained DEM meshes answer ray hits and the
    // visible terrain LOD; nullptr: ellipsoid and repository tiles only.
    void setSurfacePreparation(const MapGlobeSurfacePreparation *surface_preparation);
    // Logical (device-independent) size of the map, as MapWidget's size().
    void setViewportSize(const QSize &viewport_size);

    // DEM elevation at coordinate from cached tiles, preferring the LOD the
    // surface shows there; cell_size_m is that LOD's sampling interval.
    bool terrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                      double *elevation_m, double *cell_size_m) const;
    bool cachedTerrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                            double *elevation_m) const;
    // Height above the ellipsoid of the terrain surface as drawn (the
    // retained DEM meshes of the window, vertical exaggeration applied) at
    // coordinate; false (height 0) where no relief mesh is drawn, i.e. the
    // surface is the ellipsoid. Drapes the demand points on the terrain.
    bool drawnSurfaceHeightAtCoordinate(const CoordinateWGS84 &coordinate, double *height_m) const;
    // Ray from the screen position (logical pixels, top-left origin) against
    // the rendered DEM meshes, else the ellipsoid. false: the ray misses.
    bool surfaceRayHitAtScreen(const QPointF &screen_position, MapGlobeSurfaceHit *hit) const;
    // Pointer coordinate: Globe -> surface ray (altitude from the DEM when a
    // mesh was hit); 2D -> the flat map's coordinate with a cached altitude.
    bool terrainCoordinateAtScreen(const QPointF &screen_position, CoordinateWGS84 *coordinate);

    // One step of the camera-height filter (vertical offset and collision
    // lift) towards the DEM under the eye. request_missing_tile also
    // requests the exact DEM tile of the current LOD under the eye (camera
    // changes do; the per-frame continuation does not). Outside Globe mode
    // this only stops the follow.
    void syncCameraHeight(bool request_missing_tile);
    // Still settling: call syncCameraHeight(false) again on the next frame.
    bool following() const { return this->follow_active; }
    // Ends a running follow; the next step starts with a fresh time step
    // (mode switch, start or end of an orbit).
    void stopFollowing();

private:
    // DEM under the eye at the LOD the camera distance selects (requests it
    // when asked), coarser cached levels while it loads.
    bool cameraTerrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                            double *elevation_m, bool request_missing_tile) const;

    MapModel *map_model = nullptr;
    MapTerrainTileSource *terrain_repository = nullptr;
    std::function<int()> terrain_detail_reduction_source;
    const MapGlobeSurfacePreparation *surface_preparation = nullptr;
    QSize viewport_size;
    // Real time between filter steps (frames, camera changes).
    QElapsedTimer follow_clock;
    bool follow_active = false;
    // setViewGlobeTerrainHeightOffsetsM() can re-enter through MapModel's
    // signals; one step at a time.
    bool sync_active = false;
};

#endif // MAP_GLOBE_TERRAIN_NAVIGATION_H
