#include "map/bgfx/map_bgfx_globe_gpu.h"

#include <iostream>
#include <cstdint>

class RecordingStyle final : public MapBgfx2dStyleBinding
{
public:
    bool available = true;
    mutable unsigned int bind_calls = 0;
    bool ready() const override { return this->available; }
    void bind(Mode, Table, bool) const override { ++this->bind_calls; }
};

int main()
{
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe GPU mock test failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)
    MapBgfxGlobeGpu::Input input;
    for (std::uint16_t index = 0; index < static_cast<std::uint16_t>(MapBgfxGlobeStream::Count); ++index)
        input.setStream(static_cast<MapBgfxGlobeStream>(index), bgfx::DynamicVertexBufferHandle{static_cast<std::uint16_t>(index + 1)}, 6);
    for (std::uint16_t index = 0; index < static_cast<std::uint16_t>(MapBgfxGlobeProgram::Count); ++index)
        input.setProgram(static_cast<MapBgfxGlobeProgram>(index), bgfx::ProgramHandle{static_cast<std::uint16_t>(index + 1)});
    input.uniforms = {{1}, {2}, {3}, {4}, {5}, {6}};
    input.icon_sampler = {7};
    input.albedo_sampler = {8};
    input.icon_atlas = {9};
    input.tank_texture = {10};
    input.reservoir_texture = {11};
    input.icon_indices = {12};
    input.icon_index_count = 6;
    input.show_junctions = true;
    MapBgfxGlobeGpu::FrameUniforms frame;
    RecordingStyle style;
    for (const MapBgfxGlobeUndergroundMode mode : {MapBgfxGlobeUndergroundMode::Solid,
                                                    MapBgfxGlobeUndergroundMode::XRay,
                                                    MapBgfxGlobeUndergroundMode::Hide})
    {
        input.underground = mode;
        bgfx::mock::reset();
        style.bind_calls = 0;
        const std::uint32_t count = MapBgfxGlobeGpu::draw(input, frame, style);
        const std::uint32_t expected = mode == MapBgfxGlobeUndergroundMode::Solid ? 17
            : mode == MapBgfxGlobeUndergroundMode::XRay ? 13 : 11;
        CHECK(count == expected);
        CHECK(bgfx::mock::submissions.size() == expected);
        CHECK(style.bind_calls == expected);
        CHECK(bgfx::mock::uniform_calls == expected * 6);
        CHECK(bgfx::mock::texture_calls == 3);
        CHECK(bgfx::mock::index_calls == 1);
        CHECK(bgfx::mock::submissions.back().indexed);
    }
    // Empty streams never issue GPU work; the style is not bound for them.
    input.setStream(MapBgfxGlobeStream::Link, bgfx::DynamicVertexBufferHandle{1}, 0);
    input.underground = MapBgfxGlobeUndergroundMode::Solid;
    bgfx::mock::reset();
    style.bind_calls = 0;
    CHECK(MapBgfxGlobeGpu::draw(input, frame, style) == 15);
    CHECK(style.bind_calls == 15);
    // A missing GPU style table prevents all submissions, even when streams
    // and programs are available.
    style.available = false;
    bgfx::mock::reset();
    CHECK(MapBgfxGlobeGpu::draw(input, frame, style) == 0);
    CHECK(bgfx::mock::submissions.empty());
    return 0;
#undef CHECK
}
