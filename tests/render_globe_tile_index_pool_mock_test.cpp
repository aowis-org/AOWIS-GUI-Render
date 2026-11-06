#include "map/bgfx/map_bgfx_globe_tile_index_pool.h"

#include <cstdint>
#include <cstring>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe tile GPU pool test failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

int main()
{
    bgfx::mock::reset();
    MapBgfxGlobeTileIndexPool pool;
    CHECK(!bgfx::isValid(pool.get(-1)));
    CHECK(!bgfx::isValid(pool.get(0)));
    CHECK(!bgfx::isValid(pool.get(65)));
    CHECK(bgfx::mock::created_static_index_buffers == 0);

    const bgfx::IndexBufferHandle first = pool.get(4);
    CHECK(bgfx::isValid(first));
    CHECK(bgfx::mock::created_static_index_buffers == 1);
    CHECK(bgfx::mock::last_static_indices.size()
          == std::size_t(mapGlobeTileIndexCount(4)) * sizeof(quint16));
    quint16 first_triangle[3] = {0, 0, 0};
    std::memcpy(first_triangle, bgfx::mock::last_static_indices.data(), sizeof(first_triangle));
    CHECK(first_triangle[0] == 0 && first_triangle[1] == 5 && first_triangle[2] == 1);
    CHECK(pool.get(4).idx == first.idx);
    CHECK(bgfx::mock::created_static_index_buffers == 1);

    const bgfx::IndexBufferHandle second = pool.get(64);
    CHECK(bgfx::isValid(second) && second.idx != first.idx);
    CHECK(bgfx::mock::created_static_index_buffers == 2);
    pool.release();
    CHECK(bgfx::mock::destroyed_static_index_buffers == 2);
    pool.release();
    CHECK(bgfx::mock::destroyed_static_index_buffers == 2);
    const bgfx::IndexBufferHandle after_recreate = pool.get(4);
    CHECK(bgfx::isValid(after_recreate) && after_recreate.idx != first.idx);
    CHECK(bgfx::mock::created_static_index_buffers == 3);
    pool.release();
    CHECK(bgfx::mock::destroyed_static_index_buffers == 3);
    return 0;
}
#undef CHECK
