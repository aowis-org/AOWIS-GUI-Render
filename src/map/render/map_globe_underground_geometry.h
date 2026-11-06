#ifndef MAP_GLOBE_UNDERGROUND_GEOMETRY_H
#define MAP_GLOBE_UNDERGROUND_GEOMETRY_H

#include <functional>

// Terrain-aware sampling policy for Globe X-Ray links. The host retains its
// geodesic, terrain LOD, vertical-transform and network entity adapters;
// Render owns the subdivision density and the contiguous buried-run logic.
// Classification samples must never become independent render segments:
// doing so restarts the GPU dash pattern at every sample.
class MapGlobeUndergroundGeometry
{
public:
    // Probe cell size at ratios 0, 1/2 and 1. Non-finite or nonpositive
    // values are ignored. The fallback interval is 20 metres; the terrain
    // interval is half the smallest valid cell, floored at 0.25 metres.
    static int subdivisionCount(
        double span_length_m,
        const std::function<double(double)> &terrain_cell_size_at_ratio);

    // Calls buried_at_ratio at all N+1 samples, including both endpoints.
    // A buried run begins at the sample *preceding* the first buried sample,
    // and ends at the last buried sample. This preserves the existing X-Ray
    // endpoint and dashed-stroke semantics (even for zero-length runs).
    static void emitBuriedRuns(
        int subdivision_count,
        const std::function<bool(double)> &buried_at_ratio,
        const std::function<void(double, double)> &receive_run);
};

#endif // MAP_GLOBE_UNDERGROUND_GEOMETRY_H
