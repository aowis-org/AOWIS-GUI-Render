#ifndef MAP_GLOBE_RENDER_INSTANCES_H
#define MAP_GLOBE_RENDER_INSTANCES_H

#include <QtGlobal>
#include <QVector3D>

// Backend-neutral retained Globe instance layouts. These are scene data, not
// GPU resources; the renderer builds its vertex streams from them.
struct MapGlobeTankInstance
{
    quint32 render_id = 0;
    QVector3D base_center;
    QVector3D basis_x = QVector3D(1.0f, 0.0f, 0.0f);
    QVector3D basis_y = QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D basis_z = QVector3D(0.0f, 0.0f, 1.0f);
    float radius_world = 0.0f;
    float base_height_world = 0.0f;
    float body_height_world = 0.0f;
    float roof_height_world = 0.0f;
    float selected = 0.0f;
};

struct MapGlobeReservoirInstance
{
    quint32 render_id = 0;
    QVector3D base_center;
    QVector3D basis_x = QVector3D(1.0f, 0.0f, 0.0f);
    QVector3D basis_y = QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D basis_z = QVector3D(0.0f, 0.0f, 1.0f);
    float radius_world = 0.0f;
    float wall_height_world = 0.0f;
    float selected = 0.0f;
};

struct MapGlobeJunctionInstance
{
    quint32 render_id = 0;
    // Kept as a float because the shader bundle includes GLSL/ESSL 100,
    // whose legacy targets do not support unsigned integer attributes.
    float style_index = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float radius_world = 0.0f;
};

static_assert(sizeof(MapGlobeJunctionInstance) == 24,
              "Junction impostor instances must remain compact");

#endif // MAP_GLOBE_RENDER_INSTANCES_H
