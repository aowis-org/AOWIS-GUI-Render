#include "map/render/map_globe_underground_geometry.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
bool near(double left, double right)
{
    return std::abs(left - right) < 1.0e-10;
}
}

int main()
{
    assert(MapGlobeUndergroundGeometry::subdivisionCount(100.0, {}) == 5);
    assert(MapGlobeUndergroundGeometry::subdivisionCount(0.0, {}) == 1);
    assert(MapGlobeUndergroundGeometry::subdivisionCount(-5.0, {}) == 1);
    assert(MapGlobeUndergroundGeometry::subdivisionCount(1.0e15, {}) == 128);
    assert(MapGlobeUndergroundGeometry::subdivisionCount(
        std::numeric_limits<double>::infinity(), {}) == 1);

    std::vector<double> probes;
    const int terrain_count = MapGlobeUndergroundGeometry::subdivisionCount(
        100.0,
        [&probes](double ratio) -> double {
            probes.push_back(ratio);
            if (near(ratio, 0.5))
                return 4.0;
            if (near(ratio, 1.0))
                return 10.0;
            return std::numeric_limits<double>::quiet_NaN();
        });
    assert(terrain_count == 50);
    assert(probes.size() == 3);
    assert(near(probes[0], 0.0) && near(probes[1], 0.5) && near(probes[2], 1.0));
    assert(MapGlobeUndergroundGeometry::subdivisionCount(
        100.0, [](double) { return 0.05; }) == 128);
    assert(MapGlobeUndergroundGeometry::subdivisionCount(
        100.0, [](double) { return -1.0; }) == 5);

    std::vector<std::pair<double, double>> runs;
    std::vector<double> classification_samples;
    const std::function<void(double, double)> collect =
        [&runs](double start, double end) { runs.emplace_back(start, end); };

    MapGlobeUndergroundGeometry::emitBuriedRuns(
        8,
        [&classification_samples](double ratio) {
            classification_samples.push_back(ratio);
            return ratio >= 0.25 && ratio <= 0.5;
        }, collect);
    assert(classification_samples.size() == 9);
    assert(runs.size() == 1);
    assert(near(runs[0].first, 0.125) && near(runs[0].second, 0.5));

    runs.clear();
    MapGlobeUndergroundGeometry::emitBuriedRuns(
        8, [](double ratio) {
            return near(ratio, 0.125) || near(ratio, 0.5) || near(ratio, 1.0);
        }, collect);
    assert(runs.size() == 3);
    assert(near(runs[0].first, 0.0) && near(runs[0].second, 0.125));
    assert(near(runs[1].first, 0.375) && near(runs[1].second, 0.5));
    assert(near(runs[2].first, 0.875) && near(runs[2].second, 1.0));

    runs.clear();
    MapGlobeUndergroundGeometry::emitBuriedRuns(
        4, [](double) { return true; }, collect);
    assert(runs.size() == 1 && near(runs[0].first, 0.0) && near(runs[0].second, 1.0));

    runs.clear();
    MapGlobeUndergroundGeometry::emitBuriedRuns(
        4, [](double) { return false; }, collect);
    assert(runs.empty());

    runs.clear();
    MapGlobeUndergroundGeometry::emitBuriedRuns(
        4, [](double ratio) { return near(ratio, 0.0); }, collect);
    assert(runs.size() == 1);
    assert(near(runs[0].first, 0.0) && near(runs[0].second, 0.0));

    runs.clear();
    MapGlobeUndergroundGeometry::emitBuriedRuns(
        0, [](double) { return true; }, collect);
    assert(runs.empty());
    MapGlobeUndergroundGeometry::emitBuriedRuns(4, {}, collect);
    assert(runs.empty());
}
