#include "map/bgfx/map_bgfx_globe_buffer_pool.h"

#include <limits>

std::uint64_t MapBgfxGlobeBufferPool::upload(MapBgfxGlobeStream stream,
                                              const bgfx::VertexLayout &layout,
                                              const void *vertices, std::size_t count,
                                              std::size_t stride)
{
    const std::size_t stream_index = static_cast<std::size_t>(stream);
    if (stream_index >= this->streams.size())
        return 0;
    MapBgfxGlobeGpu::StreamBuffer &buffer = this->streams[stream_index];
    buffer.count = 0;
    if (count == 0 || vertices == nullptr || stride == 0
        || count > std::numeric_limits<std::uint32_t>::max()
        || stride > std::numeric_limits<std::uint32_t>::max() / count)
        return 0;

    const std::uint32_t vertex_count = static_cast<std::uint32_t>(count);
    const std::uint32_t byte_count = static_cast<std::uint32_t>(count * stride);
    if (!bgfx::isValid(buffer.handle))
        buffer.handle = bgfx::createDynamicVertexBuffer(vertex_count, layout, BGFX_BUFFER_ALLOW_RESIZE);
    if (!bgfx::isValid(buffer.handle))
        return 0;
    bgfx::update(buffer.handle, 0, bgfx::copy(vertices, byte_count));
    buffer.count = vertex_count;
    return byte_count;
}

std::uint64_t MapBgfxGlobeBufferPool::uploadIconOrder(const std::uint32_t *indices,
                                                       std::size_t count)
{
    this->icon_index_count = 0;
    if (count == 0 || indices == nullptr
        || count > std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t))
        return 0;
    const std::uint32_t index_count = static_cast<std::uint32_t>(count);
    if (!bgfx::isValid(this->icon_indices))
        this->icon_indices = bgfx::createDynamicIndexBuffer(
            index_count, BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE);
    if (!bgfx::isValid(this->icon_indices))
        return 0;
    const std::uint32_t bytes = static_cast<std::uint32_t>(count * sizeof(std::uint32_t));
    bgfx::update(this->icon_indices, 0, bgfx::copy(indices, bytes));
    this->icon_index_count = index_count;
    return bytes;
}

void MapBgfxGlobeBufferPool::populateInput(MapBgfxGlobeGpu::Input *input) const
{
    if (input == nullptr)
        return;
    input->streams = this->streams;
    input->icon_indices = this->icon_indices;
    input->icon_index_count = this->icon_index_count;
}

void MapBgfxGlobeBufferPool::release()
{
    for (MapBgfxGlobeGpu::StreamBuffer &buffer : this->streams)
    {
        if (bgfx::isValid(buffer.handle))
            bgfx::destroy(buffer.handle);
        buffer.handle = BGFX_INVALID_HANDLE;
        buffer.count = 0;
    }
    if (bgfx::isValid(this->icon_indices))
        bgfx::destroy(this->icon_indices);
    this->icon_indices = BGFX_INVALID_HANDLE;
    this->icon_index_count = 0;
}
