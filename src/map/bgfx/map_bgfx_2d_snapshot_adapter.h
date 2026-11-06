#ifndef MAP_BGFX_2D_SNAPSHOT_ADAPTER_H
#define MAP_BGFX_2D_SNAPSHOT_ADAPTER_H

#include "map/bgfx/map_bgfx_2d_network_input.h"
#include "map/render/map_retained_network_scene.h"

#include <utility>

// The only production 2D renderer dependency on GUI-owned network types.
// Called on geometry revisions and first-time monitor/editor variant switches,
// never for camera movement, zoom, hydraulic value updates or HUD redraws.
inline MapBgfx2dNetworkInput mapBgfx2dNetworkInput(const MapRetainedNetworkScene &retained)
{
    MapBgfx2dNetworkInput result;
    if (!retained.hasSnapshot())
        return result;
    const NetworkRenderSnapshot &snapshot = retained.snapshot();
    result.valid = true;
    result.geometry_revision = snapshot.geometry_revision;
    result.nodes.reserve(snapshot.nodes.size());
    result.links.reserve(snapshot.links.size());
    result.attachments.reserve(snapshot.demand_point_attachments.size());
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        result.nodes.append({node.render_id, static_cast<quint8>(node.entity_type),
            {node.coordinate_wgs84.longitude_deg, node.coordinate_wgs84.latitude_deg},
            node.entity_type == InfrastructureEntity::Junction});
    }
    for (const NetworkRenderLink &link : snapshot.links)
    {
        MapBgfx2dInputLink normalized;
        normalized.render_id = link.render_id;
        normalized.entity_type = static_cast<quint8>(link.entity_type);
        normalized.start_node_render_id = link.start_node_render_id;
        normalized.end_node_render_id = link.end_node_render_id;
        normalized.pipe = link.entity_type == InfrastructureEntity::Pipe;
        normalized.vertices.reserve(link.vertices_wgs84.size());
        for (const CoordinateWGS84 &point : link.vertices_wgs84)
            normalized.vertices.append({point.longitude_deg, point.latitude_deg});
        result.links.append(std::move(normalized));
    }
    for (const NetworkRenderDemandPointAttachment &attachment : snapshot.demand_point_attachments)
    {
        result.attachments.append({attachment.demand_point_render_id,
            {attachment.demand_point_coordinate_wgs84.longitude_deg,
             attachment.demand_point_coordinate_wgs84.latitude_deg},
            {attachment.attachment_coordinate_wgs84.longitude_deg,
             attachment.attachment_coordinate_wgs84.latitude_deg}});
    }
    return result;
}

#endif // MAP_BGFX_2D_SNAPSHOT_ADAPTER_H
