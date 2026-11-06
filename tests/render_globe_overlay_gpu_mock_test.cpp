#include "map/bgfx/map_bgfx_globe_overlay_gpu.h"

#include <iostream>
#include <cstdint>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe overlay GPU mock failed at line " << __LINE__ \
              << ": " << #condition << '\n'; return 1; \
} } while (false)

namespace
{
bool fail_sky = false;
bool fail_bake_program = false;
bgfx::ProgramHandle loadProgram(const char *vertex, const char *)
{
    if ((fail_sky && vertex[3] == 'g' && vertex[9] == 's')
        || (fail_bake_program && vertex[3] == 'g' && vertex[9] == 'h'))
        return BGFX_INVALID_HANDLE;
    return {bgfx::mock::next_handle++};
}
}

int main()
{
    bgfx::mock::reset();
    MapBgfxGlobeSkyGpu sky;
    MapBgfxGlobeSkyGpu::Frame frame;
    CHECK(!sky.ready());
    CHECK(!sky.draw(12, frame));
    CHECK(sky.initialize(&loadProgram));
    CHECK(sky.ready());
    CHECK(bgfx::mock::created_static_vertex_buffers == 1);
    CHECK(bgfx::mock::created_uniforms == 5);
    CHECK(sky.draw(12, frame));
    CHECK(bgfx::mock::submissions.size() == 1);
    CHECK((bgfx::mock::submissions[0].state & BGFX_STATE_BLEND_ADD) != 0);
    CHECK((bgfx::mock::submissions[0].state & BGFX_STATE_WRITE_Z) == 0);
    CHECK(bgfx::mock::uniform_calls == 5);
    sky.release();
    sky.release();
    CHECK(bgfx::mock::destroyed_static_vertex_buffers == 1);
    CHECK(bgfx::mock::destroyed_uniforms == 5);
    CHECK(bgfx::mock::destroyed_programs == 1);

    bgfx::mock::reset();
    fail_sky = true;
    CHECK(!sky.initialize(&loadProgram));
    CHECK(!sky.ready());
    CHECK(bgfx::mock::created_static_vertex_buffers == 0);
    fail_sky = false;

    bgfx::mock::reset();
    bgfx::mock::fail_uniform_after = 2;
    CHECK(!sky.initialize(&loadProgram));
    CHECK(!sky.ready());
    CHECK(bgfx::mock::destroyed_uniforms == 2);
    CHECK(bgfx::mock::destroyed_static_vertex_buffers == 1);
    CHECK(bgfx::mock::destroyed_programs == 1);
    bgfx::mock::reset();
    CHECK(sky.initialize(&loadProgram));
    sky.release();

    bgfx::mock::reset();
    MapBgfxGlobeHeatmapGpu heatmap;
    CHECK(!heatmap.ready());
    CHECK(!heatmap.ensurePage(0));
    CHECK(heatmap.initialize(&loadProgram));
    CHECK(heatmap.ready());
    CHECK(bgfx::mock::created_uniforms == 4);
    CHECK(!bgfx::isValid(heatmap.pageTexture(-1)));
    CHECK(!bgfx::isValid(heatmap.pageTexture(4)));
    CHECK(heatmap.ensurePage(1));
    CHECK(heatmap.ensurePage(1));
    CHECK(bgfx::mock::created_textures == 1);
    CHECK(bgfx::mock::created_frame_buffers == 1);
    CHECK(!heatmap.ensurePage(4));
    float tile[4] = {0, 0, 1, 1};
    float style[4] = {0.5f, 0, 0, 0};
    const MapBgfxGlobeTerrainGpu::Heatmap composite = heatmap.composite(1, tile, style);
    CHECK(bgfx::isValid(composite.program));
    CHECK(bgfx::isValid(composite.texture));
    CHECK(composite.style == style && composite.tile_rect == tile);
    CHECK(!bgfx::isValid(heatmap.composite(0, tile, style).program));

    heatmap.orderViews(20, 8);
    CHECK(bgfx::mock::view_order.size() == 5);
    CHECK(bgfx::mock::view_order[0] == 8 && bgfx::mock::view_order[4] == 20);
    MapBgfxGlobeHeatmapGpu::BakeVertex clear_vertices[6];
    MapBgfxGlobeHeatmapGpu::BakeVertex stamp_vertices[6];
    CHECK(heatmap.bakePage(9, 1, clear_vertices, 6, stamp_vertices, 6));
    CHECK(bgfx::mock::submissions.size() == 2);
    CHECK(bgfx::mock::submissions[0].program == bgfx::mock::submissions[1].program);
    CHECK(bgfx::mock::submissions[0].state != bgfx::mock::submissions[1].state);
    CHECK(bgfx::mock::transient_allocations == 2);
    CHECK(bgfx::mock::frame_buffer_views.size() == 1);
    CHECK(bgfx::mock::frame_buffer_views[0] == 9);
    CHECK(bgfx::mock::transient_storage.size() == 2);
    CHECK(bgfx::mock::transient_storage[0].size() == 6 * sizeof(clear_vertices[0]));
    CHECK(!heatmap.bakePage(9, 1, nullptr, 6, stamp_vertices, 6));
    CHECK(!heatmap.bakePage(9, 0, clear_vertices, 6, stamp_vertices, 6));
    bgfx::mock::transient_capacity = 11;
    CHECK(!heatmap.bakePage(9, 1, clear_vertices, 6, stamp_vertices, 6));
    CHECK(bgfx::mock::submissions.size() == 2);
    CHECK(bgfx::mock::transient_allocations == 2);
    heatmap.release();
    heatmap.release();
    CHECK(bgfx::mock::destroyed_frame_buffers == 1);
    CHECK(bgfx::mock::destroyed_textures == 1);
    CHECK(bgfx::mock::destroyed_uniforms == 4);
    CHECK(bgfx::mock::destroyed_programs == 2);

    bgfx::mock::reset();
    CHECK(heatmap.initialize(&loadProgram));
    bgfx::mock::fail_texture_after = 0;
    CHECK(!heatmap.ensurePage(0));
    CHECK(!bgfx::isValid(heatmap.pageTexture(0)));
    bgfx::mock::fail_texture_after = -1;
    bgfx::mock::fail_frame_buffer = true;
    CHECK(!heatmap.ensurePage(0));
    CHECK(bgfx::mock::destroyed_textures == 1);
    CHECK(!bgfx::isValid(heatmap.pageTexture(0)));
    bgfx::mock::fail_frame_buffer = false;
    CHECK(heatmap.ensurePage(0));
    heatmap.release();

    bgfx::mock::reset();
    fail_bake_program = true;
    CHECK(!heatmap.initialize(&loadProgram));
    CHECK(!heatmap.ready());
    CHECK(bgfx::mock::destroyed_programs == 1);
    fail_bake_program = false;

    bgfx::mock::reset();
    bgfx::mock::fail_uniform_after = 2;
    CHECK(!heatmap.initialize(&loadProgram));
    CHECK(!heatmap.ready());
    CHECK(bgfx::mock::destroyed_uniforms == 2);
    CHECK(bgfx::mock::destroyed_programs == 2);
    bgfx::mock::reset();
    CHECK(heatmap.initialize(&loadProgram));
    heatmap.release();
    return 0;
}
#undef CHECK
