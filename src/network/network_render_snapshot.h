#ifndef NETWORK_RENDER_SNAPSHOT_H
#define NETWORK_RENDER_SNAPSHOT_H

#include <QList>
#include <QString>
#include <QUuid>
#include <QtGlobal>

#include <limits>

#include <aowis/model/gis.h>

#include "map/core/map_network_types.h"

struct NetworkRenderNode
{
    quint32 render_id = 0;
    QString id;
    QUuid uuid;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    CoordinateWGS84 coordinate_wgs84;
    double elevation_m = 0.0;
};

struct NetworkRenderLink
{
    quint32 render_id = 0;
    QString id;
    QUuid uuid;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    quint32 start_node_render_id = 0;
    quint32 end_node_render_id = 0;

    // Complete ordered geometry: start node, optional vertices, end node.
    QList<CoordinateWGS84> vertices_wgs84;
    // Elevation for every entry in vertices_wgs84. Intermediate link vertices
    // interpolate between endpoint elevations because the hydraulic model does
    // not currently store a dedicated elevation for those geometry vertices.
    QList<double> elevations_m;
};

struct NetworkRenderDemandPointAttachment
{
    quint32 demand_point_render_id = 0;
    QUuid demand_point_uuid;
    CoordinateWGS84 demand_point_coordinate_wgs84;
    CoordinateWGS84 attachment_coordinate_wgs84;
    // Elevation of the network at the attachment: the junction's, or the
    // pipe's at the attachment position (interpolated between its end nodes
    // along its length, as NetworkRenderLink::elevations_m). NaN: unknown.
    double attachment_elevation_m = std::numeric_limits<double>::quiet_NaN();
};

struct NetworkRenderSnapshot
{
    quint64 geometry_revision = 0;
    quint64 visual_revision = 0;
    QList<NetworkRenderNode> nodes;
    QList<NetworkRenderLink> links;
    QList<NetworkRenderDemandPointAttachment> demand_point_attachments;
};

#endif // NETWORK_RENDER_SNAPSHOT_H
