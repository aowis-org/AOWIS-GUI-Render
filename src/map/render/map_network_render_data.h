#ifndef MAP_NETWORK_RENDER_DATA_H
#define MAP_NETWORK_RENDER_DATA_H

#include "map/core/map_network_types.h"

#include <QtGlobal>

// Backend-neutral retained network vertex layouts. Keep field order and types
// stable: the bgfx globe network uploads these structs directly
// (MapBgfxGlobeNetwork's vertex layouts mirror them).
struct MapNetworkLinkVertex
{
    float start_x = 0.0f;
    float start_y = 0.0f;
    float start_z = 0.0f;
    float end_x = 0.0f;
    float end_y = 0.0f;
    float end_z = 0.0f;
    float along = 0.0f;
    float side = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
    float size_adjust_px = 0.0f;
    // Optional world-space unit direction used to measure metre-sized link
    // half-width. Flat 2D geometry leaves this zero so the shared shader
    // retains its XY-plane perpendicular; Globe geometry fills it with the
    // local WGS84 tangent direction.
    float width_direction_x = 0.0f;
    float width_direction_y = 0.0f;
    float width_direction_z = 0.0f;
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
};

struct MapNetworkIconVertex
{
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float offset_x_ratio = 0.0f;
    float offset_y_ratio = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
};

struct MapNetworkHeatmapVertex
{
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float corner_x = 0.0f;
    float corner_y = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

struct MapNetworkNodeVertex
{
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float corner_x = 0.0f;
    float corner_y = 0.0f;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 1.0f;
    float size_adjust_px = 0.0f;
    // Globe generic-node quads are screen-facing billboards. When this flag
    // is set, metre sizing is measured along the camera-right world axis
    // instead of the flat renderer's global X axis, preserving true
    // perspective size without introducing an ECEF-axis dependency.
    float metric_billboard = 0.0f;
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
};

// Entity-coloured vertices (the globe network's base geometry) carry no
// colour: their colour fields hold a reference to the entity, and the
// renderer colours them from per-entity data on the GPU
// (map_bgfx_entity_style.h, shaders/entity_style.sh). A time step, metric,
// palette or range change therefore never rebuilds or re-uploads them.
//   red:   render id (exact in a float up to 2^24 entities per class),
//   green: 1 for a link, 0 for a node (demand point attachments reference
//          their demand point),
//   blue:  flow chevron orientation: +1 along the link, -1 against it,
//   alpha: 1.
template <typename Vertex>
void mapNetworkSetEntityReference(Vertex *vertex, quint32 render_id, bool link,
                                  float orientation = 0.0f)
{
    vertex->red = float(render_id);
    vertex->green = link ? 1.0f : 0.0f;
    vertex->blue = orientation;
    vertex->alpha = 1.0f;
}

#endif // MAP_NETWORK_RENDER_DATA_H
