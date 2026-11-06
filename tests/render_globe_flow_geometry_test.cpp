#include "map/render/map_globe_flow_geometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

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
}

int main()
{
    const MapGlobeFlowFrame frame{
        6378137.0, 0.0, 0.0, 6378137.0, 6356752.314245179};
    const MapGlobeFlowSettings settings{3.0, 12, 4};
    MapGlobeFlowPath path;
    path.render_id = 37;
    path.total_length_m = 10.0;
    path.segments.append({QVector3D(0.0f, 0.0f, 0.0f),
                          QVector3D(0.0f, 10.0f, 0.0f), 10.0f});

    QVector<MapGlobeFlowStroke> pipe_strokes;
    MapGlobeFlowGeometry::emitStrokes(
        path, true, settings, frame,
        [&pipe_strokes](const MapGlobeFlowStroke &stroke)
        {
            pipe_strokes.append(stroke);
        });
    require(pipe_strokes.size() == 4, "real Qt path emits both chevron directions");
    require(pipe_strokes[0].render_id == 37 && pipe_strokes[2].orientation == -1.0f,
            "render id and reverse flow orientation preserved");
    require(std::abs(pipe_strokes[0].end.y() - 7.0f) < 1.0e-4f,
            "pipe chevron lies at half the link length");

    QVector<MapGlobeFlowStroke> nonpipe_strokes;
    MapGlobeFlowGeometry::emitStrokes(
        path, false, settings, frame,
        [&nonpipe_strokes](const MapGlobeFlowStroke &stroke)
        {
            nonpipe_strokes.append(stroke);
        });
    require(nonpipe_strokes.size() == 4
                && std::abs(nonpipe_strokes[0].end.y() - 5.0f) < 1.0e-4f,
            "non-pipe chevron retains 30 percent offset");

    QVector3D basis_x, basis_y, basis_z;
    require(MapGlobeFlowGeometry::modelBasisAt(
        QVector3D(), frame, &basis_x, &basis_y, &basis_z),
        "real Qt Globe model tangent frame initializes");
    require(std::abs(basis_z.x() - 1.0f) < 1.0e-5f,
            "model orientation uses WGS84 up");

    std::cout << "PASS: Qt Globe flow geometry and model tangent frame\n";
    return EXIT_SUCCESS;
}
