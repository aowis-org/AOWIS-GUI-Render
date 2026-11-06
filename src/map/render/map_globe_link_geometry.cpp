#include "map/render/map_globe_link_geometry.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double PolarRadiusM = 6356752.314245179;
constexpr double MaximumChordSagittaM = 0.5;
constexpr int MaximumSubdivisionCount = 4096;
}

int MapGlobeLinkGeometry::curvedSegmentCount(double geodesic_distance_m)
{
    if (!std::isfinite(geodesic_distance_m) || geodesic_distance_m <= 0.0)
        return 1;

    const double chord_length_m = 2.0 * std::sqrt(std::max(
        0.0, 2.0 * PolarRadiusM * MaximumChordSagittaM
            - MaximumChordSagittaM * MaximumChordSagittaM));
    if (!std::isfinite(chord_length_m) || chord_length_m <= 0.0)
        return 1;

    // Clamp in floating-point before narrowing to int: large or malformed
    // distances must never overflow the subdivision counter.
    return int(std::clamp(std::ceil(geodesic_distance_m / chord_length_m),
                          1.0, double(MaximumSubdivisionCount)));
}

void MapGlobeLinkGeometry::emitCurvedSegments(
    const QVector3D &start, const QVector3D &end,
    double geodesic_distance_m,
    const std::function<bool(double, QVector3D *)> &sample_ecef,
    const std::function<void(const QVector3D &, const QVector3D &)> &receive_segment)
{
    if (!receive_segment)
        return;

    const int segment_count = curvedSegmentCount(geodesic_distance_m);
    QVector3D segment_start = start;
    for (int index = 1; index <= segment_count; ++index)
    {
        const double ratio = double(index) / double(segment_count);
        QVector3D segment_end = end;
        if (index < segment_count)
        {
            if (!sample_ecef || !sample_ecef(ratio, &segment_end))
                segment_end = start + (end - start) * float(ratio);
        }
        receive_segment(segment_start, segment_end);
        segment_start = segment_end;
    }
}

QVector3D MapGlobeLinkGeometry::widthDirection(
    const QVector3D &start, const QVector3D &end, const QVector3D &midpoint_up)
{
    const QVector3D segment_direction = end - start;
    if (segment_direction.lengthSquared() <= 1.0e-12f)
        return QVector3D();
    QVector3D width_direction = QVector3D::crossProduct(midpoint_up, segment_direction);
    if (width_direction.lengthSquared() <= 1.0e-12f)
        return QVector3D();
    width_direction.normalize();
    return width_direction;
}

void MapGlobeLinkGeometry::emitRibbonCorners(
    const QVector3D &start, const QVector3D &end,
    const QVector3D &width_direction,
    const std::function<void(const MapGlobeRibbonCorner &)> &receive_corner)
{
    if (!receive_corner)
        return;
    constexpr float corners[6][2] = {
        {0.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
        {0.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
    };
    for (int index = 0; index < 6; ++index)
        receive_corner({start, end, width_direction, corners[index][0], corners[index][1]});
}
