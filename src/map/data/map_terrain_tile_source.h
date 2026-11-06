#ifndef MAP_TERRAIN_TILE_SOURCE_H
#define MAP_TERRAIN_TILE_SOURCE_H

#include "map/data/map_terrain_dataset.h"
#include "map/data/map_terrain_tile.h"

#include <QString>
#include <QtGlobal>

// The decoded terrain tiles the globe terrain pipeline reads, and the place
// it asks for missing ones. The host owns transport, caching and the
// dataset catalog request (the GUI: its terrain repository) and tells the
// pipeline about arrivals itself (MapGlobeSurfacePreparation::
// notifyTerrainTileAvailable() and friends). Returned tiles stay valid until
// the host's next tile-available / tiles-deleted notification.
class MapTerrainTileSource
{
public:
    virtual ~MapTerrainTileSource() = default;

    // nullptr while the tile is not cached.
    virtual const MapTerrainTile *tile(const QString &key) const = 0;
    virtual const MapTerrainTile *tile(const QString &dataset, int zoom,
                                       quint32 x, quint32 y) const = 0;
    // Asynchronous; repeated requests for a pending tile are cheap.
    virtual void requestTile(const QString &dataset, int zoom, quint32 x, quint32 y) = 0;
    // The dataset the renderer draws, nullptr until the catalog is known.
    virtual const MapTerrainDatasetInfo *activeDataset() const = 0;
};

#endif // MAP_TERRAIN_TILE_SOURCE_H
