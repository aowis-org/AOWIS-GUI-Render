#include "map/render/map_globe_tank_model.h"

#include <QVector2D>
#include <QVector3D>

#include <cmath>

namespace
{
constexpr int TankRadialSegments = 96;
constexpr int TankDomeSegments = 24;
constexpr float Pi = 3.14159265358979323846f;
constexpr float BodyU0 = 0.0f;
constexpr float BodyU1 = 0.625f;
constexpr float RoofU0 = 0.625f;
constexpr float RoofU1 = 1.0f;
constexpr float RoofV0 = 0.0f;
constexpr float RoofV1 = 0.375f;
constexpr float MetalV0 = 0.375f;
constexpr float MetalV1 = 1.0f;

MapGlobeTankModelVertex makeVertex(
    const QVector3D &position,
    const QVector3D &normal,
    const QVector2D &uv)
{
    MapGlobeTankModelVertex vertex;
    vertex.position_x = position.x();
    vertex.position_y = position.y();
    vertex.position_z = position.z();
    vertex.normal_x = normal.x();
    vertex.normal_y = normal.y();
    vertex.normal_z = normal.z();
    vertex.u = uv.x();
    vertex.v = uv.y();
    return vertex;
}

void appendTriangle(
    QVector<MapGlobeTankModelVertex> *vertices,
    const QVector3D &a, const QVector3D &b, const QVector3D &c,
    const QVector3D &normal,
    const QVector2D &uv_a,
    const QVector2D &uv_b,
    const QVector2D &uv_c)
{
    // The 3D map projection contains a horizontal reflection so geographic east
    // stays screen-right. Reverse the model-space triangle winding here to keep
    // the tank exterior front-facing with the Globe renderer's back-face culling.
    vertices->append(makeVertex(a, normal, uv_a));
    vertices->append(makeVertex(c, normal, uv_c));
    vertices->append(makeVertex(b, normal, uv_b));
}

void appendQuad(
    QVector<MapGlobeTankModelVertex> *vertices,
    const QVector3D &a,
    const QVector3D &b,
    const QVector3D &c,
    const QVector3D &d,
    const QVector3D &normal,
    const QVector2D &uv_a,
    const QVector2D &uv_b,
    const QVector2D &uv_c,
    const QVector2D &uv_d)
{
    appendTriangle(vertices, a, b, c, normal, uv_a, uv_b, uv_c);
    appendTriangle(vertices, a, c, d, normal, uv_a, uv_c, uv_d);
}

void appendCylinderSide(
    QVector<MapGlobeTankModelVertex> *vertices,
    const QVector3D &center,
    float radius,
    float z0,
    float z1,
    float u0,
    float u1,
    float v0,
    float v1,
    int segments)
{
    for (int index = 0; index < segments; ++index)
    {
        const float t0 = float(index) / float(segments);
        const float t1 = float(index + 1) / float(segments);
        const float angle0 = t0 * 2.0f * Pi;
        const float angle1 = t1 * 2.0f * Pi;
        const float x0 = std::cos(angle0) * radius;
        const float y0 = std::sin(angle0) * radius;
        const float x1 = std::cos(angle1) * radius;
        const float y1 = std::sin(angle1) * radius;
        const QVector3D p00(center.x() + x0, center.y() + y0, center.z() + z0);
        const QVector3D p01(center.x() + x1, center.y() + y1, center.z() + z0);
        const QVector3D p11(center.x() + x1, center.y() + y1, center.z() + z1);
        const QVector3D p10(center.x() + x0, center.y() + y0, center.z() + z1);
        const QVector3D normal0 = QVector3D(std::cos(angle0), std::sin(angle0), 0.0f).normalized();
        const QVector3D normal1 = QVector3D(std::cos(angle1), std::sin(angle1), 0.0f).normalized();

        vertices->append(makeVertex(p00, normal0, QVector2D(u0 + (u1 - u0) * t0, v1)));
        vertices->append(makeVertex(p11, normal1, QVector2D(u0 + (u1 - u0) * t1, v0)));
        vertices->append(makeVertex(p01, normal1, QVector2D(u0 + (u1 - u0) * t1, v1)));
        vertices->append(makeVertex(p00, normal0, QVector2D(u0 + (u1 - u0) * t0, v1)));
        vertices->append(makeVertex(p10, normal0, QVector2D(u0 + (u1 - u0) * t0, v0)));
        vertices->append(makeVertex(p11, normal1, QVector2D(u0 + (u1 - u0) * t1, v0)));
    }
}

void appendDisc(
    QVector<MapGlobeTankModelVertex> *vertices,
    const QVector3D &center,
    float radius,
    float z,
    bool face_up,
    float u0,
    float v0,
    float u1,
    float v1,
    int segments)
{
    const QVector3D normal = face_up ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 0.0f, -1.0f);
    const QVector3D disc_center(center.x(), center.y(), center.z() + z);
    const QVector2D uv_center((u0 + u1) * 0.5f, (v0 + v1) * 0.5f);

    for (int index = 0; index < segments; ++index)
    {
        const float t0 = float(index) / float(segments);
        const float t1 = float(index + 1) / float(segments);
        const float angle0 = t0 * 2.0f * Pi;
        const float angle1 = t1 * 2.0f * Pi;
        const QVector3D p0(
            disc_center.x() + std::cos(angle0) * radius,
            disc_center.y() + std::sin(angle0) * radius,
            disc_center.z());
        const QVector3D p1(
            disc_center.x() + std::cos(angle1) * radius,
            disc_center.y() + std::sin(angle1) * radius,
            disc_center.z());
        const QVector2D uv0(
            uv_center.x() + std::cos(angle0) * (u1 - u0) * 0.5f,
            uv_center.y() + std::sin(angle0) * (v1 - v0) * 0.5f);
        const QVector2D uv1(
            uv_center.x() + std::cos(angle1) * (u1 - u0) * 0.5f,
            uv_center.y() + std::sin(angle1) * (v1 - v0) * 0.5f);

        if (face_up)
            appendTriangle(vertices, disc_center, p0, p1, normal, uv_center, uv0, uv1);
        else
            appendTriangle(vertices, disc_center, p1, p0, normal, uv_center, uv1, uv0);
    }
}

void appendDome(
    QVector<MapGlobeTankModelVertex> *vertices,
    const QVector3D &center,
    float radius,
    float z_base,
    float height,
    int radial_segments,
    int vertical_segments)
{
    for (int y_index = 0; y_index < vertical_segments; ++y_index)
    {
        const float v0 = float(y_index) / float(vertical_segments);
        const float v1 = float(y_index + 1) / float(vertical_segments);
        const float theta0 = v0 * (Pi * 0.5f);
        const float theta1 = v1 * (Pi * 0.5f);
        const float ring_radius0 = std::cos(theta0) * radius;
        const float ring_radius1 = std::cos(theta1) * radius;
        const float ring_z0 = z_base + std::sin(theta0) * height;
        const float ring_z1 = z_base + std::sin(theta1) * height;
        const float roof_center_u = (RoofU0 + RoofU1) * 0.5f;
        const float roof_center_v = (RoofV0 + RoofV1) * 0.5f;
        const float roof_radius_u = (RoofU1 - RoofU0) * 0.47f;
        const float roof_radius_v = (RoofV1 - RoofV0) * 0.47f;

        for (int x_index = 0; x_index < radial_segments; ++x_index)
        {
            const float u0 = float(x_index) / float(radial_segments);
            const float u1 = float(x_index + 1) / float(radial_segments);
            const float phi0 = u0 * 2.0f * Pi;
            const float phi1 = u1 * 2.0f * Pi;

            const QVector3D p00(
                center.x() + std::cos(phi0) * ring_radius0,
                center.y() + std::sin(phi0) * ring_radius0,
                center.z() + ring_z0);
            const QVector3D p01(
                center.x() + std::cos(phi1) * ring_radius0,
                center.y() + std::sin(phi1) * ring_radius0,
                center.z() + ring_z0);
            const QVector3D p11(
                center.x() + std::cos(phi1) * ring_radius1,
                center.y() + std::sin(phi1) * ring_radius1,
                center.z() + ring_z1);
            const QVector3D p10(
                center.x() + std::cos(phi0) * ring_radius1,
                center.y() + std::sin(phi0) * ring_radius1,
                center.z() + ring_z1);

            const QVector3D n00 = QVector3D(
                std::cos(phi0) * std::cos(theta0),
                std::sin(phi0) * std::cos(theta0),
                std::sin(theta0)).normalized();
            const QVector3D n01 = QVector3D(
                std::cos(phi1) * std::cos(theta0),
                std::sin(phi1) * std::cos(theta0),
                std::sin(theta0)).normalized();
            const QVector3D n11 = QVector3D(
                std::cos(phi1) * std::cos(theta1),
                std::sin(phi1) * std::cos(theta1),
                std::sin(theta1)).normalized();
            const QVector3D n10 = QVector3D(
                std::cos(phi0) * std::cos(theta1),
                std::sin(phi0) * std::cos(theta1),
                std::sin(theta1)).normalized();

            const float radial0 = ring_radius0 / qMax(radius, 0.0001f);
            const float radial1 = ring_radius1 / qMax(radius, 0.0001f);
            const QVector2D uv00(
                roof_center_u + std::cos(phi0) * radial0 * roof_radius_u,
                roof_center_v + std::sin(phi0) * radial0 * roof_radius_v);
            const QVector2D uv01(
                roof_center_u + std::cos(phi1) * radial0 * roof_radius_u,
                roof_center_v + std::sin(phi1) * radial0 * roof_radius_v);
            const QVector2D uv11(
                roof_center_u + std::cos(phi1) * radial1 * roof_radius_u,
                roof_center_v + std::sin(phi1) * radial1 * roof_radius_v);
            const QVector2D uv10(
                roof_center_u + std::cos(phi0) * radial1 * roof_radius_u,
                roof_center_v + std::sin(phi0) * radial1 * roof_radius_v);

            vertices->append(makeVertex(p00, n00, uv00));
            vertices->append(makeVertex(p11, n11, uv11));
            vertices->append(makeVertex(p01, n01, uv01));
            vertices->append(makeVertex(p00, n00, uv00));
            vertices->append(makeVertex(p10, n10, uv10));
            vertices->append(makeVertex(p11, n11, uv11));
        }
    }
}

void appendTank(
    QVector<MapGlobeTankModelVertex> *vertices,
    const MapGlobeTankInstance &instance)
{
    const QVector3D center = instance.base_center;
    const float base_height = instance.base_height_world;
    const float body_height = instance.body_height_world;
    const float roof_height = instance.roof_height_world;
    const float radius = instance.radius_world;
    const float body_bottom = base_height;
    const float body_top = base_height + body_height;
    const float rim_height = body_height * 0.08f;
    const float ring_radius = radius * 1.05f;
    const float hatch_radius = radius * 0.18f;
    const float hatch_height = roof_height * 0.25f;

    appendCylinderSide(vertices, center, radius * 1.02f,
        0.0f, base_height,
        RoofU0, RoofU1, MetalV0, MetalV1,
        TankRadialSegments);
    appendDisc(vertices, center, radius * 1.02f, 0.0f, false,
        RoofU0, MetalV0, RoofU1, MetalV1,
        TankRadialSegments);
    appendDisc(vertices, center, radius * 1.02f, base_height, true,
        RoofU0, MetalV0, RoofU1, MetalV1,
        TankRadialSegments);

    appendCylinderSide(vertices, center, radius,
        body_bottom, body_top,
        BodyU0, BodyU1, 0.0f, 1.0f,
        TankRadialSegments);
    appendDisc(vertices, center, radius, body_bottom, false,
        RoofU0, MetalV0, RoofU1, MetalV1,
        TankRadialSegments);

    appendCylinderSide(vertices, center, ring_radius,
        body_top - rim_height, body_top,
        RoofU0, RoofU1, MetalV0, MetalV1,
        TankRadialSegments);
    appendDisc(vertices, center, ring_radius, body_top - rim_height, false,
        RoofU0, MetalV0, RoofU1, MetalV1,
        TankRadialSegments);

    appendDome(vertices, center, radius * 0.98f, body_top - rim_height * 0.4f,
        roof_height, TankRadialSegments, TankDomeSegments);

    appendCylinderSide(vertices, center, hatch_radius,
        body_top + roof_height * 0.72f,
        body_top + roof_height * 0.72f + hatch_height,
        RoofU0, RoofU1, MetalV0, MetalV1,
        TankRadialSegments / 2);
    appendDisc(vertices, center, hatch_radius,
        body_top + roof_height * 0.72f + hatch_height,
        true,
        RoofU0, RoofV0, RoofU1, RoofV1,
        TankRadialSegments / 2);

    const float door_width = radius * 0.42f;
    const float door_height = body_height * 0.28f;
    const float door_depth = radius * 0.06f;
    const float door_z0 = base_height * 0.18f;
    const float door_z1 = door_z0 + door_height;
    const QVector3D p0(center.x() + radius - door_depth, center.y() - door_width * 0.5f, center.z() + door_z0);
    const QVector3D p1(center.x() + radius - door_depth, center.y() + door_width * 0.5f, center.z() + door_z0);
    const QVector3D p2(center.x() + radius - door_depth, center.y() + door_width * 0.5f, center.z() + door_z1);
    const QVector3D p3(center.x() + radius - door_depth, center.y() - door_width * 0.5f, center.z() + door_z1);
    appendQuad(vertices, p0, p1, p2, p3,
        QVector3D(1.0f, 0.0f, 0.0f),
        QVector2D(BodyU0 + 0.43f * (BodyU1 - BodyU0), 0.89f),
        QVector2D(BodyU0 + 0.57f * (BodyU1 - BodyU0), 0.89f),
        QVector2D(BodyU0 + 0.57f * (BodyU1 - BodyU0), 0.73f),
        QVector2D(BodyU0 + 0.43f * (BodyU1 - BodyU0), 0.73f));
}
}

QVector<MapGlobeTankModelVertex> mapGlobeBuildTankModelVertices(
    const QVector<MapGlobeTankInstance> &instances)
{
    QVector<MapGlobeTankModelVertex> vertices;
    vertices.reserve(instances.size() * 18000);
    for (const MapGlobeTankInstance &instance : instances)
    {
        MapGlobeTankInstance local_instance = instance;
        local_instance.base_center = QVector3D();
        const qsizetype first_vertex = vertices.size();
        appendTank(&vertices, local_instance);
        for (qsizetype vertex_index = first_vertex; vertex_index < vertices.size(); ++vertex_index)
        {
            MapGlobeTankModelVertex &vertex = vertices[vertex_index];
            const QVector3D local_position(
                vertex.position_x, vertex.position_y, vertex.position_z);
            const QVector3D local_normal(
                vertex.normal_x, vertex.normal_y, vertex.normal_z);
            const QVector3D world_position = instance.base_center
                + instance.basis_x * local_position.x()
                + instance.basis_y * local_position.y()
                + instance.basis_z * local_position.z();
            const QVector3D world_normal = (
                instance.basis_x * local_normal.x()
                + instance.basis_y * local_normal.y()
                + instance.basis_z * local_normal.z()).normalized();
            vertex.position_x = world_position.x();
            vertex.position_y = world_position.y();
            vertex.position_z = world_position.z();
            vertex.normal_x = world_normal.x();
            vertex.normal_y = world_normal.y();
            vertex.normal_z = world_normal.z();
            vertex.selected = instance.selected;
            vertex.render_id = instance.render_id;
        }
    }
    return vertices;
}
