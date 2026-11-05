#include "map/bgfx/map_bgfx_2d_projection.h"

#include <cmath>
#include <cstdio>

namespace
{
bool near(double a, double b, double tolerance = 1e-7)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < tolerance;
}

bool check(bool result, const char *description)
{
    if (!result)
        std::fprintf(stderr, "bgfx_2d_projection: %s\n", description);
    return result;
}
}

int main()
{
    const MapBgfx2d::WorldPoint origin = MapBgfx2d::worldFromWgs84(0.0, 0.0);
    if (!check(near(origin.x, 0.5) && near(origin.y, 0.5), "Mercator origin"))
        return 1;

    const MapBgfx2d::Camera camera = MapBgfx2d::cameraFor(18.19, 11.98, 14.25, 1024, 768);
    const MapBgfx2d::ScreenPoint center = MapBgfx2d::screenFromWorld(
        camera, MapBgfx2d::worldFromWgs84(18.19, 11.98));
    if (!check(near(center.x, 512.0) && near(center.y, 384.0), "camera center"))
        return 1;

    const MapBgfx2d::WorldPoint location = MapBgfx2d::worldFromWgs84(18.191, 11.979);
    const MapBgfx2d::ScreenPoint first = MapBgfx2d::screenFromWorld(camera, location);
    const MapBgfx2d::Camera zoom = MapBgfx2d::cameraFor(18.19, 11.98, 15.25, 1024, 768);
    const MapBgfx2d::ScreenPoint second = MapBgfx2d::screenFromWorld(zoom, location);
    if (!check(near(second.x - 512.0, 2.0 * (first.x - 512.0))
                    && near(second.y - 384.0, 2.0 * (first.y - 384.0)),
               "continuous zoom scaling"))
        return 1;

    const MapBgfx2d::Camera before_boundary = MapBgfx2d::cameraFor(
        18.19, 11.98, 15.4999, 1024, 768);
    const MapBgfx2d::Camera after_boundary = MapBgfx2d::cameraFor(
        18.19, 11.98, 15.5001, 1024, 768);
    const MapBgfx2d::ScreenPoint a = MapBgfx2d::screenFromWorld(before_boundary, location);
    const MapBgfx2d::ScreenPoint b = MapBgfx2d::screenFromWorld(after_boundary, location);
    if (!check(std::hypot(a.x - b.x, a.y - b.y) < 1.0,
               "no discontinuity across integer tile zoom selection"))
        return 1;

    const MapBgfx2d::Camera dateline = MapBgfx2d::cameraFor(179.99, 0.0, 12.0, 800, 600);
    const MapBgfx2d::ScreenPoint wrapped = MapBgfx2d::screenFromWorld(
        dateline, MapBgfx2d::worldFromWgs84(-179.99, 0.0));
    if (!check(std::abs(wrapped.x - 400.0) < 100.0, "wrapped point near dateline"))
        return 1;
    MapBgfx2d::ScreenPoint start;
    MapBgfx2d::ScreenPoint end;
    MapBgfx2d::screenSegment(dateline,
        MapBgfx2d::worldFromWgs84(179.98, 0.0),
        MapBgfx2d::worldFromWgs84(-179.98, 0.0), &start, &end);
    if (!check(std::abs(start.x - end.x) < 200.0,
               "line segment stays continuous across dateline"))
        return 1;

    const MapBgfx2d::WorldPoint north = MapBgfx2d::worldFromWgs84(0.0, 90.0);
    const MapBgfx2d::WorldPoint south = MapBgfx2d::worldFromWgs84(0.0, -90.0);
    if (!check(north.y >= 0.0 && north.y < 1e-6
                    && south.y <= 1.0 && south.y > 1.0 - 1e-6,
               "Mercator latitude clamps to finite bounds"))
        return 1;

    std::puts("Continuous 2D bgfx camera/projection tests passed");
    return 0;
}
