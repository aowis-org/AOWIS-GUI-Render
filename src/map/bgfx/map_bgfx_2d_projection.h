#ifndef MAP_BGFX_2D_PROJECTION_H
#define MAP_BGFX_2D_PROJECTION_H

#include <algorithm>
#include <cmath>

// Backend-neutral, continuous Web Mercator coordinates. All stored geometry
// is zoom-independent; the camera changes only the view transformation.
// x wraps every 1.0 world, while y remains in the finite Mercator extent.
namespace MapBgfx2d
{
constexpr double MaximumMercatorLatitude = 85.05112878;
constexpr double DegreesToRadians = 0.017453292519943295769236907684886;
constexpr double Pi = 3.1415926535897932384626433832795;

struct WorldPoint
{
    double x = 0.0;
    double y = 0.0;
};

struct ScreenPoint
{
    double x = 0.0;
    double y = 0.0;
};

struct Camera
{
    WorldPoint center;
    double world_pixels = 256.0;
    double viewport_width = 1.0;
    double viewport_height = 1.0;
};

inline bool coordinateIsValid(double longitude_deg, double latitude_deg)
{
    return std::isfinite(longitude_deg) && std::isfinite(latitude_deg)
        && latitude_deg >= -90.0 && latitude_deg <= 90.0;
}

inline WorldPoint worldFromWgs84(double longitude_deg, double latitude_deg)
{
    // A Mercator world has period 1.0. fmod handles user-supplied longitude
    // beyond [-180,180] without losing continuity across the antimeridian.
    double normalized_lon = std::fmod(longitude_deg + 180.0, 360.0);
    if (normalized_lon < 0.0)
        normalized_lon += 360.0;
    const double latitude = std::clamp(latitude_deg,
        -MaximumMercatorLatitude, MaximumMercatorLatitude);
    const double sine = std::sin(latitude * DegreesToRadians);
    const double mercator_y = 0.5 - std::log((1.0 + sine) / (1.0 - sine)) / (4.0 * Pi);
    return {normalized_lon / 360.0, std::clamp(mercator_y, 0.0, 1.0)};
}

inline Camera cameraFor(double center_lon_deg, double center_lat_deg,
                        double continuous_zoom, double viewport_width,
                        double viewport_height)
{
    Camera camera;
    camera.center = worldFromWgs84(center_lon_deg, center_lat_deg);
    camera.world_pixels = 256.0 * std::exp2(std::clamp(continuous_zoom, -4.0, 24.0));
    camera.viewport_width = std::max(1.0, viewport_width);
    camera.viewport_height = std::max(1.0, viewport_height);
    return camera;
}

inline double wrapNearest(double world_x, double reference_x)
{
    return world_x + std::round(reference_x - world_x);
}

inline ScreenPoint screenFromWorld(const Camera &camera, const WorldPoint &point)
{
    return {
        camera.viewport_width * 0.5
            + (wrapNearest(point.x, camera.center.x) - camera.center.x)
                * camera.world_pixels,
        camera.viewport_height * 0.5
            + (point.y - camera.center.y) * camera.world_pixels
    };
}

// Preserve continuity of a geographic link crossing ±180° instead of
// independently wrapping both endpoints to opposite sides of the viewport.
inline void screenSegment(const Camera &camera,
                          const WorldPoint &start, const WorldPoint &end,
                          ScreenPoint *screen_start, ScreenPoint *screen_end)
{
    const double continuous_end_x = wrapNearest(end.x, start.x);
    const double midpoint_x = 0.5 * (start.x + continuous_end_x);
    const double shift = std::round(camera.center.x - midpoint_x);
    if (screen_start != nullptr)
    {
        *screen_start = {
            camera.viewport_width * 0.5
                + (start.x + shift - camera.center.x) * camera.world_pixels,
            camera.viewport_height * 0.5
                + (start.y - camera.center.y) * camera.world_pixels
        };
    }
    if (screen_end != nullptr)
    {
        *screen_end = {
            camera.viewport_width * 0.5
                + (continuous_end_x + shift - camera.center.x) * camera.world_pixels,
            camera.viewport_height * 0.5
                + (end.y - camera.center.y) * camera.world_pixels
        };
    }
}
}

#endif // MAP_BGFX_2D_PROJECTION_H
