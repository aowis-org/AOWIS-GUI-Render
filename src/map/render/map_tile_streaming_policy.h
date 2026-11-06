#ifndef MAP_TILE_STREAMING_POLICY_H
#define MAP_TILE_STREAMING_POLICY_H

// Only budget selection is shared here: no bgfx, Qt, GPU or model dependency.
// The active-navigation budgets favor responsiveness over maximum tile fill
// throughput. Idle budgets retain the original fast fill behavior.
namespace MapTileStreamingPolicy
{
inline constexpr int TwoDActiveUploads = 2;
inline constexpr int TwoDIdleUploads = 8;
inline constexpr int GlobeActiveUploads = 3;
inline constexpr int GlobeIdleUploads = 12;

constexpr int uploadBudget(bool globe, bool camera_moving, int override_budget = -1)
{
    if (override_budget >= 1 && override_budget <= 32)
        return override_budget;
    if (globe)
        return camera_moving ? GlobeActiveUploads : GlobeIdleUploads;
    return camera_moving ? TwoDActiveUploads : TwoDIdleUploads;
}
}

#endif // MAP_TILE_STREAMING_POLICY_H
