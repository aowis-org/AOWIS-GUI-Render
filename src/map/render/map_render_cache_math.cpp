#include "map/render/map_render_cache_math.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
constexpr qreal CacheOverscanFactor = 3.0;
constexpr int CacheMaximumPhysicalDimension = 4096;
constexpr qint64 CacheMaximumPhysicalArea = 8LL * 1024LL * 1024LL;
constexpr qreal CacheRebuildEdge = 256.0;
}

QSize MapRenderCacheMath::boundedCacheLogicalSize(
    const QSize &viewport_size, qreal device_pixel_ratio)
{
    if (!viewport_size.isValid())
        return QSize();

    const qreal bounded_device_pixel_ratio = qMax<qreal>(1.0, device_pixel_ratio);
    const qreal viewport_physical_area = qMax<qreal>(1.0,
        viewport_size.width() * bounded_device_pixel_ratio *
        viewport_size.height() * bounded_device_pixel_ratio);
    const qreal maximum_factor = std::min({
        CacheOverscanFactor,
        CacheMaximumPhysicalDimension /
            qMax<qreal>(1.0, viewport_size.width() * bounded_device_pixel_ratio),
        CacheMaximumPhysicalDimension /
            qMax<qreal>(1.0, viewport_size.height() * bounded_device_pixel_ratio),
        std::sqrt(CacheMaximumPhysicalArea / viewport_physical_area)
    });
    const qreal factor = qMax<qreal>(1.0, maximum_factor);
    return QSize(
        qMax(1, qFloor(viewport_size.width() * factor)),
        qMax(1, qFloor(viewport_size.height() * factor)));
}

QRectF MapRenderCacheMath::centeredWorldRect(
    const QPointF &center, const QSize &logical_size, qreal reference_scale)
{
    if (!logical_size.isValid() || reference_scale <= 0.0)
        return QRectF();

    return QRectF(
        center.x() - logical_size.width() / (2.0 * reference_scale),
        center.y() - logical_size.height() / (2.0 * reference_scale),
        logical_size.width() / reference_scale,
        logical_size.height() / reference_scale);
}

bool MapRenderCacheMath::coverageCoversView(
    const QRectF &coverage_world_bounds,
    const QRectF &view_world_bounds,
    qreal reference_scale,
    bool include_rebuild_margin)
{
    if (coverage_world_bounds.isEmpty() || reference_scale <= 0.0)
        return false;
    if (!include_rebuild_margin)
        return coverage_world_bounds.contains(view_world_bounds);

    const qreal horizontal_overscan = qMax<qreal>(0.0,
        (coverage_world_bounds.width() - view_world_bounds.width()) / 2.0);
    const qreal vertical_overscan = qMax<qreal>(0.0,
        (coverage_world_bounds.height() - view_world_bounds.height()) / 2.0);
    const qreal horizontal_safety = std::min(
        CacheRebuildEdge / reference_scale, horizontal_overscan / 2.0);
    const qreal vertical_safety = std::min(
        CacheRebuildEdge / reference_scale, vertical_overscan / 2.0);
    const QRectF required_bounds = view_world_bounds.adjusted(
        -horizontal_safety, -vertical_safety,
        horizontal_safety, vertical_safety);
    return coverage_world_bounds.contains(required_bounds);
}
