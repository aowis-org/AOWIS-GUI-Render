#include "map/render/map_globe_flow_geometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(float first, float second, float tolerance = 1.0e-4f)
{
    return std::fabs(first - second) <= tolerance;
}

MapGlobeFlowPath pathForLength(float length_m)
{
    MapGlobeFlowPath path;
    path.render_id = 37;
    path.total_length_m = length_m;
    path.segments.append({QVector3D(0.0f, 0.0f, 0.0f),
                          QVector3D(0.0f, length_m, 0.0f), length_m});
    return path;
}
}

int main()
{
    const MapGlobeFlowFrame frame{
        6378137.0, 0.0, 0.0, 6378137.0, 6356752.314245179};
    const MapGlobeFlowSettings settings{3.0, 12, 4};
    const MapGlobeFlowPath path = pathForLength(10.0f);

    require(MapGlobeFlowGeometry::markerCount(path, settings) == 1,
            "single small pipe has one flow marker");
    std::vector<MapGlobeFlowStroke> strokes;
    const std::function<void(const MapGlobeFlowStroke &)> collect =
        [&strokes](const MapGlobeFlowStroke &stroke) { strokes.push_back(stroke); };
    MapGlobeFlowGeometry::emitStrokes(path, true, settings, frame, collect);
    require(strokes.size() == 4, "two mirrored chevrons emit four strokes");
    require(strokes[0].render_id == 37 && strokes[0].orientation == 1.0f,
            "forward orientation and entity id retained");
    require(strokes[2].orientation == -1.0f,
            "reverse orientation retained for GPU flow style selection");
    require(near(strokes[0].end.y(), 7.0f),
            "single pipe marker centered at half path length");
    require(near(strokes[0].end.x(), 4.0f / 3.0f, 1e-3f),
            "chevron receives terrain clearance in WGS84 up direction");
    require(near(strokes[0].start.z(), 1.6f),
            "chevron retains original perpendicular half width");
    require(near(strokes[0].half_width_px, 1.2f),
            "chevron stroke pixel width retained");

    strokes.clear();
    MapGlobeFlowGeometry::emitStrokes(path, false, settings, frame, collect);
    require(strokes.size() == 4 && near(strokes[0].end.y(), 5.0f),
            "single pump/valve marker sits at 30 percent instead of 50 percent");

    const MapGlobeFlowPath short_path = pathForLength(5.0f);
    require(MapGlobeFlowGeometry::markerCount(short_path, settings) == 0,
            "short link below 18 pixels has no marker");
    strokes.clear();
    MapGlobeFlowGeometry::emitStrokes(short_path, true, settings, frame, collect);
    require(strokes.empty(), "short link emits no geometry");

    const MapGlobeFlowPath long_path = pathForLength(1200.0f);
    require(MapGlobeFlowGeometry::markerCount(long_path, settings) == 32,
            "long links capped at 32 markers");
    strokes.clear();
    MapGlobeFlowGeometry::emitStrokes(long_path, true, settings, frame, collect);
    require(strokes.size() == 128, "capped flow generates 128 strokes (768 vertices)");

    MapGlobeFlowPath multi_path = pathForLength(10.0f);
    multi_path.segments.clear();
    multi_path.segments.append({QVector3D(0, 0, 0), QVector3D(0, 2, 0), 2.0f});
    multi_path.segments.append({QVector3D(0, 2, 0), QVector3D(0, 10, 0), 8.0f});
    strokes.clear();
    MapGlobeFlowGeometry::emitStrokes(multi_path, true, settings, frame, collect);
    require(strokes.size() == 4 && near(strokes[0].end.y(), 7.0f),
            "marker distance follows both path segments");

    MapGlobeFlowSettings bad_settings = settings;
    bad_settings.pixels_per_meter = std::numeric_limits<double>::quiet_NaN();
    require(MapGlobeFlowGeometry::markerCount(path, bad_settings) == 0,
            "NaN camera scale is rejected");
    MapGlobeFlowPath no_segments = path;
    no_segments.segments.clear();
    require(MapGlobeFlowGeometry::markerCount(no_segments, settings) == 0,
            "empty path is ignored");

    const QVector3D normal = MapGlobeFlowGeometry::ellipsoidNormalAt(
        QVector3D(), frame);
    require(near(normal.x(), 1.0f) && near(normal.y(), 0.0f),
            "equatorial ellipsoid normal is radial");
    const MapGlobeFlowFrame invalid_frame{};
    require(MapGlobeFlowGeometry::ellipsoidNormalAt(
        QVector3D(), invalid_frame).lengthSquared() == 0.0f,
        "invalid origin produces a zero normal");

    QVector3D basis_x, basis_y, basis_z;
    require(MapGlobeFlowGeometry::modelBasisAt(
        QVector3D(), frame, &basis_x, &basis_y, &basis_z),
        "tank/reservoir tangent basis is valid at equator");
    require(near(basis_z.x(), 1.0f)
            && near(QVector3D::dotProduct(basis_x, basis_z), 0.0f)
            && near(QVector3D::dotProduct(basis_y, basis_z), 0.0f),
            "model basis is orthogonal to surface normal");
    require(!MapGlobeFlowGeometry::modelBasisAt(
        QVector3D(), frame, nullptr, &basis_y, &basis_z),
        "null model basis outputs rejected");
    require(!MapGlobeFlowGeometry::modelBasisAt(
        QVector3D(), invalid_frame, &basis_x, &basis_y, &basis_z),
        "degenerate model tangent frame rejected");

    std::cout << "PASS: production Globe flow placement and tangent geometry\n";
    return EXIT_SUCCESS;
}
