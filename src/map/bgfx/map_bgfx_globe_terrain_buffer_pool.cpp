#include "map/bgfx/map_bgfx_globe_terrain_buffer_pool.h"

#include <cstdint>
#include <limits>

namespace
{
bool validVertexBytes(const void *vertices, std::size_t count, std::size_t stride,
                      std::uint32_t *bytes)
{
    if (stride == 0 || count > std::numeric_limits<std::uint32_t>::max()
        || count > std::numeric_limits<std::uint32_t>::max() / stride
        || (count != 0 && vertices == nullptr))
        return false;
    *bytes = std::uint32_t(count * stride);
    return true;
}

bool validIndexBytes(const std::uint32_t *indices, std::size_t count, std::uint32_t *bytes)
{
    if (count > std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t)
        || (count != 0 && indices == nullptr))
        return false;
    *bytes = std::uint32_t(count * sizeof(std::uint32_t));
    return true;
}
}

bool MapBgfxGlobeTerrainBufferPool::replaceVertices(
    bgfx::DynamicVertexBufferHandle *handle, const bgfx::VertexLayout &layout,
    const void *vertices, std::size_t vertex_count, std::size_t vertex_stride)
{
    std::uint32_t bytes = 0;
    if (layout.getStride() != vertex_stride
        || !validVertexBytes(vertices, vertex_count, vertex_stride, &bytes))
        return false;
    if (!bgfx::isValid(*handle))
    {
        *handle = bgfx::createDynamicVertexBuffer(
            std::uint32_t(vertex_count == 0 ? 1 : vertex_count),
            layout, BGFX_BUFFER_ALLOW_RESIZE);
        if (!bgfx::isValid(*handle))
            return false;
    }
    if (bytes != 0)
        bgfx::update(*handle, 0, bgfx::copy(vertices, bytes));
    return true;
}

bool MapBgfxGlobeTerrainBufferPool::replaceIndices(
    bgfx::DynamicIndexBufferHandle *handle, const std::uint32_t *indices,
    std::size_t index_count)
{
    std::uint32_t bytes = 0;
    if (!validIndexBytes(indices, index_count, &bytes))
        return false;
    if (!bgfx::isValid(*handle))
    {
        *handle = bgfx::createDynamicIndexBuffer(
            std::uint32_t(index_count == 0 ? 1 : index_count),
            BGFX_BUFFER_INDEX32 | BGFX_BUFFER_ALLOW_RESIZE);
        if (!bgfx::isValid(*handle))
            return false;
    }
    if (bytes != 0)
        bgfx::update(*handle, 0, bgfx::copy(indices, bytes));
    return true;
}

bool MapBgfxGlobeTerrainBufferPool::uploadWindow(
    const bgfx::VertexLayout &layout, const void *vertices,
    std::size_t vertex_count, std::size_t vertex_stride)
{
    if (!replaceVertices(&this->window_vertices, layout, vertices, vertex_count, vertex_stride))
        return false;
    this->window_vertex_count = vertex_count;
    this->window_vertex_stride = vertex_stride;
    return true;
}

bool MapBgfxGlobeTerrainBufferPool::uploadCaps(
    const bgfx::VertexLayout &layout, const void *vertices,
    std::size_t vertex_count, std::size_t vertex_stride,
    const std::uint32_t *indices, std::size_t index_count)
{
    // Validate both streams before mutating either GPU allocation.
    std::uint32_t vertex_bytes = 0;
    std::uint32_t index_bytes = 0;
    if (layout.getStride() != vertex_stride
        || !validVertexBytes(vertices, vertex_count, vertex_stride, &vertex_bytes)
        || !validIndexBytes(indices, index_count, &index_bytes))
        return false;
    return replaceVertices(&this->cap_vertices, layout, vertices, vertex_count, vertex_stride)
        && replaceIndices(&this->cap_indices, indices, index_count);
}

bool MapBgfxGlobeTerrainBufferPool::uploadWireframe(
    const bgfx::VertexLayout &layout, const void *vertices,
    std::size_t vertex_count, std::size_t vertex_stride)
{
    return replaceVertices(&this->wireframe_vertices, layout, vertices, vertex_count, vertex_stride);
}

bool MapBgfxGlobeTerrainBufferPool::patchWindow(
    const void *all_vertices, std::size_t total_vertices,
    std::size_t first_vertex, std::size_t patch_count, std::size_t vertex_stride)
{
    if (!bgfx::isValid(this->window_vertices)
        || total_vertices != this->window_vertex_count
        || vertex_stride == 0 || vertex_stride != this->window_vertex_stride
        || first_vertex > total_vertices || patch_count > total_vertices - first_vertex
        || total_vertices > std::numeric_limits<std::uint32_t>::max()
        || patch_count > std::numeric_limits<std::uint32_t>::max() / vertex_stride
        || (patch_count != 0 && all_vertices == nullptr))
        return false;
    if (patch_count == 0)
        return true;
    const std::uint8_t *start = static_cast<const std::uint8_t *>(all_vertices)
        + first_vertex * vertex_stride;
    bgfx::update(this->window_vertices, std::uint32_t(first_vertex),
                 bgfx::copy(start, std::uint32_t(patch_count * vertex_stride)));
    return true;
}

void MapBgfxGlobeTerrainBufferPool::release()
{
    if (bgfx::isValid(this->window_vertices))
        bgfx::destroy(this->window_vertices);
    if (bgfx::isValid(this->cap_vertices))
        bgfx::destroy(this->cap_vertices);
    if (bgfx::isValid(this->cap_indices))
        bgfx::destroy(this->cap_indices);
    if (bgfx::isValid(this->wireframe_vertices))
        bgfx::destroy(this->wireframe_vertices);
    this->window_vertices = BGFX_INVALID_HANDLE;
    this->cap_vertices = BGFX_INVALID_HANDLE;
    this->cap_indices = BGFX_INVALID_HANDLE;
    this->wireframe_vertices = BGFX_INVALID_HANDLE;
    this->window_vertex_count = 0;
    this->window_vertex_stride = 0;
}
