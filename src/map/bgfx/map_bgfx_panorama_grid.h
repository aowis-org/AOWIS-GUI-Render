#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

// Look-around panorama test mode (MapBgfxMapView::setPanorama): an
// equirectangular 360-degree image drawn instead of the map, through the
// same camera, frame pacing and presentation. It isolates the map's rendering
// cost from the motion of the view: if Look around is smooth over the
// panorama but not over the map, the map's frames are the bottleneck.
//
// The panorama is drawn with the existing imagery program (vs_imagery_2d /
// fs_imagery_2d: clip-space positions, one texture), so no shader binaries
// change. This header computes its geometry, without Qt or bgfx: a regular
// grid over the screen whose every vertex carries the panorama texture
// coordinate of its view ray. A cell spans about 1/96 of the screen width;
// interpolating the coordinate across it is far below a pixel off.
//
// Texture coordinates: u = 0.5 looks north, increasing clockwise (east at
// 0.75); v = 0 is the zenith, 0.5 the horizon, 1 the nadir. The sampler
// repeats in u; a cell across the image's seam gets its corners' u unwrapped
// to one side. Only a cell containing the zenith or nadir itself is smeared
// (a few pixels around that point).
struct MapPanoramaVertex
{
    float x = 0.0f; // clip space, -1 (left) .. 1 (right)
    float y = 0.0f; // clip space, 1 (top) .. -1 (bottom)
    float u = 0.0f;
    float v = 0.0f;
};

struct MapPanoramaView
{
    // View rays: forward + right * x + up * y for clip coordinates (x, y);
    // right and up are scaled by the half-extents of the view (tan(fov / 2),
    // right also by the aspect ratio). Any common frame (ECEF here).
    std::array<double, 3> forward{0.0, 1.0, 0.0};
    std::array<double, 3> right{1.0, 0.0, 0.0};
    std::array<double, 3> up{0.0, 0.0, 1.0};
    // Local axes at the eye in the same frame.
    std::array<double, 3> east{1.0, 0.0, 0.0};
    std::array<double, 3> north{0.0, 1.0, 0.0};
    std::array<double, 3> zenith{0.0, 0.0, 1.0};
    // Compass bearing (radians, clockwise from north) shown at u = 0.5.
    double image_center_bearing = 0.0;
};

namespace MapPanoramaGrid
{
constexpr int DefaultColumns = 96;
constexpr int DefaultRows = 54;

inline double dot(const std::array<double, 3> &a, const std::array<double, 3> &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Panorama coordinate of a view ray (any length > 0).
inline std::array<double, 2> texcoord(const MapPanoramaView &view, const std::array<double, 3> &ray)
{
    constexpr double pi = 3.14159265358979323846;
    const double east = dot(ray, view.east);
    const double north = dot(ray, view.north);
    const double up = dot(ray, view.zenith);
    const double horizontal = std::sqrt(east * east + north * north);
    const double bearing = std::atan2(east, north) - view.image_center_bearing;
    double u = 0.5 + bearing / (2.0 * pi);
    u -= std::floor(u);
    const double elevation = std::atan2(up, horizontal);
    return {u, 0.5 - elevation / pi};
}

// Two triangles per cell (six vertices), cells row by row from the top left.
inline void build(const MapPanoramaView &view, int columns, int rows, std::vector<MapPanoramaVertex> *vertices)
{
    vertices->clear();
    if (columns < 1 || rows < 1)
        return;
    const std::size_t stride = std::size_t(columns) + 1;
    std::vector<std::array<double, 2>> corners(stride * (std::size_t(rows) + 1));
    for (int row = 0; row <= rows; ++row)
    {
        const double y = 1.0 - 2.0 * double(row) / double(rows);
        for (int column = 0; column <= columns; ++column)
        {
            const double x = -1.0 + 2.0 * double(column) / double(columns);
            const std::array<double, 3> ray{
                view.forward[0] + view.right[0] * x + view.up[0] * y,
                view.forward[1] + view.right[1] * x + view.up[1] * y,
                view.forward[2] + view.right[2] * x + view.up[2] * y};
            corners[std::size_t(row) * stride + std::size_t(column)] = texcoord(view, ray);
        }
    }
    vertices->reserve(std::size_t(columns) * std::size_t(rows) * 6);
    for (int row = 0; row < rows; ++row)
    {
        const float top = float(1.0 - 2.0 * double(row) / double(rows));
        const float bottom = float(1.0 - 2.0 * double(row + 1) / double(rows));
        for (int column = 0; column < columns; ++column)
        {
            const float left = float(-1.0 + 2.0 * double(column) / double(columns));
            const float right = float(-1.0 + 2.0 * double(column + 1) / double(columns));
            const std::size_t index = std::size_t(row) * stride + std::size_t(column);
            std::array<std::array<double, 2>, 4> c = {
                corners[index], corners[index + 1], corners[index + stride + 1], corners[index + stride]};
            // Unwrap u across the seam: every corner within half a turn of
            // the first (the sampler repeats in u).
            for (std::size_t k = 1; k < 4; ++k)
            {
                if (c[k][0] - c[0][0] > 0.5)
                    c[k][0] -= 1.0;
                else if (c[0][0] - c[k][0] > 0.5)
                    c[k][0] += 1.0;
            }
            const MapPanoramaVertex top_left{left, top, float(c[0][0]), float(c[0][1])};
            const MapPanoramaVertex top_right{right, top, float(c[1][0]), float(c[1][1])};
            const MapPanoramaVertex bottom_right{right, bottom, float(c[2][0]), float(c[2][1])};
            const MapPanoramaVertex bottom_left{left, bottom, float(c[3][0]), float(c[3][1])};
            vertices->push_back(top_left);
            vertices->push_back(top_right);
            vertices->push_back(bottom_right);
            vertices->push_back(top_left);
            vertices->push_back(bottom_right);
            vertices->push_back(bottom_left);
        }
    }
}
}
