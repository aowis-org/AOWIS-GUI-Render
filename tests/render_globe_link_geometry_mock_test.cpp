#include "map/render/map_globe_link_geometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
void require(bool ok, const char *message)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }
}

int main()
{
    require(MapGlobeLinkGeometry::curvedSegmentCount(0.0) == 1, "zero span");
    require(MapGlobeLinkGeometry::curvedSegmentCount(-10.0) == 1, "negative span");
    require(MapGlobeLinkGeometry::curvedSegmentCount(std::numeric_limits<double>::quiet_NaN()) == 1,
            "invalid span");
    require(MapGlobeLinkGeometry::curvedSegmentCount(5000.0) == 1,
            "short local pipe remains one segment");
    require(MapGlobeLinkGeometry::curvedSegmentCount(6000.0) == 2,
            "curved earth long link subdivision");
    require(MapGlobeLinkGeometry::curvedSegmentCount(1.0e12) == 4096,
            "extreme geodesic span clamped before integer conversion");
    const QVector3D start(0.0f, 0.0f, 0.0f);
    const QVector3D end(12.0f, 0.0f, 0.0f);
    const QVector3D width = MapGlobeLinkGeometry::widthDirection(
        start, end, QVector3D(0.0f, 0.0f, 1.0f));
    require(near(width.x(), 0.0f) && near(width.y(), 1.0f), "width lies in tangent plane");
    require(MapGlobeLinkGeometry::widthDirection(start, start, QVector3D(0,0,1))
            .lengthSquared() == 0.0f, "degenerate ribbon has zero width");
    require(MapGlobeLinkGeometry::widthDirection(start, end, QVector3D(1,0,0))
            .lengthSquared() == 0.0f, "parallel up/edge has zero width");

    std::vector<MapGlobeRibbonCorner> ribbon;
    MapGlobeLinkGeometry::emitRibbonCorners(start, end, width,
        [&](const MapGlobeRibbonCorner &corner) { ribbon.push_back(corner); });
    require(ribbon.size() == 6, "ribbon has six vertices");
    const float expected_along[6] = {0,1,1,0,1,0};
    const float expected_side[6] = {-1,-1,1,-1,1,1};
    for (int i = 0; i < 6; ++i)
    {
        require(ribbon[i].along == expected_along[i] && ribbon[i].side == expected_side[i],
                "production triangle winding and corner order");
        require(ribbon[i].start.x() == 0.0f && ribbon[i].end.x() == 12.0f,
                "ribbon ECEF endpoints unchanged");
    }

    int sample_calls = 0;
    std::vector<QVector3D> starts;
    std::vector<QVector3D> ends;
    const std::function<void(const QVector3D &, const QVector3D &)> receive =
        [&](const QVector3D &a, const QVector3D &b) {
            starts.push_back(a); ends.push_back(b);
        };
    MapGlobeLinkGeometry::emitCurvedSegments(start, end, 5000.0,
        [&](double, QVector3D *) { ++sample_calls; return false; }, receive);
    require(sample_calls == 0 && starts.size() == 1, "no geodesic query for local link");
    require(starts[0].x() == 0.0f && ends[0].x() == 12.0f,
            "short link endpoints preserved");

    starts.clear(); ends.clear();
    MapGlobeLinkGeometry::emitCurvedSegments(start, end, 6000.0,
        [&](double ratio, QVector3D *value) {
            require(std::fabs(ratio - 0.5) < 1.0e-9, "geodesic fraction");
            *value = QVector3D(6.0f, 3.0f, 0.0f);
            ++sample_calls;
            return true;
        }, receive);
    require(sample_calls == 1 && starts.size() == 2,
            "curved link calls sample and emits two segments");
    require(near(ends[0].x(), 6.0f) && near(ends[0].y(), 3.0f)
            && near(starts[1].x(), 6.0f) && near(starts[1].y(), 3.0f),
            "geodesic sample shared by neighboring segments");
    require(near(ends[1].x(), 12.0f), "last endpoint exact");

    starts.clear(); ends.clear();
    MapGlobeLinkGeometry::emitCurvedSegments(start, end, 6000.0,
        [&](double, QVector3D *) { return false; }, receive);
    require(starts.size() == 2 && near(ends[0].x(), 6.0f),
            "failed geodesic query falls back to ECEF linear chord");

    starts.clear(); ends.clear();
    MapGlobeLinkGeometry::emitCurvedSegments(start, end, 6000.0, {}, receive);
    require(starts.size() == 2 && near(ends[0].x(), 6.0f),
            "absent sampler uses original linear fallback");

    starts.clear(); ends.clear();
    MapGlobeLinkGeometry::emitCurvedSegments(start, end, 6000.0,
        [&](double, QVector3D *) { return false; }, {});
    require(starts.empty(), "absent receiver is safe");
    std::cout << "PASS: curved link and ribbon geometry\n";
    return EXIT_SUCCESS;
}
