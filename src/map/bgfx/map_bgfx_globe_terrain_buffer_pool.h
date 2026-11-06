#ifndef MAP_BGFX_GLOBE_TERRAIN_BUFFER_POOL_H
#define MAP_BGFX_GLOBE_TERRAIN_BUFFER_POOL_H

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>

// Owns the globe terrain's mutable GPU geometry: the retained tile vertex
// window, polar caps, and wireframe, plus the caps' 32-bit index stream.
// Immutable per-resolution tile/skirt indices are managed separately by
// MapBgfxGlobeTileIndexPool. All calls must occur on the owning bgfx thread.
// Release before shutting down/reinitializing the bgfx device.
class MapBgfxGlobeTerrainBufferPool final
{
public:
    MapBgfxGlobeTerrainBufferPool() = default;
    MapBgfxGlobeTerrainBufferPool(const MapBgfxGlobeTerrainBufferPool &) = delete;
    MapBgfxGlobeTerrainBufferPool &operator=(const MapBgfxGlobeTerrainBufferPool &) = delete;

    // Full replacement preserves the existing growable GPU allocation.
    // Empty streams are valid and retain their allocation for later reuse.
    // On failure the caller should retain its full-upload-pending flag.
    bool uploadWindow(const bgfx::VertexLayout &layout, const void *vertices,
                      std::size_t vertex_count, std::size_t vertex_stride);
    bool uploadCaps(const bgfx::VertexLayout &layout, const void *vertices,
                    std::size_t vertex_count, std::size_t vertex_stride,
                    const std::uint32_t *indices, std::size_t index_count);
    bool uploadWireframe(const bgfx::VertexLayout &layout, const void *vertices,
                         std::size_t vertex_count, std::size_t vertex_stride);

    // Upload just [first_vertex, first_vertex + patch_count) from the supplied
    // complete retained window, without reallocating or copying other tiles.
    // Rejects stale/out-of-range patches and byte-count overflows.
    bool patchWindow(const void *all_vertices, std::size_t total_vertices,
                     std::size_t first_vertex, std::size_t patch_count,
                     std::size_t vertex_stride);

    bgfx::DynamicVertexBufferHandle window() const { return this->window_vertices; }
    bgfx::DynamicVertexBufferHandle caps() const { return this->cap_vertices; }
    bgfx::DynamicIndexBufferHandle capIndices() const { return this->cap_indices; }
    bgfx::DynamicVertexBufferHandle wireframe() const { return this->wireframe_vertices; }

    void release();

private:
    static bool replaceVertices(bgfx::DynamicVertexBufferHandle *handle,
                                const bgfx::VertexLayout &layout, const void *vertices,
                                std::size_t vertex_count, std::size_t vertex_stride);
    static bool replaceIndices(bgfx::DynamicIndexBufferHandle *handle,
                               const std::uint32_t *indices, std::size_t index_count);

    bgfx::DynamicVertexBufferHandle window_vertices = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle cap_vertices = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle cap_indices = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle wireframe_vertices = BGFX_INVALID_HANDLE;
    std::size_t window_vertex_count = 0;
    std::size_t window_vertex_stride = 0;
};

#endif // MAP_BGFX_GLOBE_TERRAIN_BUFFER_POOL_H
