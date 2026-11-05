#ifndef MAP_BGFX_GLOBE_IMAGERY_H
#define MAP_BGFX_GLOBE_IMAGERY_H

#include "map/bgfx/map_bgfx_2d_imagery_layout.h"

#include <cstdint>

// Which imagery a bgfx globe surface tile samples (map_bgfx_globe_surface.h).
//
// The globe shares the 2D imagery atlas (MapBgfx2dImageryGpu). A tile whose
// own imagery is not resident yet shows the part of its nearest resident
// ancestor that covers it - the same fallback as the QRhi globe's
// provisional textures, but without compositing a CPU image: the tile's
// texture coordinates are mapped into a sub-rectangle of the ancestor's
// atlas cell (u_globe_tile in vs_globe_surface.sc).
//
// Pure math, unit-tested without a GPU.
namespace MapBgfxGlobeImagery
{
// A tile (or ancestor) and the part of it one surface tile covers, in the
// source tile's texture coordinates (0..1, v down like the atlas rows).
struct Source
{
    MapBgfx2dImagery::TileId tile;
    double u0 = 0.0;
    double v0 = 0.0;
    double u1 = 1.0;
    double v1 = 1.0;
};

// The ancestor levels_up levels above tile (0: tile itself) and the part of
// it covering tile.
inline Source ancestor(const MapBgfx2dImagery::TileId &tile, int levels_up)
{
    Source source;
    source.tile.zoom = tile.zoom - levels_up;
    source.tile.x = int(uint32_t(tile.x) >> levels_up);
    source.tile.y = int(uint32_t(tile.y) >> levels_up);
    const double span = double(1u << levels_up);
    const double local_x = double(uint32_t(tile.x) & ((1u << levels_up) - 1u));
    const double local_y = double(uint32_t(tile.y) & ((1u << levels_up) - 1u));
    source.u0 = local_x / span;
    source.v0 = local_y / span;
    source.u1 = (local_x + 1.0) / span;
    source.v1 = (local_y + 1.0) / span;
    return source;
}

// u_globe_tile for a source drawn from the atlas rectangle atlas_rect
// (u0, v0, u1, v1 of the source tile's texels): xy offset, zw scale, so
// atlas = offset + tile_uv * scale.
inline void tileUniform(const float (&atlas_rect)[4], const Source &source, float (&uniform)[4])
{
    const double width = double(atlas_rect[2]) - double(atlas_rect[0]);
    const double height = double(atlas_rect[3]) - double(atlas_rect[1]);
    uniform[0] = float(double(atlas_rect[0]) + width * source.u0);
    uniform[1] = float(double(atlas_rect[1]) + height * source.v0);
    uniform[2] = float(width * (source.u1 - source.u0));
    uniform[3] = float(height * (source.v1 - source.v0));
}
}

#endif // MAP_BGFX_GLOBE_IMAGERY_H
