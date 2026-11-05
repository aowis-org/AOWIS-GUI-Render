#ifndef MAP_BGFX_THREE_VIEW_CAMERA_H
#define MAP_BGFX_THREE_VIEW_CAMERA_H

#include "map/bgfx/map_bgfx_2d_projection.h"

#include <algorithm>
#include <cmath>

// Independent camera for a secondary map viewport. All dimensions are logical
// pixels; the native swapchain uses the device-pixel-ratio adjusted dimensions.
// This never changes the MapModel backing the existing Maps workspace.
class MapBgfxThreeViewCamera final
{
public:
    void reset(double longitude_deg, double latitude_deg, double continuous_zoom)
    {
        this->center = MapBgfx2d::worldFromWgs84(longitude_deg, latitude_deg);
        this->zoom_level = std::clamp(continuous_zoom, -4.0, 24.0);
    }

    MapBgfx2d::Camera camera(double width, double height) const
    {
        MapBgfx2d::Camera result;
        result.center = this->center;
        result.world_pixels = 256.0 * std::exp2(this->zoom_level);
        result.viewport_width = std::max(1.0, width);
        result.viewport_height = std::max(1.0, height);
        return result;
    }

    void pan(double delta_x, double delta_y)
    {
        const double pixels = 256.0 * std::exp2(this->zoom_level);
        this->center.x -= delta_x / pixels;
        this->center.y = std::clamp(this->center.y - delta_y / pixels, 0.0, 1.0);
        this->center.x -= std::floor(this->center.x);
    }

    void zoomAround(double steps, double pointer_x, double pointer_y,
                    double viewport_width, double viewport_height)
    {
        const double next = std::clamp(this->zoom_level + steps * 0.25, -4.0, 24.0);
        const double old_pixels = 256.0 * std::exp2(this->zoom_level);
        const double new_pixels = 256.0 * std::exp2(next);
        const double offset_x = pointer_x - viewport_width * 0.5;
        const double offset_y = pointer_y - viewport_height * 0.5;
        // Keep the geographic coordinate under the wheel pointer fixed.
        this->center.x += offset_x * (1.0 / old_pixels - 1.0 / new_pixels);
        this->center.y = std::clamp(
            this->center.y + offset_y * (1.0 / old_pixels - 1.0 / new_pixels), 0.0, 1.0);
        this->center.x -= std::floor(this->center.x);
        this->zoom_level = next;
    }

    MapBgfx2d::WorldPoint centerWorld() const { return this->center; }
    double zoom() const { return this->zoom_level; }

private:
    MapBgfx2d::WorldPoint center;
    double zoom_level = 0.0;
};

#endif // MAP_BGFX_THREE_VIEW_CAMERA_H
