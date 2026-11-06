#ifndef MAP_BGFX_GLOBE_BUFFER_POOL_H
#define MAP_BGFX_GLOBE_BUFFER_POOL_H

#include "map/bgfx/map_bgfx_globe_gpu.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>

// GPU ownership for all globe-network vertex streams and the sorted icon
// index stream. This class has no knowledge of the GUI scene or hydraulic
// model: the host supplies a vertex layout and contiguous vertex bytes.
// All methods must be invoked on the owning bgfx render/submission thread.
// Call release() before shutting down/reinitializing the bgfx device.
class MapBgfxGlobeBufferPool final
{
public:
    MapBgfxGlobeBufferPool() = default;
    MapBgfxGlobeBufferPool(const MapBgfxGlobeBufferPool &) = delete;
    MapBgfxGlobeBufferPool &operator=(const MapBgfxGlobeBufferPool &) = delete;

    // Uploads a complete vertex stream. Empty input hides the stream without
    // destroying the backing allocation, preserving the previous reuse policy.
    // Returns bytes enqueued, or zero for empty/invalid/oversized input.
    std::uint64_t upload(MapBgfxGlobeStream stream, const bgfx::VertexLayout &layout,
                         const void *vertices, std::size_t count, std::size_t stride);

    // The host determines draw order. Render owns its GPU index allocation.
    std::uint64_t uploadIconOrder(const std::uint32_t *indices, std::size_t count);

    // Populates only stream/index handles and counts. Programs, uniforms,
    // textures and view configuration remain separate responsibilities.
    void populateInput(MapBgfxGlobeGpu::Input *input) const;

    // Explicit GPU teardown; idempotent and safe to call before first upload.
    void release();

private:
    std::array<MapBgfxGlobeGpu::StreamBuffer,
               static_cast<std::size_t>(MapBgfxGlobeStream::Count)> streams = {};
    bgfx::DynamicIndexBufferHandle icon_indices = BGFX_INVALID_HANDLE;
    std::uint32_t icon_index_count = 0;
};

#endif // MAP_BGFX_GLOBE_BUFFER_POOL_H
