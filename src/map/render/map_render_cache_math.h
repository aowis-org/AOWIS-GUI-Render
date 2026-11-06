#ifndef MAP_RENDER_CACHE_MATH_H
#define MAP_RENDER_CACHE_MATH_H

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QtGlobal>

namespace MapRenderCacheMath
{
constexpr int ReferenceZoom = 18;

QSize boundedCacheLogicalSize(const QSize &viewport_size, qreal device_pixel_ratio);
QRectF centeredWorldRect(const QPointF &center, const QSize &logical_size,
                         qreal reference_scale);
bool coverageCoversView(const QRectF &coverage_world_bounds,
                        const QRectF &view_world_bounds,
                        qreal reference_scale,
                        bool include_rebuild_margin);
}

#endif // MAP_RENDER_CACHE_MATH_H
