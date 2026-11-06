#include "map/bgfx/map_bgfx_globe_tile_index_pool.h"

#include <cstddef>
#include <cstdint>

MapBgfxGlobeTileIndexPool::MapBgfxGlobeTileIndexPool()
{
    const bgfx::IndexBufferHandle invalid = BGFX_INVALID_HANDLE;
    this->buffers.fill(invalid);
}

bgfx::IndexBufferHandle MapBgfxGlobeTileIndexPool::get(int cells)
{
    if (cells < 1 || cells > MapGlobeTileMaximumCellCount)
        return BGFX_INVALID_HANDLE;

    bgfx::IndexBufferHandle &handle = this->buffers[std::size_t(cells)];
    if (bgfx::isValid(handle))
        return handle;

    // Cached topology never moves or changes for the life of the process;
    // bgfx::makeRef is therefore safe across asynchronous GPU consumption.
    const QVector<quint16> &indices = mapGlobeTileIndices(cells);
    if (indices.isEmpty())
        return BGFX_INVALID_HANDLE;
    handle = bgfx::createIndexBuffer(bgfx::makeRef(
        indices.constData(), std::uint32_t(indices.size() * sizeof(quint16))));
    return handle;
}

void MapBgfxGlobeTileIndexPool::release()
{
    for (bgfx::IndexBufferHandle &handle : this->buffers)
    {
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}
