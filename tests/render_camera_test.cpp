#include "map/bgfx/map_bgfx_three_view_camera.h"
#include <cassert>
#include <cmath>

int main()
{
    MapBgfxThreeViewCamera camera;
    camera.reset(8.0, 50.0, 12.0);
    const MapBgfx2d::Camera before = camera.camera(480.0, 320.0);
    const double screen_x = 120.0;
    const double screen_y = 90.0;
    const double anchor_x = before.center.x + (screen_x - 240.0) / before.world_pixels;
    const double anchor_y = before.center.y + (screen_y - 160.0) / before.world_pixels;
    camera.zoomAround(4.0, screen_x, screen_y, 480.0, 320.0);
    const MapBgfx2d::Camera after = camera.camera(480.0, 320.0);
    assert(std::abs(after.world_pixels / before.world_pixels - 2.0) < 1e-8);
    assert(std::abs(anchor_x - (after.center.x + (screen_x - 240.0) / after.world_pixels)) < 1e-8);
    assert(std::abs(anchor_y - (after.center.y + (screen_y - 160.0) / after.world_pixels)) < 1e-8);
    camera.pan(100.0, 40.0);
    assert(std::abs(camera.centerWorld().x - after.center.x) > 1e-8);
    assert(std::abs(camera.centerWorld().y - after.center.y) > 1e-8);
    return 0;
}
