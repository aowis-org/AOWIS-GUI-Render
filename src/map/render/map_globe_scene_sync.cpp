#include "map/render/map_globe_scene_sync.h"

bool MapGlobeSceneInvalidation::empty() const
{
    return !this->geometry && !this->icons && !this->models
        && !this->junctions && !this->underground && !this->flow
        && !this->highlights;
}

MapGlobeSceneInvalidation MapGlobeSceneSync::forSnapshot(bool geometry_changed)
{
    MapGlobeSceneInvalidation plan;
    plan.geometry = geometry_changed;
    return plan;
}

MapGlobeSceneInvalidation MapGlobeSceneSync::forSymbology(
    const MapGlobeSceneSymbologyChanges &changes)
{
    MapGlobeSceneInvalidation plan;
    // Attachment widths are baked into their link geometry. For other links
    // thickness is a GPU style property and needs no geometry rebuild.
    if (changes.link_thickness_changed && changes.has_demand_attachments)
    {
        plan.geometry = true;
        return plan;
    }
    plan.icons = changes.icon_changed;
    plan.models = changes.icon_changed;
    plan.junctions = changes.junction_size_changed;
    plan.underground = changes.junction_size_changed && changes.underground_enabled;
    plan.flow = changes.flow_changed;
    plan.highlights = changes.highlights_changed;
    return plan;
}

MapGlobeSceneInvalidation MapGlobeSceneSync::forSelection()
{
    MapGlobeSceneInvalidation plan;
    plan.highlights = true;
    plan.icons = true;
    plan.models = true;
    return plan;
}

MapGlobeSceneInvalidation MapGlobeSceneSync::forDiagnostics()
{
    MapGlobeSceneInvalidation plan;
    plan.highlights = true;
    return plan;
}

MapGlobeSceneInvalidation MapGlobeSceneSync::forIconModelMode()
{
    MapGlobeSceneInvalidation plan;
    plan.icons = true;
    plan.models = true;
    return plan;
}

MapGlobeSceneInvalidation MapGlobeSceneSync::forFlowScale(
    bool use_3d_icon_models, bool icon_size_in_pixels)
{
    MapGlobeSceneInvalidation plan;
    plan.flow = true;
    plan.models = use_3d_icon_models && icon_size_in_pixels;
    return plan;
}

MapGlobeSceneInvalidation MapGlobeSceneSync::merge(
    const MapGlobeSceneInvalidation &left,
    const MapGlobeSceneInvalidation &right)
{
    if (left.geometry || right.geometry)
        return forSnapshot(true);
    MapGlobeSceneInvalidation plan;
    plan.icons = left.icons || right.icons;
    plan.models = left.models || right.models;
    plan.junctions = left.junctions || right.junctions;
    plan.underground = left.underground || right.underground;
    plan.flow = left.flow || right.flow;
    plan.highlights = left.highlights || right.highlights;
    return plan;
}
