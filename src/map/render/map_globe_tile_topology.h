#ifndef MAP_GLOBE_TILE_TOPOLOGY_H
#define MAP_GLOBE_TILE_TOPOLOGY_H

#include <QtGlobal>
#include <QVector>
#include <cmath>

// Shared CPU terrain topology and skirts: no terrain repository, GUI model,
// bgfx handles or GeographicLib dependency. The cached 16-bit topology is
// shared by all tiles at a given DEM cell count and stays alive for the process.
// --- Globe surface tile layout ---------------------------------------------
// Every globe surface tile (terrain mesh, stand-in or flat) is its
// row-major (cells + 1)^2 grid followed by its skirt: the edge vertices
// again (top row, bottom row, left column, right column; cells + 1 each),
// lowered along the vertical by mapGlobeTileSkirtDepthM(). Neighbours drawn
// from the same DEM zoom share their edge vertices exactly (both lie on the
// same DEM samples, mapGlobeTerrainCellCount()); neighbours from different
// DEM zooms, a stand-in next to a final mesh or a flat tile next to relief
// disagree about it, and the skirt fills that crack and lies hidden below
// the neighbour everywhere else. The grid alone is the surface for rays.
inline int mapGlobeTileGridVertexCount(int cells) { return (cells + 1) * (cells + 1); }
inline int mapGlobeTileVertexCount(int cells) { return mapGlobeTileGridVertexCount(cells) + 4 * (cells + 1); }
inline int mapGlobeTileGridIndexCount(int cells) { return cells * cells * 6; }
inline int mapGlobeTileIndexCount(int cells) { return mapGlobeTileGridIndexCount(cells) + 4 * cells * 6; }

// Most cells per side of a globe surface tile (a whole DEM tile).
// Every source DEM tile currently has 64 cells per edge. The GUI verifies
// this shared rendering ABI against its dataset tile constant at compile time.
inline constexpr int MapGlobeTileMaximumCellCount = 64;

// The triangles of a tile with `cells` cells per side, as indices relative
// to its first vertex: the grid's cells row by row (two triangles each, so a
// grid row is one contiguous block of cells * 6 indices), then the skirt's
// walls. The topology depends on the cell count only, so every tile with
// that count shares it: the renderer draws a tile with this one index list
// and the tile's first vertex as base vertex, and window rebuilds write no
// indices at all. 16 bit: 64 cells have 4485 vertices. Built once per cell
// count, kept for the process (the GPU references it without a copy);
// empty for a count outside 1..MapGlobeTileMaximumCellCount. Thread-safe.
const QVector<quint16> &mapGlobeTileIndices(int cells);

// Skirt depth of a tile: the elevation range of its DEM plus one grid
// cell's ground length (at least 20 m), times the vertical exaggeration.
// Both neighbours' edges interpolate heights from within that range, so the
// bottom of whichever skirt hangs from the higher edge, lowered below the
// lowest height, reaches past the lower one; the cell length is the margin
// for DEM levels smoothed differently and for the flat tiles' chords.
double mapGlobeTileSkirtDepthM(int zoom, int tile_y, int cells, double vertical_exaggeration,
                               double elevation_range_m);

// Appends the skirt of the grid that starts at grid_first (positions
// relative to the render origin; the vertical is the geocentric direction,
// within a fraction of a degree of the ellipsoid normal).
template <typename Vertex>
void mapGlobeAppendTileSkirt(QVector<Vertex> *vertices, qsizetype grid_first, int cells, double depth_m,
                             double origin_x, double origin_y, double origin_z)
{
    const int width = cells + 1;
    const qsizetype skirt_first = vertices->size();
    vertices->resize(skirt_first + 4 * qsizetype(width));
    Vertex *skirt = vertices->data() + skirt_first;
    const Vertex *grid = vertices->constData() + grid_first;
    for (int edge = 0; edge < 4; ++edge)
    {
        for (int index = 0; index < width; ++index)
        {
            const int row = edge == 0 ? 0 : edge == 1 ? cells : index;
            const int column = edge == 2 ? 0 : edge == 3 ? cells : index;
            Vertex vertex = grid[row * width + column];
            const double x = origin_x + double(vertex.x);
            const double y = origin_y + double(vertex.y);
            const double z = origin_z + double(vertex.z);
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length > 0.0)
            {
                vertex.x = float(double(vertex.x) - x / length * depth_m);
                vertex.y = float(double(vertex.y) - y / length * depth_m);
                vertex.z = float(double(vertex.z) - z / length * depth_m);
            }
            skirt[edge * width + index] = vertex;
        }
    }
}

#endif // MAP_GLOBE_TILE_TOPOLOGY_H
