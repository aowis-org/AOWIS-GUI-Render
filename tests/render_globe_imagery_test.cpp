#include "map/bgfx/map_bgfx_globe_imagery.h"
#include <cassert>
#include <cmath>

int main()
{
    const MapBgfx2dImagery::TileId tile{8, 139, 84};
    const MapBgfxGlobeImagery::Source source = MapBgfxGlobeImagery::ancestor(tile, 2);
    assert(source.tile.zoom == 6 && source.tile.x == 34 && source.tile.y == 21);
    assert(std::abs(source.u0 - 0.75) < 1e-8 && std::abs(source.v0) < 1e-8);
    assert(std::abs(source.u1 - 1.0) < 1e-8 && std::abs(source.v1 - 0.25) < 1e-8);
    const float rect[4] = {0.1f, 0.2f, 0.9f, 0.8f};
    float uniform[4] = {};
    MapBgfxGlobeImagery::tileUniform(rect, source, uniform);
    assert(std::abs(uniform[0] - 0.7f) < 1e-6f);
    assert(std::abs(uniform[1] - 0.2f) < 1e-6f);
    assert(std::abs(uniform[2] - 0.2f) < 1e-6f);
    assert(std::abs(uniform[3] - 0.15f) < 1e-6f);
    return 0;
}
