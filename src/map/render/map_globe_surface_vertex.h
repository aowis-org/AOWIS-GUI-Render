#ifndef MAP_GLOBE_SURFACE_VERTEX_H
#define MAP_GLOBE_SURFACE_VERTEX_H

#include <cmath>
#include <cstdint>

// Vertex layouts of the prepared globe surface (MapGlobeSurfacePreparation).
// MapBgfxGlobeSurface uploads them as they are: its bgfx vertex layouts must
// match these fields and sizes.

// A unit surface normal (ECEF) packed into four bytes: component n is stored
// as round((n + 1) * 127.5); the GPU reads it as an unsigned normalized
// byte (n * 0.5 + 0.5) and unpacks it in the vertex shader. Precision about
// half a degree, plenty for hillshading. w is unused (padding).
struct MapGlobePackedNormal
{
    std::uint8_t x = 128;
    std::uint8_t y = 128;
    std::uint8_t z = 255;
    std::uint8_t w = 0;
};

inline std::uint8_t mapGlobePackNormalComponent(double unit_component)
{
    // Rounded by truncating the non-negative value + 0.5 (std::lround is a
    // library call per component: it made the 64-cell mesh build 3x slower).
    const double clamped = unit_component < -1.0 ? -1.0 : (unit_component > 1.0 ? 1.0 : unit_component);
    return std::uint8_t((clamped + 1.0) * 127.5 + 0.5);
}

inline MapGlobePackedNormal mapGlobePackNormal(double x, double y, double z)
{
    const double length = std::sqrt(x * x + y * y + z * z);
    if (!(length > 0.0))
        return MapGlobePackedNormal{};
    return MapGlobePackedNormal{mapGlobePackNormalComponent(x / length), mapGlobePackNormalComponent(y / length),
                                mapGlobePackNormalComponent(z / length), 0};
}

// Position relative to the render origin, imagery texture coordinates, the
// terrain's surface normal (hillshading).
struct MapGlobeSurfaceVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    MapGlobePackedNormal normal;
};

// Wireframe line vertex, relative to the render origin.
struct MapGlobeSurfaceWireframeVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

static_assert(sizeof(MapGlobeSurfaceVertex) == 24);
static_assert(sizeof(MapGlobeSurfaceWireframeVertex) == 12);

#endif // MAP_GLOBE_SURFACE_VERTEX_H
