#include "map/bgfx/map_bgfx_panorama_grid.h"

#include <cmath>
#include <iostream>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Panorama grid failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

namespace
{
constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tolerance = 1e-9)
{
    return std::fabs(a - b) <= tolerance;
}

// Camera looking along bearing (clockwise from north) and elevation, in an
// east/north/zenith frame; half-extents tan(fov / 2).
MapPanoramaView looking(double bearing_deg, double elevation_deg, double half_width, double half_height)
{
    const double b = bearing_deg * kPi / 180.0;
    const double e = elevation_deg * kPi / 180.0;
    MapPanoramaView view;
    view.forward = {std::sin(b) * std::cos(e), std::cos(b) * std::cos(e), std::sin(e)};
    const std::array<double, 3> right{std::cos(b), -std::sin(b), 0.0};
    const std::array<double, 3> up{-std::sin(b) * std::sin(e), -std::cos(b) * std::sin(e), std::cos(e)};
    view.right = {right[0] * half_width, right[1] * half_width, right[2] * half_width};
    view.up = {up[0] * half_height, up[1] * half_height, up[2] * half_height};
    return view;
}
}

int main()
{
    MapPanoramaView view;
    // North on the horizon is the image centre; east a quarter turn right.
    std::array<double, 2> t = MapPanoramaGrid::texcoord(view, {0.0, 1.0, 0.0});
    CHECK(near(t[0], 0.5) && near(t[1], 0.5));
    t = MapPanoramaGrid::texcoord(view, {1.0, 0.0, 0.0});
    CHECK(near(t[0], 0.75) && near(t[1], 0.5));
    t = MapPanoramaGrid::texcoord(view, {-1.0, 0.0, 0.0});
    CHECK(near(t[0], 0.25));
    // 45 degrees up: a quarter of the way from the horizon to the top.
    t = MapPanoramaGrid::texcoord(view, {0.0, 1.0, 1.0});
    CHECK(near(t[1], 0.25));
    t = MapPanoramaGrid::texcoord(view, {0.0, 3.0, -3.0});
    CHECK(near(t[1], 0.75));
    // The ray's length does not matter.
    CHECK(near(MapPanoramaGrid::texcoord(view, {2.0, 2.0, 0.0})[0],
               MapPanoramaGrid::texcoord(view, {0.5, 0.5, 0.0})[0]));
    // A rotated image: its centre bearing at u = 0.5.
    view.image_center_bearing = kPi / 2.0;
    CHECK(near(MapPanoramaGrid::texcoord(view, {1.0, 0.0, 0.0})[0], 0.5));
    view.image_center_bearing = 0.0;

    // Grid: six vertices per cell, covering clip space exactly.
    std::vector<MapPanoramaVertex> vertices;
    MapPanoramaGrid::build(looking(30.0, 10.0, 0.6, 0.35), 8, 4, &vertices);
    CHECK(vertices.size() == 8u * 4u * 6u);
    CHECK(vertices.front().x == -1.0f && vertices.front().y == 1.0f);
    CHECK(vertices[vertices.size() - 1].x == -1.0f + 2.0f * 7.0f / 8.0f && vertices.back().y == -1.0f);
    // The screen centre is a grid corner here: the forward ray's coordinate.
    const MapPanoramaView centred = looking(30.0, 10.0, 0.6, 0.35);
    const std::array<double, 2> forward = MapPanoramaGrid::texcoord(centred, centred.forward);
    CHECK(near(forward[0], 0.5 + 30.0 / 360.0) && near(forward[1], 0.5 - 10.0 / 180.0));
    bool found = false;
    for (const MapPanoramaVertex &vertex : vertices)
        found = found || (vertex.x == 0.0f && vertex.y == 0.0f
                          && near(vertex.u, forward[0], 1e-6) && near(vertex.v, forward[1], 1e-6));
    CHECK(found);
    // Left of the screen is anticlockwise: smaller u (no seam here).
    CHECK(vertices.front().u < float(forward[0]));

    // Looking south the image seam (u 0 / 1) crosses the screen: each cell's
    // corners stay within half a turn, so no cell spans the whole image
    // (15 columns: the seam falls inside a cell, not on a grid line).
    MapPanoramaGrid::build(looking(180.0, 0.0, 0.6, 0.35), 15, 8, &vertices);
    bool seam_seen = false;
    for (std::size_t cell = 0; cell < vertices.size(); cell += 6)
    {
        float low = vertices[cell].u;
        float high = vertices[cell].u;
        for (std::size_t k = 0; k < 6; ++k)
        {
            low = std::min(low, vertices[cell + k].u);
            high = std::max(high, vertices[cell + k].u);
            seam_seen = seam_seen || vertices[cell + k].u < 0.0f || vertices[cell + k].u > 1.0f;
        }
        CHECK(high - low < 0.1f);
    }
    CHECK(seam_seen);

    // Degenerate grids produce nothing.
    MapPanoramaGrid::build(view, 0, 4, &vertices);
    CHECK(vertices.empty());
    std::cout << "panorama grid: ok\n";
    return 0;
}
