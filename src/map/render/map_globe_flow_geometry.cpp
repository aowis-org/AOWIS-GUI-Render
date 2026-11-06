#include "map/render/map_globe_flow_geometry.h"

#include <cmath>

namespace
{
constexpr double MinimumLinkPixels = 18.0;
constexpr double SpacingPixels = 100.0;
constexpr double ChevronHalfWidthRatio = 0.4;
constexpr double StrokeWidthRatio = 0.2;
constexpr double MinimumElevationPixels = 4.0;
constexpr int MaximumMarkersPerLink = 32;
}

int MapGlobeFlowGeometry::markerCount(
    const MapGlobeFlowPath &path, const MapGlobeFlowSettings &settings)
{
    if (path.segments.isEmpty() || !std::isfinite(path.total_length_m)
        || path.total_length_m <= 0.0
        || !std::isfinite(settings.pixels_per_meter)
        || settings.pixels_per_meter <= 0.0
        || settings.flow_direction_size_px <= 0)
    {
        return 0;
    }

    const double screen_length_px = path.total_length_m * settings.pixels_per_meter;
    if (screen_length_px < MinimumLinkPixels)
        return 0;
    if (!std::isfinite(screen_length_px)
        || screen_length_px >= SpacingPixels * MaximumMarkersPerLink)
    {
        return MaximumMarkersPerLink;
    }
    return qBound(1, int(std::floor(screen_length_px / SpacingPixels)),
                  MaximumMarkersPerLink);
}

QVector3D MapGlobeFlowGeometry::ellipsoidNormalAt(
    const QVector3D &relative_ecef, const MapGlobeFlowFrame &frame)
{
    const double absolute_x = frame.origin_x + double(relative_ecef.x());
    const double absolute_y = frame.origin_y + double(relative_ecef.y());
    const double absolute_z = frame.origin_z + double(relative_ecef.z());
    const double equatorial_radius_squared =
        frame.equatorial_radius_m * frame.equatorial_radius_m;
    const double polar_radius_squared =
        frame.polar_radius_m * frame.polar_radius_m;
    const double normal_x = absolute_x / equatorial_radius_squared;
    const double normal_y = absolute_y / equatorial_radius_squared;
    const double normal_z = absolute_z / polar_radius_squared;
    const double normal_length = std::sqrt(
        normal_x * normal_x + normal_y * normal_y + normal_z * normal_z);
    if (!std::isfinite(normal_length) || normal_length <= 0.0)
        return QVector3D();

    return QVector3D(
        float(normal_x / normal_length),
        float(normal_y / normal_length),
        float(normal_z / normal_length));
}

bool MapGlobeFlowGeometry::modelBasisAt(
    const QVector3D &relative_ecef, const MapGlobeFlowFrame &frame,
    QVector3D *basis_x, QVector3D *basis_y, QVector3D *basis_z)
{
    if (basis_x == nullptr || basis_y == nullptr || basis_z == nullptr)
        return false;

    const QVector3D up = ellipsoidNormalAt(relative_ecef, frame);
    if (up.lengthSquared() <= 1e-8f)
        return false;

    QVector3D horizontal = QVector3D::crossProduct(up, QVector3D(0.0f, 0.0f, 1.0f));
    if (horizontal.lengthSquared() <= 1e-8f)
        horizontal = QVector3D::crossProduct(up, QVector3D(0.0f, 1.0f, 0.0f));
    if (horizontal.lengthSquared() <= 1e-8f)
        return false;

    horizontal.normalize();
    QVector3D tangent_y = QVector3D::crossProduct(horizontal, up);
    if (tangent_y.lengthSquared() <= 1e-8f)
        return false;
    tangent_y.normalize();

    // Preserve the original left-handed Globe model basis: it cancels the
    // mesh's flat-map reflection so back-face culling remains correct.
    *basis_x = horizontal;
    *basis_y = tangent_y;
    *basis_z = up;
    return true;
}

void MapGlobeFlowGeometry::emitStrokes(
    const MapGlobeFlowPath &path, bool is_pipe,
    const MapGlobeFlowSettings &settings, const MapGlobeFlowFrame &frame,
    const std::function<void(const MapGlobeFlowStroke &)> &emit_stroke)
{
    const int marker_count = markerCount(path, settings);
    if (marker_count == 0 || !emit_stroke)
        return;

    const double chevron_length_m = double(settings.flow_direction_size_px)
        / settings.pixels_per_meter;
    const double chevron_half_width_m = chevron_length_m * ChevronHalfWidthRatio;
    const double elevation_pixels = qMax(
        MinimumElevationPixels, double(settings.link_thickness_px) / 2.0 + 2.0);
    const float elevation_m = float(elevation_pixels / settings.pixels_per_meter);
    const double stroke_width_px = qMax(
        1.0, double(settings.flow_direction_size_px) * StrokeWidthRatio);
    const float half_stroke_px = float(stroke_width_px / 2.0);

    for (int marker_index = 0; marker_index < marker_count; ++marker_index)
    {
        double target_world_distance_m = path.total_length_m
            * double(marker_index + 1) / double(marker_count + 1);
        if (marker_count == 1 && !is_pipe)
            target_world_distance_m = path.total_length_m * 0.3;

        double traversed_world_distance_m = 0.0;
        for (const MapGlobeFlowSegment &segment : path.segments)
        {
            const QVector3D segment_vector = segment.end - segment.start;
            const double segment_world_length_m = double(segment.length_m);
            if (segment_world_length_m <= 0.0)
                continue;
            if (traversed_world_distance_m + segment_world_length_m
                < target_world_distance_m)
            {
                traversed_world_distance_m += segment_world_length_m;
                continue;
            }

            const double ratio = qBound(
                0.0,
                (target_world_distance_m - traversed_world_distance_m)
                    / segment_world_length_m,
                1.0);
            QVector3D center = segment.start + segment_vector * float(ratio);
            const QVector3D surface_normal = ellipsoidNormalAt(center, frame);
            if (surface_normal.lengthSquared() <= 0.0f)
                break;

            QVector3D direction = segment_vector
                - surface_normal * QVector3D::dotProduct(segment_vector, surface_normal);
            if (direction.lengthSquared() <= 1.0e-12f)
                break;
            direction.normalize();

            QVector3D normal = QVector3D::crossProduct(surface_normal, direction);
            if (normal.lengthSquared() <= 1.0e-12f)
                break;
            normal.normalize();

            center += surface_normal * elevation_m;
            for (const float orientation : {1.0f, -1.0f})
            {
                const QVector3D tip = center
                    + direction * float(orientation * chevron_length_m / 2.0);
                const QVector3D base = center
                    - direction * float(orientation * chevron_length_m / 2.0);
                const QVector3D tail_first = base
                    + normal * float(chevron_half_width_m);
                const QVector3D tail_second = base
                    - normal * float(chevron_half_width_m);

                emit_stroke({tail_first, tip, path.render_id, orientation, half_stroke_px});
                emit_stroke({tail_second, tip, path.render_id, orientation, half_stroke_px});
            }
            break;
        }
    }
}
