#include "map/bgfx/map_bgfx_globe_terrain_gpu.h"

#include <cstdint>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Terrain GPU mock test failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

namespace
{
bool fail_shader = false;
bgfx::ProgramHandle loadProgram(const char *vertex, const char *)
{
    if (fail_shader && vertex[3] == 'g')
        return BGFX_INVALID_HANDLE;
    return {bgfx::mock::next_handle++};
}
}

int main()
{
    const std::array<std::uint8_t, 4> missing = {18, 58, 72, 255};
    const std::array<std::uint8_t, 4> polar = {235, 240, 245, 255};
    MapBgfxGlobeTerrainGpu gpu;
    MapBgfxGlobeTerrainGpu::Frame frame;
    const bgfx::ViewId view = 5;
    const bgfx::DynamicVertexBufferHandle window = {4};
    const bgfx::DynamicVertexBufferHandle caps = {5};
    const bgfx::DynamicVertexBufferHandle wireframe = {6};
    const bgfx::DynamicIndexBufferHandle cap_indices = {7};
    const bgfx::IndexBufferHandle tile_indices = {8};
    const bgfx::TextureHandle tile_texture = {9};
    const float uv[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    CHECK(!gpu.ready());
    CHECK(!gpu.drawTile(view, window, 0, 6, tile_indices, tile_texture, uv, frame));
    bgfx::mock::reset();
    CHECK(gpu.initialize(&loadProgram, missing, polar));
    CHECK(gpu.ready());
    CHECK(bgfx::mock::created_uniforms == 7);
    CHECK(bgfx::mock::created_textures == 2);
    CHECK(bgfx::mock::created_texture_bytes == 8);
    CHECK(gpu.missingTexture().idx != 0xffff);

    CHECK(gpu.drawCap(view, caps, cap_indices, 12, 18, frame));
    CHECK(gpu.drawTile(view, window, 20, 30, tile_indices, tile_texture, uv, frame));
    CHECK(gpu.drawWireframe(view, wireframe, 12, frame));
    CHECK(bgfx::mock::submissions.size() == 3);
    CHECK(bgfx::mock::submissions[0].vertex == caps.idx);
    CHECK(bgfx::mock::submissions[1].vertex == window.idx);
    CHECK(bgfx::mock::submissions[2].vertex == wireframe.idx);
    CHECK(bgfx::mock::submissions[0].program == bgfx::mock::submissions[1].program);
    CHECK(bgfx::mock::submissions[0].program != bgfx::mock::submissions[2].program);
    CHECK((bgfx::mock::submissions[0].state & BGFX_STATE_WRITE_Z) != 0);
    CHECK((bgfx::mock::submissions[2].state & BGFX_STATE_PT_LINES) != 0);
    CHECK(bgfx::mock::submissions[0].indexed && bgfx::mock::submissions[1].indexed);
    CHECK(!bgfx::mock::submissions[2].indexed);
    CHECK(bgfx::mock::texture_calls == 2);
    CHECK(bgfx::mock::uniform_calls == 13);
    CHECK(bgfx::mock::index_calls == 2);

    MapBgfxGlobeTerrainGpu::Heatmap heatmap;
    heatmap.program = {80};
    heatmap.sampler = {81};
    heatmap.tile_uniform = {82};
    heatmap.style_uniform = {83};
    heatmap.texture = {84};
    heatmap.tile_rect = uv;
    heatmap.style = uv;
    CHECK(gpu.drawTile(view, window, 20, 30, tile_indices, tile_texture, uv, frame, &heatmap));
    CHECK(bgfx::mock::submissions.size() == 4);
    CHECK(bgfx::mock::submissions[3].program == heatmap.program.idx);
    CHECK(bgfx::mock::texture_calls == 4);
    CHECK(bgfx::mock::uniform_calls == 21);
    CHECK(!gpu.drawTile(view, window, 0, 0, tile_indices, tile_texture, uv, frame));
    CHECK(!gpu.drawCap(view, caps, cap_indices, 0, 0, frame));
    CHECK(!gpu.drawWireframe(view, wireframe, 0, frame));
    CHECK(bgfx::mock::submissions.size() == 4);

    gpu.release();
    CHECK(!gpu.ready());
    CHECK(bgfx::mock::destroyed_programs == 2);
    CHECK(bgfx::mock::destroyed_uniforms == 7);
    CHECK(bgfx::mock::destroyed_textures == 2);
    gpu.release();
    CHECK(bgfx::mock::destroyed_programs == 2);

    bgfx::mock::reset();
    fail_shader = true;
    CHECK(!gpu.initialize(&loadProgram, missing, polar));
    CHECK(!gpu.ready());
    CHECK(bgfx::mock::destroyed_programs == 0);
    fail_shader = false;

    bgfx::mock::reset();
    bgfx::mock::fail_uniform_after = 3;
    CHECK(!gpu.initialize(&loadProgram, missing, polar));
    CHECK(!gpu.ready());
    CHECK(bgfx::mock::destroyed_uniforms == 3);
    CHECK(bgfx::mock::destroyed_programs == 2);
    CHECK(bgfx::mock::destroyed_textures == 2);

    bgfx::mock::reset();
    bgfx::mock::fail_uniform_after = -1;
    CHECK(gpu.initialize(&loadProgram, missing, polar));
    CHECK(gpu.ready());
    gpu.release();
    return 0;
}
#undef CHECK
