#include "map/render/map_globe_scene_sync.h"

#include <cstdio>

namespace
{
bool require(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
}

int main()
{
    const MapGlobeSceneInvalidation visual = MapGlobeSceneSync::forSnapshot(false);
    if (!require(visual.empty(), "visual-only revision rebuilt retained geometry"))
        return 1;
    const MapGlobeSceneInvalidation geometry = MapGlobeSceneSync::forSnapshot(true);
    if (!require(geometry.geometry && !geometry.icons && !geometry.flow,
                 "geometry revision must subsume derived streams"))
        return 1;

    MapGlobeSceneSymbologyChanges changes;
    changes.link_thickness_changed = true;
    changes.flow_changed = true;
    changes.highlights_changed = true;
    MapGlobeSceneInvalidation plan = MapGlobeSceneSync::forSymbology(changes);
    if (!require(!plan.geometry && plan.flow && plan.highlights,
                 "link thickness without attachments must avoid geometry rebuild"))
        return 1;
    changes.has_demand_attachments = true;
    plan = MapGlobeSceneSync::forSymbology(changes);
    if (!require(plan.geometry && !plan.flow && !plan.highlights,
                 "demand attachments require one full rebuild"))
        return 1;

    changes = MapGlobeSceneSymbologyChanges{};
    changes.icon_changed = true;
    plan = MapGlobeSceneSync::forSymbology(changes);
    if (!require(plan.icons && plan.models && !plan.geometry && !plan.junctions,
                 "icon size/visibility must only invalidate icons and models"))
        return 1;

    changes = MapGlobeSceneSymbologyChanges{};
    changes.junction_size_changed = true;
    plan = MapGlobeSceneSync::forSymbology(changes);
    if (!require(plan.junctions && !plan.underground,
                 "junction size without X-Ray must not classify terrain"))
        return 1;
    changes.underground_enabled = true;
    plan = MapGlobeSceneSync::forSymbology(changes);
    if (!require(plan.junctions && plan.underground && !plan.geometry,
                 "junction size in X-Ray must refresh buried instances"))
        return 1;

    changes = MapGlobeSceneSymbologyChanges{};
    changes.flow_changed = true;
    plan = MapGlobeSceneSync::forSymbology(changes);
    if (!require(plan.flow && !plan.models && !plan.geometry,
                 "flow result changes must not retessellate links"))
        return 1;

    plan = MapGlobeSceneSync::forSelection();
    if (!require(plan.highlights && plan.icons && plan.models && !plan.geometry,
                 "selection should not rebuild geometry"))
        return 1;
    plan = MapGlobeSceneSync::forDiagnostics();
    if (!require(plan.highlights && !plan.icons && !plan.geometry,
                 "diagnostics should touch highlights only"))
        return 1;
    plan = MapGlobeSceneSync::forIconModelMode();
    if (!require(plan.icons && plan.models && !plan.geometry,
                 "model-mode toggle must not rebuild network geometry"))
        return 1;
    for (int models = 0; models < 2; ++models)
    {
        for (int pixels = 0; pixels < 2; ++pixels)
        {
            plan = MapGlobeSceneSync::forFlowScale(models != 0, pixels != 0);
            if (!require(plan.flow && plan.models == (models != 0 && pixels != 0)
                         && !plan.geometry && !plan.icons,
                         "flow-scale model invalidation mismatch"))
                return 1;
        }
    }
    const MapGlobeSceneInvalidation merged = MapGlobeSceneSync::merge(
        MapGlobeSceneSync::forDiagnostics(), MapGlobeSceneSync::forIconModelMode());
    if (!require(merged.highlights && merged.icons && merged.models && !merged.geometry,
                 "batched changes must preserve all independent streams"))
        return 1;
    plan = MapGlobeSceneSync::merge(merged, geometry);
    if (!require(plan.geometry && !plan.icons && !plan.models && !plan.highlights,
                 "full geometry must subsume batched rebuilds"))
        return 1;
    return 0;
}
