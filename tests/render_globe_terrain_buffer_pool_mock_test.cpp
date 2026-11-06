#include "map/bgfx/map_bgfx_globe_terrain_buffer_pool.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Terrain GPU buffer test failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

int main()
{
    bgfx::mock::reset();
    MapBgfxGlobeTerrainBufferPool pool;
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    const std::array<float, 12> vertices = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const std::size_t count = 4;
    const std::size_t stride = 3 * sizeof(float);
    CHECK(!bgfx::isValid(pool.window()));
    CHECK(!pool.patchWindow(vertices.data(), count, 1, 1, stride));
    CHECK(!pool.uploadWindow(layout, vertices.data(), count, stride - 1));
    CHECK(!pool.uploadWindow(layout, nullptr, count, stride));
    CHECK(!pool.uploadWindow(layout, vertices.data(), std::numeric_limits<std::size_t>::max(), stride));
    CHECK(bgfx::mock::created_vertex_buffers == 0);

    CHECK(pool.uploadWindow(layout, vertices.data(), count, stride));
    CHECK(bgfx::mock::created_vertex_buffers == 1);
    CHECK(bgfx::mock::vertex_updates == 1);
    CHECK(bgfx::mock::vertex_update_history[0].first_element == 0);
    CHECK(bgfx::mock::vertex_update_history[0].bytes.size() == sizeof(vertices));
    const std::uint16_t window_handle = pool.window().idx;

    // Two independent terrain tiles each update only one two-vertex patch;
    // the unchanged prefix/suffix must not be reuploaded.
    CHECK(pool.patchWindow(vertices.data(), count, 1, 2, stride));
    CHECK(bgfx::mock::vertex_updates == 2);
    CHECK(bgfx::mock::vertex_update_history[1].first_element == 1);
    CHECK(bgfx::mock::vertex_update_history[1].bytes.size() == 2 * stride);
    CHECK(std::memcmp(bgfx::mock::vertex_update_history[1].bytes.data(),
                      vertices.data() + 3, 2 * stride) == 0);
    CHECK(bgfx::mock::created_vertex_buffers == 1);
    CHECK(bgfx::mock::vertex_update_history[1].handle == window_handle);

    // Stale window, invalid bounds, overflow and wrong stride must not upload.
    CHECK(!pool.patchWindow(vertices.data(), count + 1, 1, 2, stride));
    CHECK(!pool.patchWindow(vertices.data(), count, 3, 2, stride));
    CHECK(!pool.patchWindow(vertices.data(), count, count + 1, 0, stride));
    CHECK(!pool.patchWindow(vertices.data(), count, 0, 1, stride - 1));
    CHECK(!pool.patchWindow(nullptr, count, 0, 1, stride));
    CHECK(!pool.patchWindow(vertices.data(), count, 1, std::numeric_limits<std::size_t>::max(), stride));
    CHECK(bgfx::mock::vertex_updates == 2);
    CHECK(pool.patchWindow(nullptr, count, count, 0, stride));
    CHECK(bgfx::mock::vertex_updates == 2);

    // A full replacement reuses the buffer while changing the retained window.
    CHECK(pool.uploadWindow(layout, vertices.data(), 3, stride));
    CHECK(pool.window().idx == window_handle);
    CHECK(bgfx::mock::vertex_updates == 3);
    CHECK(!pool.patchWindow(vertices.data(), count, 0, 1, stride));
    CHECK(pool.patchWindow(vertices.data(), 3, 2, 1, stride));
    CHECK(bgfx::mock::vertex_update_history.back().first_element == 2);
    CHECK(bgfx::mock::vertex_update_history.back().bytes.size() == stride);

    const std::array<std::uint32_t, 6> indices = {0, 1, 2, 2, 3, 0};
    CHECK(!pool.uploadCaps(layout, vertices.data(), count, stride,
                           nullptr, indices.size()));
    CHECK(bgfx::mock::created_index_buffers == 0);
    CHECK(pool.uploadCaps(layout, vertices.data(), count, stride,
                          indices.data(), indices.size()));
    CHECK(bgfx::isValid(pool.caps()) && bgfx::isValid(pool.capIndices()));
    CHECK(bgfx::mock::created_vertex_buffers == 2);
    CHECK(bgfx::mock::created_index_buffers == 1);
    CHECK(bgfx::mock::index_update_history.back().bytes.size() == sizeof(indices));
    CHECK(pool.uploadCaps(layout, vertices.data(), 3, stride,
                          indices.data(), 3));
    CHECK(bgfx::mock::created_vertex_buffers == 2 && bgfx::mock::created_index_buffers == 1);

    CHECK(pool.uploadWireframe(layout, vertices.data(), 3, stride));
    CHECK(bgfx::mock::created_vertex_buffers == 3);
    const std::uint16_t wireframe_handle = pool.wireframe().idx;
    CHECK(pool.uploadWireframe(layout, nullptr, 0, stride));
    CHECK(pool.wireframe().idx == wireframe_handle);
    CHECK(bgfx::mock::created_vertex_buffers == 3);

    pool.release();
    CHECK(bgfx::mock::destroyed_vertex_buffers == 3);
    CHECK(bgfx::mock::destroyed_index_buffers == 1);
    CHECK(!bgfx::isValid(pool.window()) && !bgfx::isValid(pool.caps())
          && !bgfx::isValid(pool.wireframe()) && !bgfx::isValid(pool.capIndices()));
    CHECK(!pool.patchWindow(vertices.data(), count, 0, 1, stride));
    pool.release();
    CHECK(bgfx::mock::destroyed_vertex_buffers == 3);

    // Device recreation: fresh GPU handles, no stale retained-window metadata.
    CHECK(pool.uploadWindow(layout, vertices.data(), count, stride));
    CHECK(bgfx::isValid(pool.window()) && pool.window().idx != window_handle);
    CHECK(bgfx::mock::created_vertex_buffers == 4);
    pool.release();
    CHECK(bgfx::mock::destroyed_vertex_buffers == 4);
    return 0;
}
#undef CHECK
