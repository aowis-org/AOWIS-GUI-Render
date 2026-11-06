#include "map/bgfx/map_bgfx_globe_buffer_pool.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe buffer-pool test failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

int main()
{
    bgfx::mock::reset();
    MapBgfxGlobeBufferPool pool;
    bgfx::VertexLayout layout;
    MapBgfxGlobeGpu::Input input;
    pool.populateInput(&input);
    for (const MapBgfxGlobeGpu::StreamBuffer &stream : input.streams)
        CHECK(stream.count == 0 && !bgfx::isValid(stream.handle));
    CHECK(!bgfx::isValid(input.icon_indices) && input.icon_index_count == 0);
    const std::array<float, 6> vertices = {1, 2, 3, 4, 5, 6};
    const std::size_t stream_count = static_cast<std::size_t>(MapBgfxGlobeStream::Count);
    for (std::size_t index = 0; index < stream_count; ++index)
    {
        CHECK(pool.upload(static_cast<MapBgfxGlobeStream>(index), layout,
                          vertices.data(), vertices.size(), sizeof(float)) == sizeof(vertices));
    }
    CHECK(bgfx::mock::created_vertex_buffers == stream_count);
    CHECK(bgfx::mock::vertex_updates == stream_count);
    pool.populateInput(&input);
    for (const MapBgfxGlobeGpu::StreamBuffer &stream : input.streams)
        CHECK(stream.count == vertices.size() && bgfx::isValid(stream.handle));

    // Repeated updates reuse the same allocation; empty updates hide a stream.
    CHECK(pool.upload(MapBgfxGlobeStream::Link, layout, vertices.data(), 3, sizeof(float)) == 12);
    CHECK(bgfx::mock::created_vertex_buffers == stream_count);
    CHECK(pool.upload(MapBgfxGlobeStream::Link, layout, nullptr, 0, sizeof(float)) == 0);
    pool.populateInput(&input);
    CHECK(input.streams[static_cast<std::size_t>(MapBgfxGlobeStream::Link)].count == 0);
    CHECK(bgfx::isValid(input.streams[static_cast<std::size_t>(MapBgfxGlobeStream::Link)].handle));
    // Oversize requests never wrap the 32-bit GPU copy size.
    CHECK(pool.upload(MapBgfxGlobeStream::Link, layout, vertices.data(),
                      std::numeric_limits<std::size_t>::max(), sizeof(float)) == 0);
    CHECK(pool.upload(MapBgfxGlobeStream::Count, layout,
                      vertices.data(), vertices.size(), sizeof(float)) == 0);

    const std::array<std::uint32_t, 6> indices = {0, 1, 2, 3, 4, 5};
    CHECK(pool.uploadIconOrder(indices.data(), indices.size()) == sizeof(indices));
    CHECK(pool.uploadIconOrder(indices.data(), 3) == 3 * sizeof(std::uint32_t));
    CHECK(bgfx::mock::created_index_buffers == 1);
    CHECK(bgfx::mock::index_updates == 2);
    pool.populateInput(&input);
    CHECK(input.icon_index_count == 3 && bgfx::isValid(input.icon_indices));
    CHECK(pool.uploadIconOrder(nullptr, 0) == 0);
    pool.populateInput(&input);
    CHECK(input.icon_index_count == 0);

    pool.release();
    pool.populateInput(&input);
    CHECK(bgfx::mock::destroyed_vertex_buffers == stream_count);
    CHECK(bgfx::mock::destroyed_index_buffers == 1);
    for (const MapBgfxGlobeGpu::StreamBuffer &stream : input.streams)
        CHECK(stream.count == 0 && !bgfx::isValid(stream.handle));
    CHECK(input.icon_index_count == 0 && !bgfx::isValid(input.icon_indices));
    pool.release();
    CHECK(bgfx::mock::destroyed_vertex_buffers == stream_count);
    CHECK(bgfx::mock::destroyed_index_buffers == 1);
    // After bgfx device recreation the same pool allocates fresh buffers.
    CHECK(pool.upload(MapBgfxGlobeStream::Tank, layout, vertices.data(), 6, sizeof(float)) == sizeof(vertices));
    CHECK(bgfx::mock::created_vertex_buffers == stream_count + 1);
    pool.release();
    CHECK(bgfx::mock::destroyed_vertex_buffers == stream_count + 1);
    return 0;
}
#undef CHECK
