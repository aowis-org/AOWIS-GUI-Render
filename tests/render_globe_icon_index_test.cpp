#include "map/bgfx/map_bgfx_globe_icon_index.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;
void require(bool condition, const char *name)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

MapBgfxGlobeIconIndex::Frustum frustum(double half_width, double relief)
{
    MapBgfxGlobeIconIndex::Frustum result;
    result.valid = true;
    result.relief_allowance_m = relief;
    result.sides = {{{1.0, 0.0, 0.0, half_width}, {-1.0, 0.0, 0.0, half_width},
                     {0.0, 1.0, 0.0, half_width}, {0.0, -1.0, 0.0, half_width}}};
    return result;
}
}

int main()
{
    MapBgfxGlobeIconIndex index;
    std::vector<MapBgfxGlobeIconIndex::Position> points;
    points.reserve(100000);
    // 100,000 deterministic anchors, spread over 100 x 100 km. Rendering
    // a one-kilometre window should not project all 100k or query their DEM.
    for (int row = 0; row < 250; ++row)
        for (int column = 0; column < 400; ++column)
            points.push_back({double(column * 250 - 50000), double(row * 400 - 50000), 6370000.0});
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    index.rebuild(points);
    const std::chrono::steady_clock::time_point after_build = std::chrono::steady_clock::now();
    require(index.size() == 100000 && index.buildCount() == 1, "build 100k retained anchors");

    const MapBgfxGlobeIconIndex::Frustum view = frustum(500.0, 0.0);
    std::vector<std::size_t> visible;
    MapBgfxGlobeIconIndex::QueryStats stats;
    for (int iteration = 0; iteration < 200; ++iteration)
        index.query(view, &visible, &stats);
    const std::chrono::steady_clock::time_point after_query = std::chrono::steady_clock::now();
    require(index.buildCount() == 1, "camera motion never rebuilds the icon index");
    require(visible.size() < 100 && stats.examined_points < 2000,
            "100k sparse network queries visit only nearby candidate anchors");
    require(std::is_sorted(visible.cbegin(), visible.cend()), "candidate ordering is stable");
    std::vector<std::size_t> unordered;
    index.queryUnordered(view, &unordered);
    require(unordered.size() == visible.size(), "unordered query has identical candidates");
    std::sort(unordered.begin(), unordered.end());
    require(unordered == visible, "render query cannot lose or add icon anchors");
    for (std::size_t id : visible)
    {
        require(std::abs(points[id].x) <= 500.0 && std::abs(points[id].y) <= 500.0,
                "no false negatives in exact plane test");
    }

    // Conservative relief allowance: points just beyond the sea-level view
    // must still be offered to the terrain-aware projector.
    const MapBgfxGlobeIconIndex::Frustum relief_view = frustum(500.0, 1000.0);
    index.query(relief_view, &visible);
    bool saw_raised_candidate = false;
    for (std::size_t id : visible)
    {
        if (points[id].x > 500.0 && points[id].x < 1500.0)
            saw_raised_candidate = true;
    }
    require(saw_raised_candidate, "terrain relief expands frustum conservatively");

    // Dense views whose entire indexed bounds lie inside the expanded
    // camera frustum take the linear path without kd-tree traversal.
    MapBgfxGlobeIconIndex::QueryStats dense_stats;
    index.queryUnordered(frustum(100000.0, 0.0), &unordered, &dense_stats);
    require(unordered.size() == points.size() && dense_stats.examined_points == 0
            && dense_stats.visited_nodes == 1,
            "fully visible networks skip unnecessary kd-tree traversal");
    require(std::is_sorted(unordered.cbegin(), unordered.cend()),
            "dense fast path preserves source order");

    // Missing frustum data deliberately falls back to full projection.
    MapBgfxGlobeIconIndex::Frustum absent;
    index.query(absent, &visible);
    require(visible.size() == points.size(), "invalid frustum preserves all icons");

    // Same-size geometry edits must explicitly rebuild when the revision
    // changes; the index itself never keeps pointers into an old snapshot.
    points.front().x = 10.0;
    index.rebuild(points);
    require(index.buildCount() == 2, "geometry revision supports index rebuild");

    const double build_ms = std::chrono::duration<double, std::milli>(after_build - start).count();
    const double query_ms = std::chrono::duration<double, std::milli>(after_query - after_build).count();
    std::printf("100k icon anchors: build %.3f ms; 200 camera queries %.3f ms; "
                "candidate scans %zu / 100000 per query\n",
                build_ms, query_ms, stats.examined_points);
    if (failures == 0)
        std::puts("All globe icon spatial-index tests passed.");
    return failures == 0 ? 0 : 1;
}
