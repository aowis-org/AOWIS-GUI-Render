#include "map/bgfx/map_bgfx_globe_draw_plan.h"

#include <iostream>
#include <cstddef>
#include <vector>

using Pass = MapBgfxGlobeDrawPass;
using Stream = MapBgfxGlobeStream;
using Program = MapBgfxGlobeProgram;
using State = MapBgfxGlobeState;
using Mode = MapBgfxGlobeUndergroundMode;
using Texture = MapBgfxGlobeTexture;

static std::vector<Pass> passes(Mode mode, bool junctions, bool atlas)
{
    std::vector<Pass> result;
    mapBgfxGlobeVisitPasses(mode, junctions, atlas,
                            [&result](const Pass &pass) { result.push_back(pass); });
    return result;
}

int main()
{
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe pass contract failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)
    const std::vector<Pass> solid = passes(Mode::Solid, true, true);
    const std::vector<Pass> xray = passes(Mode::XRay, true, true);
    const std::vector<Pass> hide = passes(Mode::Hide, true, true);
    CHECK(solid.size() == 17);
    CHECK(xray.size() == 13);
    CHECK(hide.size() == 11);
    CHECK(solid.front().stream == Stream::Link && solid.front().state == State::Overlay);
    CHECK(solid[1].stream == Stream::Node && solid[1].state == State::Overlay);
    CHECK(solid[2].stream == Stream::Junction && solid[2].state == State::Overlay);
    CHECK(solid[3].stream == Stream::SelectedNode && solid[3].vertex_colors);
    CHECK(solid[6].stream == Stream::Link && solid[6].state == State::DepthWrite);
    CHECK(xray[0].stream == Stream::Link && xray[0].state == State::DepthWrite);
    CHECK(xray[1].stream == Stream::Flow && xray[1].state == State::DepthTest);
    CHECK(xray[4].stream == Stream::Tank && xray[4].texture == Texture::Tank);
    CHECK(xray[5].stream == Stream::Reservoir && xray[5].texture == Texture::Reservoir);
    CHECK(xray[xray.size() - 3].stream == Stream::UndergroundLink);
    CHECK(xray[xray.size() - 3].program == Program::LinkXRay);
    CHECK(xray[xray.size() - 2].stream == Stream::UndergroundJunction);
    CHECK(xray.back().stream == Stream::Icon && xray.back().ordered_icons);
    CHECK(hide.back().stream == Stream::Icon);
    const std::vector<Pass> no_junctions = passes(Mode::XRay, false, true);
    CHECK(no_junctions.size() == 11);
    for (const Pass &pass : no_junctions)
        CHECK(pass.stream != Stream::Junction && pass.stream != Stream::UndergroundJunction);
    const std::vector<Pass> no_atlas = passes(Mode::Solid, true, false);
    CHECK(no_atlas.size() == solid.size() - 1);
    for (const Pass &pass : no_atlas)
        CHECK(pass.stream != Stream::Icon);
    // Regardless of mode, the selected-link overlay occurs before the
    // depth-tested selected-node and diagnostic passes.
    for (const std::vector<Pass> *sequence : {&solid, &xray, &hide})
    {
        bool found_selected_link = false;
        for (const Pass &pass : *sequence)
        {
            if (pass.stream == Stream::SelectedLink)
            {
                CHECK(pass.state == State::Overlay && pass.vertex_colors);
                found_selected_link = true;
            }
            if (pass.stream == Stream::DiagnosticNode && pass.state == State::DepthWrite)
                CHECK(found_selected_link);
        }
    }
    return 0;
#undef CHECK
}
