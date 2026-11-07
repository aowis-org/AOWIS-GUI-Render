#ifndef MAP_CAMERA_VIEW_STATE_H
#define MAP_CAMERA_VIEW_STATE_H

#include "map/core/map_view_types.h"

#include <cmath>
#include <cstdint>

// Continuous, backend-neutral camera data. No tile zoom, raster cache zoom,
// GPU-resource revision or network geometry revision belongs in this state.
// The 2D camera zoom is measured in continuous Web Mercator zoom levels;
// the globe camera uses the physical orbit distance in metres.
struct MapCameraViewState
{
    MapViewMode mode = MapViewMode::TwoD;
    double center_lon_deg = 0.0;
    double center_lat_deg = 0.0;
    double continuous_2d_zoom = 0.0;
    double globe_yaw_deg = 0.0;
    double globe_pitch_deg = 90.0;
    double globe_distance_m = 1.0;
    double globe_vertical_offset_m = 0.0;
    std::uint64_t globe_pose_revision = 0;
    double globe_collision_lift_m = 0.0;
};

// Frame/camera updates and tile resource selection are *not* network
// invalidation events. Network geometry/attribute revisions are instead
// delivered through NetworkRenderSnapshot and renderer-specific style inputs.
struct MapCameraUpdate
{
    bool camera_changed = false;
    bool imagery_tile_selection_changed = false;
    bool network_geometry_invalidated = false;
    bool network_attributes_invalidated = false;
};

inline MapCameraUpdate mapCameraUpdate(
    const MapCameraViewState &previous, const MapCameraViewState &current,
    int previous_imagery_zoom, int current_imagery_zoom)
{
    const bool center_changed = previous.center_lon_deg != current.center_lon_deg
        || previous.center_lat_deg != current.center_lat_deg;
    const bool view_changed = previous.mode != current.mode;
    const bool flat_changed = previous.continuous_2d_zoom != current.continuous_2d_zoom;
    const bool globe_changed = previous.globe_pose_revision != current.globe_pose_revision
        || previous.globe_yaw_deg != current.globe_yaw_deg
        || previous.globe_pitch_deg != current.globe_pitch_deg
        || previous.globe_distance_m != current.globe_distance_m
        || previous.globe_vertical_offset_m != current.globe_vertical_offset_m
        || previous.globe_collision_lift_m != current.globe_collision_lift_m;
    MapCameraUpdate result;
    result.camera_changed = center_changed || view_changed
        || (current.mode == MapViewMode::TwoD && flat_changed)
        || (current.mode == MapViewMode::Globe && globe_changed);
    result.imagery_tile_selection_changed =
        previous_imagery_zoom != current_imagery_zoom;
    return result;
}

#endif // MAP_CAMERA_VIEW_STATE_H
