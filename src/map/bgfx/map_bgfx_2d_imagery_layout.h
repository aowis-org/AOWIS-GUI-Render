#ifndef MAP_BGFX_2D_IMAGERY_LAYOUT_H
#define MAP_BGFX_2D_IMAGERY_LAYOUT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

// Backend-neutral 2D imagery tile layout for the continuous Web Mercator
// camera. No Qt, no bgfx: unit-tested in tests/bgfx_2d_imagery_layout_test.cpp.
//
// * The imagery level is derived from the continuous camera zoom (rounded,
//   imageryZoomFor(); the camera has no integer zoom). It never feeds back
//   into the camera, and the quads are positioned with the continuous
//   scale, so zooming between two levels stretches the tiles smoothly
//   instead of stepping.
// * Quads are computed per frame on the CPU in double precision relative to
//   the camera centre, so there is no float precision issue at zoom 19+.
// * A tile that is not available yet is covered by (1) the nearest
//   available ancestor's matching sub-rectangle and (2) any available
//   children drawn on top. Zooming in shows the blurry parent, zooming out
//   shows the sharp children, never a hole while tiles stream in.
namespace MapBgfx2dImagery
{
constexpr int TileSizePixels = 256;
constexpr int MaximumAncestorLevels = 8;

struct TileId
{
    int zoom = 0;
    int x = 0; // wrapped into [0, 2^zoom)
    int y = 0;

    bool operator==(const TileId &other) const
    {
        return this->zoom == other.zoom && this->x == other.x && this->y == other.y;
    }
};

struct Quad
{
    TileId tile;          // texture to sample
    double left = 0.0;    // logical screen pixels
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    double u0 = 0.0;      // sub-rectangle of the texture (ancestor fallback)
    double v0 = 0.0;
    double u1 = 1.0;
    double v1 = 1.0;
};

struct Request
{
    TileId tile;
    int priority = 0; // squared tile distance from the centre tile; lower first
};

struct Camera
{
    double center_x = 0.5;    // normalized Web Mercator world [0,1)
    double center_y = 0.5;
    double zoom = 0.0;        // continuous zoom level
    double viewport_width = 1.0;  // logical pixels
    double viewport_height = 1.0;
};

struct Layout
{
    int imagery_zoom = 0;
    // Draw in order: ancestor fallbacks, child fallbacks, target tiles.
    std::vector<Quad> quads;
    // Missing target tiles to request, sorted by priority.
    std::vector<Request> requests;
    // Identity of the target tile set; changes only when the set changes.
    std::uint64_t layout_key = 0;
};

inline int imageryZoomFor(double continuous_zoom, int minimum_zoom, int maximum_zoom)
{
    if (!std::isfinite(continuous_zoom))
        return minimum_zoom;
    return std::clamp(int(std::lround(continuous_zoom)), minimum_zoom, maximum_zoom);
}

inline int wrapTileX(int x, int zoom)
{
    const int count = 1 << zoom;
    const int wrapped = x % count;
    return wrapped < 0 ? wrapped + count : wrapped;
}

using Availability = std::function<bool(const TileId &)>;

inline Layout buildLayout(const Camera &camera, int minimum_zoom, int maximum_zoom,
                          const Availability &available)
{
    Layout layout;
    if (!std::isfinite(camera.center_x) || !std::isfinite(camera.center_y)
        || !std::isfinite(camera.zoom) || camera.viewport_width <= 0.0
        || camera.viewport_height <= 0.0)
    {
        return layout;
    }

    const int zoom = imageryZoomFor(camera.zoom, minimum_zoom, maximum_zoom);
    layout.imagery_zoom = zoom;
    const int tile_count = 1 << zoom;
    // Logical screen pixels per normalized world unit at the continuous zoom.
    const double world_pixels = double(TileSizePixels) * std::exp2(camera.zoom);
    const double tile_pixels = world_pixels / double(tile_count);
    const double half_width = camera.viewport_width * 0.5;
    const double half_height = camera.viewport_height * 0.5;

    // Visible range in tile units of the imagery level, one tile of margin
    // so tiles entering the view during a pan are already requested.
    const double center_tile_x = camera.center_x * double(tile_count);
    const double center_tile_y = camera.center_y * double(tile_count);
    const int first_x = int(std::floor(center_tile_x - half_width / tile_pixels)) - 1;
    const int last_x = int(std::floor(center_tile_x + half_width / tile_pixels)) + 1;
    const int first_y = std::max(0, int(std::floor(center_tile_y - half_height / tile_pixels)) - 1);
    const int last_y = std::min(tile_count - 1,
        int(std::floor(center_tile_y + half_height / tile_pixels)) + 1);

    // Screen edge of tile boundary index i (virtual, unwrapped). Adjacent
    // tiles use the *same* expression for their shared edge, so there are
    // no cracks between quads at any fractional scale.
    const std::function<double(int)> edge_x = [&](int index)
    {
        return half_width + (double(index) - center_tile_x) * tile_pixels;
    };
    const std::function<double(int)> edge_y = [&](int index)
    {
        return half_height + (double(index) - center_tile_y) * tile_pixels;
    };

    std::vector<Quad> ancestor_quads;
    std::vector<Quad> child_quads;
    std::vector<Quad> target_quads;
    std::uint64_t key = 1469598103934665603ULL;
    const std::function<void(std::uint64_t)> mix = [&key](std::uint64_t value)
    {
        key ^= value;
        key *= 1099511628211ULL;
    };
    mix(std::uint64_t(zoom));
    mix(std::uint64_t(std::int64_t(first_x)));
    mix(std::uint64_t(std::int64_t(last_x)));
    mix(std::uint64_t(std::int64_t(first_y)));
    mix(std::uint64_t(std::int64_t(last_y)));

    const int center_index_x = int(std::floor(center_tile_x));
    const int center_index_y = int(std::floor(center_tile_y));

    for (int y = first_y; y <= last_y; ++y)
    {
        for (int virtual_x = first_x; virtual_x <= last_x; ++virtual_x)
        {
            const TileId tile{zoom, wrapTileX(virtual_x, zoom), y};
            Quad quad;
            quad.tile = tile;
            quad.left = edge_x(virtual_x);
            quad.right = edge_x(virtual_x + 1);
            quad.top = edge_y(y);
            quad.bottom = edge_y(y + 1);

            if (available(tile))
            {
                target_quads.push_back(quad);
                continue;
            }

            const int dx = virtual_x - center_index_x;
            const int dy = y - center_index_y;
            layout.requests.push_back({tile, dx * dx + dy * dy});

            // (1) Nearest available ancestor, sampled at this tile's sub-rect.
            for (int levels = 1; levels <= MaximumAncestorLevels && zoom - levels >= 0; ++levels)
            {
                const TileId ancestor{zoom - levels, tile.x >> levels, tile.y >> levels};
                if (!available(ancestor))
                    continue;
                const double span = 1.0 / double(1 << levels);
                Quad fallback = quad;
                fallback.tile = ancestor;
                fallback.u0 = double(tile.x - (ancestor.x << levels)) * span;
                fallback.v0 = double(tile.y - (ancestor.y << levels)) * span;
                fallback.u1 = fallback.u0 + span;
                fallback.v1 = fallback.v0 + span;
                ancestor_quads.push_back(fallback);
                break;
            }

            // (2) Available children, sharper than any ancestor (zoom-out).
            if (zoom + 1 <= maximum_zoom)
            {
                for (int child = 0; child < 4; ++child)
                {
                    const int cx = child & 1;
                    const int cy = child >> 1;
                    const TileId child_tile{zoom + 1, tile.x * 2 + cx, tile.y * 2 + cy};
                    if (!available(child_tile))
                        continue;
                    Quad child_quad;
                    child_quad.tile = child_tile;
                    const double mid_x = 0.5 * (quad.left + quad.right);
                    const double mid_y = 0.5 * (quad.top + quad.bottom);
                    child_quad.left = cx == 0 ? quad.left : mid_x;
                    child_quad.right = cx == 0 ? mid_x : quad.right;
                    child_quad.top = cy == 0 ? quad.top : mid_y;
                    child_quad.bottom = cy == 0 ? mid_y : quad.bottom;
                    child_quads.push_back(child_quad);
                }
            }
        }
    }

    std::stable_sort(layout.requests.begin(), layout.requests.end(),
        [](const Request &a, const Request &b) { return a.priority < b.priority; });

    layout.quads.reserve(ancestor_quads.size() + child_quads.size() + target_quads.size());
    layout.quads.insert(layout.quads.end(), ancestor_quads.begin(), ancestor_quads.end());
    layout.quads.insert(layout.quads.end(), child_quads.begin(), child_quads.end());
    layout.quads.insert(layout.quads.end(), target_quads.begin(), target_quads.end());
    layout.layout_key = key;
    return layout;
}
}

#endif // MAP_BGFX_2D_IMAGERY_LAYOUT_H
