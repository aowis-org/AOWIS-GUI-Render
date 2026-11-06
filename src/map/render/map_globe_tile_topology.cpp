#include "map/render/map_globe_tile_topology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

double mapGlobeTileSkirtDepthM(int zoom, int tile_y, int cells, double vertical_exaggeration,
                               double elevation_range_m)
{
    constexpr double MinimumSkirtDepthM = 20.0;
    // Exact Web Mercator inverse at the tile centre (radians); the only
    // ellipsoid constant needed by this topological depth rule is WGS84 a.
    constexpr double Pi = 3.141592653589793238462643383279502884;
    constexpr double Wgs84EquatorialRadiusM = 6378137.0;
    const double tiles_per_axis = std::ldexp(1.0, zoom);
    const double mercator_y = Pi - 2.0 * Pi * (double(tile_y) + 0.5) / tiles_per_axis;
    const double latitude_rad = std::atan(std::sinh(mercator_y));
    const double tile_width_m = 2.0 * Pi * Wgs84EquatorialRadiusM
        * std::cos(latitude_rad) / tiles_per_axis;
    const double cell_length_m = tile_width_m / double(std::max(1, cells));
    return (elevation_range_m + std::max(MinimumSkirtDepthM, cell_length_m))
        * std::max(1.0, vertical_exaggeration);
}

const QVector<quint16> &mapGlobeTileIndices(int cells)
{
    static const QVector<quint16> empty;
    if (cells < 1 || cells > MapGlobeTileMaximumCellCount)
        return empty;

    // Built under the lock, never changed afterwards: a returned reference
    // stays valid (and its data at the same address) for the process.
    static std::mutex mutex;
    static std::array<QVector<quint16>, MapGlobeTileMaximumCellCount + 1> topologies;
    const std::lock_guard<std::mutex> lock(mutex);
    QVector<quint16> &indices = topologies[std::size_t(cells)];
    if (!indices.isEmpty())
        return indices;

    const quint16 grid_width = quint16(cells + 1);
    indices.resize(mapGlobeTileIndexCount(cells));
    quint16 *target = indices.data();
    for (int row = 0; row < cells; ++row)
    {
        for (int column = 0; column < cells; ++column)
        {
            const quint16 p00 = quint16(row * grid_width + column);
            const quint16 p10 = quint16(p00 + 1);
            const quint16 p01 = quint16(p00 + grid_width);
            const quint16 p11 = quint16(p01 + 1);
            *target++ = p00;
            *target++ = p01;
            *target++ = p10;
            *target++ = p10;
            *target++ = p01;
            *target++ = p11;
        }
    }

    // Skirt: one quad per edge segment between the grid edge vertices g0, g1
    // and their lowered copies s0, s1 (mapGlobeAppendTileSkirt() order: top
    // row, bottom row, left column, right column). The surface state culls
    // no faces, so the winding does not matter.
    const int skirt_first = mapGlobeTileGridVertexCount(cells);
    for (int edge = 0; edge < 4; ++edge)
    {
        for (int index = 0; index < cells; ++index)
        {
            int g0 = 0;
            int g1 = 0;
            if (edge == 0)
            {
                g0 = index;
                g1 = g0 + 1;
            }
            else if (edge == 1)
            {
                g0 = cells * grid_width + index;
                g1 = g0 + 1;
            }
            else
            {
                const int column = edge == 2 ? 0 : cells;
                g0 = index * grid_width + column;
                g1 = g0 + grid_width;
            }
            const int s0 = skirt_first + edge * grid_width + index;
            const int s1 = s0 + 1;
            *target++ = quint16(g0);
            *target++ = quint16(s0);
            *target++ = quint16(g1);
            *target++ = quint16(g1);
            *target++ = quint16(s0);
            *target++ = quint16(s1);
        }
    }
    return indices;
}

