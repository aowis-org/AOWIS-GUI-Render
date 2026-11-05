#ifndef MAP_GLOBE_RESERVOIR_MODEL_H
#define MAP_GLOBE_RESERVOIR_MODEL_H

#include "map/render/map_globe_render_instances.h"

#include <QtGlobal>
#include <QVector>
#include <QVector3D>

struct MapGlobeReservoirModelVertex
{
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float normal_z = 1.0f;
    float u = 0.0f;
    float v = 0.0f;
    float selected = 0.0f;
    quint32 render_id = 0;
};

QVector<MapGlobeReservoirModelVertex> mapGlobeBuildReservoirModelVertices(
    const QVector<MapGlobeReservoirInstance> &instances);

#endif // MAP_GLOBE_RESERVOIR_MODEL_H
