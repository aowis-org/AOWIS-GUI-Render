#ifndef MAP_GLOBE_SCENE_SYNC_H
#define MAP_GLOBE_SCENE_SYNC_H

// A model-independent invalidation plan for the retained 3D network scene.
// GUI adapts hydraulic/Qt state into changes; Render decides which CPU
// streams need rebuilding. Camera movement and visual-only revisions must
// never invalidate the geometry-owned ECEF coordinate frame.
struct MapGlobeSceneInvalidation
{
    bool geometry = false;
    bool icons = false;
    bool models = false;
    bool junctions = false;
    bool underground = false;
    bool flow = false;
    bool highlights = false;

    bool empty() const;
};

struct MapGlobeSceneSymbologyChanges
{
    bool link_thickness_changed = false;
    bool icon_changed = false;
    bool junction_size_changed = false;
    bool flow_changed = false;
    bool highlights_changed = false;
    bool has_demand_attachments = false;
    bool underground_enabled = false;
};

class MapGlobeSceneSync
{
public:
    // Full geometry supersedes every derived stream; visual-only revisions
    // do not touch retained vertex allocations.
    static MapGlobeSceneInvalidation forSnapshot(bool geometry_changed);
    static MapGlobeSceneInvalidation forSymbology(
        const MapGlobeSceneSymbologyChanges &changes);
    static MapGlobeSceneInvalidation forSelection();
    static MapGlobeSceneInvalidation forDiagnostics();
    static MapGlobeSceneInvalidation forIconModelMode();
    static MapGlobeSceneInvalidation forFlowScale(
        bool use_3d_icon_models, bool icon_size_in_pixels);
    // Useful for future batched notifications; full geometry dominates.
    static MapGlobeSceneInvalidation merge(
        const MapGlobeSceneInvalidation &left,
        const MapGlobeSceneInvalidation &right);
};

#endif // MAP_GLOBE_SCENE_SYNC_H
