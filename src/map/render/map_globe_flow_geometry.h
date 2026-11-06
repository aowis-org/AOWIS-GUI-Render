#ifndef MAP_GLOBE_FLOW_GEOMETRY_H
#define MAP_GLOBE_FLOW_GEOMETRY_H

#include <QtGlobal>
#include <QVector>
#include <QVector3D>

#include <functional>

// Camera-scale-dependent Globe chevrons are CPU scene geometry, not bgfx
// resources. No hydraulic MODEL, GPU state, or host scene types enter here.
// Paths and positions are already expressed in the network's retained ECEF
// frame. The host supplies only the pipe/non-pipe placement distinction.
struct MapGlobeFlowSegment
{
    QVector3D start;
    QVector3D end;
    float length_m = 0.0f;
};

struct MapGlobeFlowPath
{
    quint32 render_id = 0;
    QVector<MapGlobeFlowSegment> segments;
    double total_length_m = 0.0;
};

struct MapGlobeFlowFrame
{
    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;
    double equatorial_radius_m = 6378137.0;
    double polar_radius_m = 6356752.314245179;
};

struct MapGlobeFlowSettings
{
    double pixels_per_meter = 0.0;
    int flow_direction_size_px = 0;
    int link_thickness_px = 0;
};

struct MapGlobeFlowStroke
{
    QVector3D start;
    QVector3D end;
    quint32 render_id = 0;
    float orientation = 0.0f;
    float half_width_px = 0.0f;
};

class MapGlobeFlowGeometry
{
public:
    // Four two-sided chevron strokes, six shader vertices per stroke.
    static int markerCount(const MapGlobeFlowPath &path,
                           const MapGlobeFlowSettings &settings);
    static void emitStrokes(
        const MapGlobeFlowPath &path, bool is_pipe,
        const MapGlobeFlowSettings &settings, const MapGlobeFlowFrame &frame,
        const std::function<void(const MapGlobeFlowStroke &)> &emit_stroke);

    // The same ellipsoid normal is shared by chevron placement and 3D model
    // orientation, avoiding two subtly divergent Globe tangent frames.
    static QVector3D ellipsoidNormalAt(const QVector3D &relative_ecef,
                                      const MapGlobeFlowFrame &frame);
    static bool modelBasisAt(const QVector3D &relative_ecef,
                             const MapGlobeFlowFrame &frame,
                             QVector3D *basis_x, QVector3D *basis_y,
                             QVector3D *basis_z);
};

#endif // MAP_GLOBE_FLOW_GEOMETRY_H
