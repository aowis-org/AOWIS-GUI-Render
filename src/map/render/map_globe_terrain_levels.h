#ifndef MAP_GLOBE_TERRAIN_LEVELS_H
#define MAP_GLOBE_TERRAIN_LEVELS_H

#include "map/data/map_terrain_tile.h"

#include <QString>
#include <QtGlobal>

// Lowest imagery/DEM zoom at which the Globe draws relief at all; coarser
// tiles lie on the ellipsoid.
inline constexpr int MapGlobeTerrainReliefMinimumZoom = 8;

// Coarsest DEM level reduction offered by the "terrain detail reduction"
// setting: each level halves the terrain resolution below the imagery.
inline constexpr int MapGlobeTerrainMaximumDetailReduction = 3;
// The setting's default (one DEM level below the imagery, ~8 px per cell);
// used when the host injects no detail-reduction source.
inline constexpr int MapGlobeTerrainDefaultDetailReduction = 1;

// Cells per side of a Globe imagery tile's terrain mesh: exactly the DEM
// cells of terrain_zoom it covers, i.e. one mesh cell per DEM cell, so every
// mesh vertex lies on a DEM sample. The DEM pyramid is the level of detail:
// a coarser mesh comes from a coarser DEM tile (which the provider derives
// from its source), never from skipping samples inside a tile. Beyond 64x
// subdivision one cell covers a part of a DEM cell (bilinear inside it).
inline int mapGlobeTerrainCellCount(int imagery_zoom, int terrain_zoom)
{
    const int zoom_delta = qMax(0, imagery_zoom - terrain_zoom);
    return qMax(1, MapTerrainTileCellCount >> qMin(zoom_delta, 6));
}

// Which DEM zoom each Globe imagery tile draws its relief from. Built from
// the terrain service's dataset catalog (MapTerrainTileSource) and the
// user's detail setting (mapGlobeTerrainLevels() in
// map_globe_surface_preparation.h); pure value otherwise, so it can be
// compared between frames to detect a change that needs new meshes.
struct MapGlobeTerrainLevels
{
    // Empty: no relief (no terrain service, or its catalog has not arrived).
    QString dataset;
    // Lowest DEM zoom drawn: MapGlobeTerrainReliefMinimumZoom or the
    // dataset's minimum zoom, whichever is higher.
    int minimum_zoom = MapGlobeTerrainReliefMinimumZoom;
    // The dataset's native detail (its maximum zoom). Finer imagery tiles
    // subdivide the DEM tile of this zoom: the terrain is never drawn finer
    // than the source provides.
    int maximum_zoom = -1;
    // DEM levels the terrain stays below the imagery: 0 draws a DEM tile of
    // the imagery tile's own zoom (one mesh cell per DEM sample spacing, about
    // 4 px), each further level halves the terrain resolution (8, 16, 32 px).
    int detail_reduction = 0;

    bool enabled() const
    {
        return !this->dataset.isEmpty() && this->maximum_zoom >= this->minimum_zoom;
    }

    bool reliefAtImageryZoom(int imagery_zoom) const
    {
        return this->enabled() && imagery_zoom >= this->minimum_zoom;
    }

    // -1 where no relief is drawn.
    int terrainZoomForImageryZoom(int imagery_zoom) const
    {
        if (!this->reliefAtImageryZoom(imagery_zoom))
            return -1;
        return qBound(this->minimum_zoom, imagery_zoom - this->detail_reduction, this->maximum_zoom);
    }

    bool operator==(const MapGlobeTerrainLevels &other) const
    {
        return this->dataset == other.dataset && this->minimum_zoom == other.minimum_zoom
            && this->maximum_zoom == other.maximum_zoom
            && this->detail_reduction == other.detail_reduction;
    }

    bool operator!=(const MapGlobeTerrainLevels &other) const
    {
        return !(*this == other);
    }
};

#endif // MAP_GLOBE_TERRAIN_LEVELS_H
