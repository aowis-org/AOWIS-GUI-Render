#ifndef MAP_BGFX_PAN_REUSE_H
#define MAP_BGFX_PAN_REUSE_H

#include "map/bgfx/map_bgfx_2d_projection.h"

#include <QPointF>
#include <QSizeF>

#include <algorithm>
#include <cmath>

// Reuse of a screen-space draw list (MapBgfxHudCanvas) across pure pans.
//
// The editor overlay and the map labels are built in logical screen pixels
// from the 2D camera. While only the camera centre moves - same zoom, same
// viewport, same device pixel ratio - every point of such a list moves by
// the same screen displacement, so the list built for an earlier centre is
// still exact, just translated. Rebuilding it per pan frame (projection,
// culling, decluttering, icon and text lookups) is replaced by drawing the
// earlier list with a translation (MapBgfxHudGpu::draw()'s offset, a plain
// add while the vertices are copied into the transient buffer anyway).
//
// So that content scrolling in from the edges already exists, a list that is
// rebuilt because of a pan is built for a viewport grown by a margin on
// every side (same centre: the grown camera puts everything margin pixels
// further right and down); it is drawn shifted back by the margin, and
// reused until the pan has moved it by more than the margin, then rebuilt
// around the new centre. A list rebuilt for any other reason (content,
// zoom, resize) gets no margin: zoom animations and content updates cost
// exactly what they cost without this, only pans get cheaper.
//
// Draw offsets are rounded to the physical pixel grid and the margin is a
// whole number of physical pixels, so sprites that were snapped to the
// grid when built (icons, halo text, sampled with point filtering) stay on
// it when drawn translated.
//
// Usage per frame:
//   QPointF offset;
//   if (!content_changed && pan.reuse(camera, viewport, ratio, &offset))
//       draw(canvas, offset);
//   else {
//       const MapBgfxPanReuse::Build build = pan.begin(camera, viewport, ratio, allow_margin);
//       buildInto(canvas, build.camera, build.viewport);
//       draw(canvas, build.offset);
//   }
class MapBgfxPanReuse
{
public:
    // Margin on every side as a fraction of the larger viewport edge, and its
    // upper bound (logical px): about a fifth of the screen can be panned
    // before the next rebuild, for roughly twice the build area.
    static constexpr double MarginFraction = 0.2;
    static constexpr double MaximumMarginPx = 400.0;

    struct Build
    {
        // The frame's camera and viewport grown by margin on every side.
        MapBgfx2d::Camera camera;
        QSizeF viewport;
        // Logical px; 0: built for exactly this camera (no pan reuse beyond
        // an unchanged centre).
        double margin = 0.0;
        // Translation to draw the build with in this frame: (-margin, -margin).
        QPointF offset;
    };

    // The draw offset of the last build for this frame's camera; false when
    // it cannot be reused (nothing built, zoom / viewport / ratio changed or
    // the pan left the margin).
    bool reuse(const MapBgfx2d::Camera &camera, const QSizeF &viewport,
               double device_pixel_ratio, QPointF *offset) const
    {
        if (!this->valid || !sameView(camera, viewport, device_pixel_ratio))
            return false;
        // Content of the build moves with the map: opposite to the centre.
        const double dx = (this->built_center.x - camera.center.x) * camera.world_pixels;
        const double dy = (this->built_center.y - camera.center.y) * camera.world_pixels;
        if (std::abs(dx) > this->margin || std::abs(dy) > this->margin)
            return false;
        *offset = QPointF(snap(dx - this->margin, device_pixel_ratio),
                          snap(dy - this->margin, device_pixel_ratio));
        return true;
    }

    // Records a new build for this frame's camera and returns the camera and
    // viewport to build with. allow_margin: the content has no screen-anchored
    // parts (pointer positions, screen rectangles), so it may be drawn
    // translated. The margin is used only when the camera pans (the last
    // build was for the same zoom, viewport and ratio at another centre).
    Build begin(const MapBgfx2d::Camera &camera, const QSizeF &viewport,
                double device_pixel_ratio, bool allow_margin)
    {
        const bool panning = this->valid && sameView(camera, viewport, device_pixel_ratio)
            && (this->built_center.x != camera.center.x || this->built_center.y != camera.center.y);
        const double ratio = device_pixel_ratio > 0.0 ? device_pixel_ratio : 1.0;
        Build build;
        build.margin = allow_margin && panning
            ? std::ceil(std::min(MaximumMarginPx,
                                 MarginFraction * std::max(viewport.width(), viewport.height()))
                        * ratio) / ratio
            : 0.0;
        build.camera = camera;
        build.camera.viewport_width = camera.viewport_width + 2.0 * build.margin;
        build.camera.viewport_height = camera.viewport_height + 2.0 * build.margin;
        build.viewport = QSizeF(viewport.width() + 2.0 * build.margin,
                                viewport.height() + 2.0 * build.margin);
        build.offset = QPointF(-build.margin, -build.margin);

        this->valid = true;
        this->built_center = camera.center;
        this->built_world_pixels = camera.world_pixels;
        this->built_viewport = viewport;
        this->built_ratio = device_pixel_ratio;
        this->margin = build.margin;
        return build;
    }

private:
    bool sameView(const MapBgfx2d::Camera &camera, const QSizeF &viewport,
                  double device_pixel_ratio) const
    {
        return this->built_world_pixels == camera.world_pixels
            && this->built_viewport.width() == viewport.width()
            && this->built_viewport.height() == viewport.height()
            && this->built_ratio == device_pixel_ratio;
    }
    static double snap(double logical, double device_pixel_ratio)
    {
        const double ratio = device_pixel_ratio > 0.0 ? device_pixel_ratio : 1.0;
        return std::round(logical * ratio) / ratio;
    }

    bool valid = false;
    MapBgfx2d::WorldPoint built_center;
    double built_world_pixels = 0.0;
    QSizeF built_viewport;
    double built_ratio = 0.0;
    double margin = 0.0;
};

#endif // MAP_BGFX_PAN_REUSE_H
