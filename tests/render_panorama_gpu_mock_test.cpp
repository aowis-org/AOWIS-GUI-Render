#include "map/bgfx/map_bgfx_panorama_gpu.h"

#include <cstdint>
#include <string>
#include <iostream>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Panorama GPU mock failed at line " << __LINE__ \
              << ": " << #condition << '\n'; return 1; \
} } while (false)

namespace
{
bool fail_program = false;
bgfx::ProgramHandle loadProgram(const char *vertex, const char *fragment)
{
    // The panorama reuses the imagery program: no new shader binaries.
    if (fail_program || std::string(vertex) != "vs_imagery_2d" || std::string(fragment) != "fs_imagery_2d")
        return BGFX_INVALID_HANDLE;
    return {bgfx::mock::next_handle++};
}
}

int main()
{
    bgfx::mock::reset();
    MapBgfxPanoramaGpu panorama;
    std::vector<MapPanoramaVertex> vertices;
    MapPanoramaGrid::build(MapPanoramaView(), 4, 2, &vertices);
    CHECK(!panorama.ready());
    CHECK(!panorama.draw(3, vertices));
    CHECK(panorama.initialize(&loadProgram));
    CHECK(panorama.ready());
    CHECK(bgfx::mock::created_uniforms == 2);
    // No image yet: nothing is drawn.
    CHECK(!panorama.hasImage());
    CHECK(!panorama.draw(3, vertices));
    CHECK(bgfx::mock::submissions.empty());

    const std::vector<std::uint8_t> pixels(8 * 4 * 4, 200);
    CHECK(panorama.setImage(8, 4, pixels.data()));
    CHECK(panorama.hasImage());
    CHECK(bgfx::mock::created_texture_bytes == 8u * 4u * 4u);
    CHECK(bgfx::mock::texture_mips == 0);
    // Repeats around the horizon, clamps at the poles.
    CHECK((bgfx::mock::texture_flags & BGFX_SAMPLER_V_CLAMP) != 0);
    CHECK(panorama.draw(3, vertices));
    CHECK(bgfx::mock::submissions.size() == 1);
    CHECK((bgfx::mock::submissions[0].state & BGFX_STATE_WRITE_A) != 0);
    CHECK((bgfx::mock::submissions[0].state & BGFX_STATE_WRITE_Z) == 0);
    CHECK(bgfx::mock::transient_allocations == 1);
    CHECK(bgfx::mock::transient_storage.back().size() == vertices.size() * sizeof(MapPanoramaVertex));

    // Too little transient space this frame: skipped, not partially drawn.
    bgfx::mock::transient_capacity = 5;
    CHECK(!panorama.draw(3, vertices));
    CHECK(bgfx::mock::submissions.size() == 1);

    // A new image replaces the old texture.
    CHECK(panorama.setImage(8, 4, pixels.data()));
    CHECK(bgfx::mock::created_textures == 2);
    CHECK(!panorama.setImage(0, 4, pixels.data()));
    CHECK(!panorama.hasImage());
    panorama.release();
    panorama.release();
    CHECK(bgfx::mock::destroyed_uniforms == 2);
    CHECK(bgfx::mock::destroyed_programs == 1);

    bgfx::mock::reset();
    fail_program = true;
    CHECK(!panorama.initialize(&loadProgram));
    CHECK(!panorama.ready());
    CHECK(bgfx::mock::created_uniforms == 0);
    std::cout << "panorama gpu mock: ok\n";
    return 0;
}
