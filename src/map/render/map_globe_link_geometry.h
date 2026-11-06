#ifndef MAP_GLOBE_LINK_GEOMETRY_H
#define MAP_GLOBE_LINK_GEOMETRY_H

#include <QVector3D>

#include <functional>

// CPU-only globe pipe tessellation. Host supplies geodesic/ECEF interpolation
// and entity-specific styling; Render owns segment subdivision and ribbon
// triangle topology. All positions are retained-origin-relative ECEF metres.
struct MapGlobeRibbonCorner
{
    QVector3D start;
    QVector3D end;
    QVector3D width_direction;
    float along = 0.0f;
    float side = 0.0f;
};

class MapGlobeLinkGeometry
{
public:
    // WGS84 polar-radius conservative bound: <= 0.5m chord sagitta;
    // preserve the original 4096 subdivision cap for very long links.
    static int curvedSegmentCount(double geodesic_distance_m);

    // Geodesic callback receives fractional distance [0,1] and writes the
    // interpolated ECEF point; false falls back to the original ECEF chord.
    // It is never invoked for an unsubdivided span. All segments are emitted
    // in order, including on sampling failure.
    static void emitCurvedSegments(
        const QVector3D &start, const QVector3D &end,
        double geodesic_distance_m,
        const std::function<bool(double, QVector3D *)> &sample_ecef,
        const std::function<void(const QVector3D &, const QVector3D &)> &receive_segment);

    // Host supplies the exact WGS84 local-frame up direction at segment
    // midpoint; this avoids duplicating GeographicLib/geodetic conversion.
    static QVector3D widthDirection(const QVector3D &start, const QVector3D &end,
                                    const QVector3D &midpoint_up);

    // Two triangles (six vertices) with the production GPU's corner order.
    static void emitRibbonCorners(
        const QVector3D &start, const QVector3D &end,
        const QVector3D &width_direction,
        const std::function<void(const MapGlobeRibbonCorner &)> &receive_corner);
};

#endif // MAP_GLOBE_LINK_GEOMETRY_H
