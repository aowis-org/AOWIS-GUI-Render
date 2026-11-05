#ifndef MAP_BGFX_2D_NETWORK_INPUT_H
#define MAP_BGFX_2D_NETWORK_INPUT_H

#include <QVector>
#include <QtGlobal>

// Geometry-only input to the production 2D renderer. The host adapts its
// network model into this format only when the geometry revision changes.
// Entity type is an opaque host-owned 8-bit classification used for picking,
// icon lookup, and stable rendering keys. Drawing uses explicit kind flags;
// the renderer must not depend on the host's hydraulic entity enum.
struct MapBgfx2dGeoPoint
{
    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
};

struct MapBgfx2dInputNode
{
    quint32 render_id = 0;
    quint8 entity_type = 0;
    MapBgfx2dGeoPoint coordinate;
    bool junction = false;
};

struct MapBgfx2dInputLink
{
    quint32 render_id = 0;
    quint8 entity_type = 0;
    quint32 start_node_render_id = 0;
    quint32 end_node_render_id = 0;
    QVector<MapBgfx2dGeoPoint> vertices;
    bool pipe = false;
};

struct MapBgfx2dInputAttachment
{
    quint32 demand_point_render_id = 0;
    MapBgfx2dGeoPoint demand;
    MapBgfx2dGeoPoint anchor;
};

struct MapBgfx2dNetworkInput
{
    bool valid = false;
    quint64 geometry_revision = 0;
    QVector<MapBgfx2dInputNode> nodes;
    QVector<MapBgfx2dInputLink> links;
    QVector<MapBgfx2dInputAttachment> attachments;
};

#endif // MAP_BGFX_2D_NETWORK_INPUT_H
