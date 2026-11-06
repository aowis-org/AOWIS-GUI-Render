#ifndef MAP_GLOBE_ICON_DRAW_ORDER_H
#define MAP_GLOBE_ICON_DRAW_ORDER_H

#include "map/render/map_network_render_data.h"

#include <QVector>
#include <QVector3D>

#include <algorithm>
#include <functional>
#include <numeric>

// Back-to-front triangle indices for screen-facing, alpha-blended globe
// icons. Their retained six-vertex quads never change as the camera moves;
// only this (small) index stream follows the eye. Sorting by camera distance
// avoids near symbols being overwritten by distant ones in snapshot order.
inline QVector<quint32> mapGlobeIconDrawOrder(const QVector<MapNetworkIconVertex> &vertices,
                                             const QVector3D &eye)
{
    if (vertices.size() % 6 != 0)
        return {};
    const qsizetype count = vertices.size() / 6;
    QVector<quint32> order(count);
    std::iota(order.begin(), order.end(), quint32(0));
    const std::function<double(quint32)> distance_squared = [&vertices, &eye](quint32 index) -> double
    {
        const MapNetworkIconVertex &vertex = vertices.at(qsizetype(index) * 6);
        const double dx = double(vertex.center_x) - double(eye.x());
        const double dy = double(vertex.center_y) - double(eye.y());
        const double dz = double(vertex.center_z) - double(eye.z());
        return dx * dx + dy * dy + dz * dz;
    };
    std::sort(order.begin(), order.end(), [&distance_squared](quint32 a, quint32 b)
    {
        const double a_distance = distance_squared(a);
        const double b_distance = distance_squared(b);
        return a_distance > b_distance || (a_distance == b_distance && a < b);
    });
    QVector<quint32> indices;
    indices.reserve(vertices.size());
    for (quint32 index : order)
    {
        const quint32 base = index * 6;
        for (quint32 offset = 0; offset < 6; ++offset)
            indices.append(base + offset);
    }
    return indices;
}

#endif // MAP_GLOBE_ICON_DRAW_ORDER_H
