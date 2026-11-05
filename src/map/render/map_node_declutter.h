#ifndef MAP_NODE_DECLUTTER_H
#define MAP_NODE_DECLUTTER_H

#include <QHash>
#include <QPointF>
#include <QVector>
#include <QtGlobal>

struct MapNodeDeclutterInput
{
    quint32 render_id = 0;
    QPointF world_center;
};

// Shared real-world separation used by both the flat bgfx scene and the
// Globe scene. Keep this deliberately small: decluttering exists to split
// exact/near-exact modeling coincidences, not to spread ordinary dense
// network geometry apart.
constexpr double MapNodeDeclutterMinimumSeparationMeters = 1.0;

// Nodes whose world_center values fall within minimum_separation_world of one
// another - directly, or transitively through a shared neighbour, e.g. three
// coincident nodes at one point - are spread evenly apart on a small circle
// around their shared centroid so every node keeps a distinct, individually
// selectable on-screen position. This is a real-world scenario for hydraulic
// networks: a pump or valve has no physical length, so its inlet/outlet
// junctions are conventionally digitized at the exact same coordinate.
//
// Nodes that already have enough separation are left untouched: their
// render_id is not present in the returned map. Offsets are expressed in the
// same local world-unit space as world_center and only ever move a node
// horizontally (elevation is left alone). Callers should add the returned
// offset to every rendered position anchored to that node, including the
// matching endpoint of any connected link, so link geometry stays attached
// to its (possibly nudged) node markers.
QHash<quint32, QPointF> computeNodeDeclutterOffsets(
    const QVector<MapNodeDeclutterInput> &nodes,
    double minimum_separation_world);

#endif // MAP_NODE_DECLUTTER_H
