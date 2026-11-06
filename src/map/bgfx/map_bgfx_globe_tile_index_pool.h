#ifndef MAP_BGFX_GLOBE_TILE_INDEX_POOL_H
#define MAP_BGFX_GLOBE_TILE_INDEX_POOL_H

#include "map/render/map_globe_tile_topology.h"

#include <bgfx/bgfx.h>

#include <array>

// GPU-side ownership of terrain's immutable, shared tile index topology.
// One index buffer per cell count (1..64); cells of equal resolution reuse
// it for all visible globe tiles, including their crack-masking skirts.
// Methods must run on the owning bgfx thread. Call release() before device
// shutdown and after device loss; later get() allocates fresh GPU objects.
class MapBgfxGlobeTileIndexPool final
{
public:
    MapBgfxGlobeTileIndexPool();
    MapBgfxGlobeTileIndexPool(const MapBgfxGlobeTileIndexPool &) = delete;
    MapBgfxGlobeTileIndexPool &operator=(const MapBgfxGlobeTileIndexPool &) = delete;

    bgfx::IndexBufferHandle get(int cells);
    void release();

private:
    std::array<bgfx::IndexBufferHandle, MapGlobeTileMaximumCellCount + 1> buffers;
};

#endif // MAP_BGFX_GLOBE_TILE_INDEX_POOL_H
