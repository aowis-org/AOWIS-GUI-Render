#include "map/bgfx/map_bgfx_globe_resource_pool.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe resource-pool failure at " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

namespace
{
std::vector<std::pair<std::string, std::string>> shader_pairs;
int fail_shader_index = -1;

bgfx::ProgramHandle loadProgram(const char *vertex, const char *fragment)
{
    if (fail_shader_index >= 0 && shader_pairs.size() == static_cast<std::size_t>(fail_shader_index))
        return BGFX_INVALID_HANDLE;
    shader_pairs.emplace_back(vertex, fragment);
    return {bgfx::mock::next_handle++};
}
MapBgfxGlobeMipTexture makeTexture()
{
    MapBgfxGlobeMipTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.pixels.resize(4 * (4 + 1), 0x9f); // 2x2 mip + 1x1 mip
    return texture;
}
}

int main()
{
    bgfx::mock::reset();
    MapBgfxGlobeResourcePool pool;
    MapBgfxGlobeGpu::Input input;
    pool.populateInput(&input);
    CHECK(!pool.ready());
    CHECK(!bgfx::isValid(input.tank_texture));
    const MapBgfxGlobeMipTexture image = makeTexture();
    MapBgfxGlobeMipTexture invalid = image;
    invalid.pixels.pop_back();
    CHECK(!pool.initialize(loadProgram, image, invalid));
    CHECK(bgfx::mock::created_uniforms == 0 && shader_pairs.empty());
    CHECK(!pool.initialize(nullptr, image, image));

    CHECK(pool.initialize(loadProgram, image, image));
    CHECK(pool.ready());
    CHECK(shader_pairs.size() == 8);
    const std::array<std::pair<std::string, std::string>, 8> expected = {{
        {"vs_globe_link", "fs_globe_link"},
        {"vs_globe_link", "fs_globe_link_xray"},
        {"vs_globe_flow", "fs_globe_link"},
        {"vs_globe_node", "fs_globe_node"},
        {"vs_globe_junction", "fs_globe_junction"},
        {"vs_globe_junction", "fs_globe_junction_xray"},
        {"vs_globe_icon", "fs_globe_icon"},
        {"vs_globe_model", "fs_globe_model"},
    }};
    CHECK(std::equal(shader_pairs.begin(), shader_pairs.end(), expected.begin()));
    CHECK(bgfx::mock::created_uniforms == 8);
    CHECK(bgfx::mock::created_textures == 2);
    CHECK(bgfx::mock::texture_mips == 2);
    CHECK(bgfx::mock::created_texture_bytes == 40);
    CHECK(bgfx::mock::texture_flags == (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP));
    pool.populateInput(&input);
    for (const bgfx::ProgramHandle program : input.programs)
        CHECK(bgfx::isValid(program));
    CHECK(input.programs[static_cast<std::size_t>(MapBgfxGlobeProgram::Flow)].idx == input.programs[0].idx + 2);
    CHECK(bgfx::isValid(input.uniforms.view_projection));
    CHECK(bgfx::isValid(input.uniforms.camera_eye));
    CHECK(bgfx::isValid(input.icon_sampler) && bgfx::isValid(input.albedo_sampler));
    CHECK(bgfx::isValid(input.tank_texture) && bgfx::isValid(input.reservoir_texture));

    pool.release();
    pool.populateInput(&input);
    CHECK(!pool.ready() && !bgfx::isValid(input.tank_texture));
    CHECK(bgfx::mock::destroyed_programs == 8);
    CHECK(bgfx::mock::destroyed_uniforms == 8);
    CHECK(bgfx::mock::destroyed_textures == 2);
    pool.release();
    CHECK(bgfx::mock::destroyed_programs == 8);

    // Partial shader initialization must roll back created programs.
    shader_pairs.clear();
    fail_shader_index = 3;
    CHECK(!pool.initialize(loadProgram, image, image));
    CHECK(!pool.ready());
    CHECK(bgfx::mock::destroyed_programs == 11);
    fail_shader_index = -1;

    // Partial uniform initialization must roll back programs and uniforms.
    bgfx::mock::fail_uniform_after = static_cast<int>(bgfx::mock::created_uniforms) + 2;
    CHECK(!pool.initialize(loadProgram, image, image));
    CHECK(!pool.ready());
    CHECK(bgfx::mock::destroyed_uniforms == 10);
    bgfx::mock::fail_uniform_after = -1;

    // A rejected second texture must destroy the first one as well.
    bgfx::mock::fail_texture_after = static_cast<int>(bgfx::mock::created_textures) + 1;
    CHECK(!pool.initialize(loadProgram, image, image));
    CHECK(!pool.ready());
    CHECK(bgfx::mock::destroyed_textures == 3);
    bgfx::mock::fail_texture_after = -1;

    CHECK(pool.initialize(loadProgram, image, image));
    CHECK(pool.ready());
    pool.release();
    CHECK(!pool.ready());
    return 0;
}
#undef CHECK
