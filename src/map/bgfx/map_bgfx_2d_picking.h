#ifndef MAP_BGFX_2D_PICKING_H
#define MAP_BGFX_2D_PICKING_H

#include "map/bgfx/map_bgfx_2d_mesh.h"
#include "map/core/map_network_types.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

// CPU picking for the bgfx 2D Map Monitor (click selection and hover).
//
// Same rules and radii as the former flat QRhi hitTest(), but against what
// bgfx actually draws: decluttered node positions, link icons at
// the middle of the link length, sizes from the continuous zoom (never the
// integer imagery level).
//
// Priority, as in QRhi:
//   1. icons (only while icons are shown): nearest within
//      max(12 px, icon size / 2 + 6 px);
//   2. nodes incl. demand points (junctions only while shown): nearest within
//      max(10 px, node radius + 5 px);
//   3. links: nearest segment within max(8 px, half link width + 5 px).
// Attachment lines are not pickable (QRhi neither); their demand point is.
//
// The work is a linear pass in screen space, done only on click and mouse
// move, never per frame.
namespace MapBgfx2dPicking
{
constexpr double MinimumIconHitRadiusPx = 12.0;
constexpr double IconHitPaddingPx = 6.0;
constexpr double MinimumNodeHitRadiusPx = 10.0;
constexpr double NodeHitPaddingPx = 5.0;
constexpr double MinimumLinkHitRadiusPx = 8.0;
constexpr double LinkHitPaddingPx = 5.0;

struct Options
{
    double icon_size_px = 24.0;
    double node_half_px = 6.0;
    double link_width_px = 3.0;
    bool show_icons = true;
    bool show_junctions = true;
    // mapHasIcon() in the application; injected so this stays Qt-GUI free.
    std::function<bool(InfrastructureEntity)> has_icon;
};

struct Hit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;

    bool isValid() const
    {
        return this->render_id != 0 && this->entity_type != InfrastructureEntity::Unknown;
    }
};

inline double screenDistance(const MapBgfx2d::ScreenPoint &a, const MapBgfx2d::ScreenPoint &b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

inline double segmentDistance(const MapBgfx2d::ScreenPoint &point,
                              const MapBgfx2d::ScreenPoint &start,
                              const MapBgfx2d::ScreenPoint &end)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length_squared = dx * dx + dy * dy;
    double along = 0.0;
    if (length_squared > 0.0)
        along = std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) / length_squared,
                           0.0, 1.0);
    return std::hypot(point.x - (start.x + dx * along), point.y - (start.y + dy * along));
}

inline Hit pick(const MapBgfx2dScene &scene, const MapBgfx2d::Camera &camera,
                const MapBgfx2d::ScreenPoint &position, const Options &options)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !(camera.world_pixels > 0.0))
        return Hit();

    // 1. Icons. Node icons sit on non-junction nodes, link icons on the
    //    middle of non-pipe links (MapBgfx2dMesh::prepare()).
    if (options.show_icons && options.has_icon)
    {
        const double icon_radius = std::max(MinimumIconHitRadiusPx,
                                            options.icon_size_px * 0.5 + IconHitPaddingPx);
        double best_distance = std::numeric_limits<double>::infinity();
        Hit best;
        for (const MapBgfx2dPoint &point : scene.networkNodes())
        {
            if (point.junction
                || !options.has_icon(static_cast<InfrastructureEntity>(point.entity_type)))
                continue;
            const double distance = screenDistance(
                position, MapBgfx2d::screenFromWorld(camera, point.position));
            if (distance <= icon_radius && distance < best_distance)
            {
                best_distance = distance;
                best = {point.render_id, static_cast<InfrastructureEntity>(point.entity_type)};
            }
        }
        for (const MapBgfx2dPolyline &polyline : scene.linkPolylines())
        {
            if (polyline.pipe
                || !options.has_icon(static_cast<InfrastructureEntity>(polyline.entity_type)))
                continue;
            MapBgfx2d::WorldPoint middle;
            if (!MapBgfx2dMesh::polylineMiddle(polyline, &middle))
                continue;
            const double distance = screenDistance(
                position, MapBgfx2d::screenFromWorld(camera, middle));
            if (distance <= icon_radius && distance < best_distance)
            {
                best_distance = distance;
                best = {polyline.render_id, static_cast<InfrastructureEntity>(polyline.entity_type)};
            }
        }
        if (best.isValid())
            return best;
    }

    // 2. Nodes, including demand points.
    {
        const double node_radius = std::max(MinimumNodeHitRadiusPx,
                                            options.node_half_px + NodeHitPaddingPx);
        double best_distance = node_radius;
        Hit best;
        for (const MapBgfx2dPoint &point : scene.networkNodes())
        {
            if (!options.show_junctions && point.junction)
                continue;
            const double distance = screenDistance(
                position, MapBgfx2d::screenFromWorld(camera, point.position));
            if (distance <= best_distance)
            {
                best_distance = distance;
                best = {point.render_id, static_cast<InfrastructureEntity>(point.entity_type)};
            }
        }
        if (best.isValid())
            return best;
    }

    // 3. Links.
    const double link_radius = std::max(MinimumLinkHitRadiusPx,
                                        options.link_width_px * 0.5 + LinkHitPaddingPx);
    double best_distance = link_radius;
    Hit best;
    for (const MapBgfx2dPolyline &polyline : scene.linkPolylines())
    {
        for (qsizetype index = 1; index < polyline.points.size(); ++index)
        {
            MapBgfx2d::ScreenPoint start;
            MapBgfx2d::ScreenPoint end;
            MapBgfx2d::screenSegment(camera, polyline.points[index - 1], polyline.points[index],
                                     &start, &end);
            const double distance = segmentDistance(position, start, end);
            if (distance <= best_distance)
            {
                best_distance = distance;
                best = {polyline.render_id, static_cast<InfrastructureEntity>(polyline.entity_type)};
            }
        }
    }
    return best;
}
}

#endif // MAP_BGFX_2D_PICKING_H
