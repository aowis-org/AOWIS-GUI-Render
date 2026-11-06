#include "map/render/map_globe_underground_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double FallbackTargetLengthM = 20.0;
constexpr double MinimumTargetLengthM = 0.25;
constexpr double TerrainCellFraction = 0.5;
constexpr int MaximumCount = 128;
}

int MapGlobeUndergroundGeometry::subdivisionCount(
    double span_length_m,
    const std::function<double(double)> &terrain_cell_size_at_ratio)
{
    double minimum_cell_size_m = std::numeric_limits<double>::infinity();
    if (terrain_cell_size_at_ratio)
    {
        const double probe_ratios[3] = {0.0, 0.5, 1.0};
        for (double ratio : probe_ratios)
        {
            const double cell_size_m = terrain_cell_size_at_ratio(ratio);
            if (std::isfinite(cell_size_m) && cell_size_m > 0.0)
                minimum_cell_size_m = std::min(minimum_cell_size_m, cell_size_m);
        }
    }

    double target_length_m = FallbackTargetLengthM;
    if (std::isfinite(minimum_cell_size_m))
    {
        target_length_m = std::max(
            MinimumTargetLengthM, minimum_cell_size_m * TerrainCellFraction);
    }

    if (!std::isfinite(span_length_m) || span_length_m <= 0.0)
        return 1;

    // Clamp before narrowing to int so extreme/corrupt spans cannot overflow.
    return int(std::clamp(
        std::ceil(span_length_m / target_length_m), 1.0, double(MaximumCount)));
}

void MapGlobeUndergroundGeometry::emitBuriedRuns(
    int subdivision_count,
    const std::function<bool(double)> &buried_at_ratio,
    const std::function<void(double, double)> &receive_run)
{
    if (subdivision_count < 1 || !buried_at_ratio || !receive_run)
        return;

    bool buried_run_active = false;
    double buried_run_start_ratio = 0.0;
    double previous_sample_ratio = 0.0;
    for (int subdivision = 0; subdivision <= subdivision_count; ++subdivision)
    {
        const double ratio = double(subdivision) / double(subdivision_count);
        const bool buried = buried_at_ratio(ratio);
        if (buried && !buried_run_active)
        {
            buried_run_active = true;
            buried_run_start_ratio = previous_sample_ratio;
        }
        else if (!buried && buried_run_active)
        {
            receive_run(buried_run_start_ratio, previous_sample_ratio);
            buried_run_active = false;
        }
        previous_sample_ratio = ratio;
    }

    if (buried_run_active)
        receive_run(buried_run_start_ratio, previous_sample_ratio);
}
