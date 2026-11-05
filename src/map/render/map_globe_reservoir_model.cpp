#include "map/render/map_globe_reservoir_model.h"

#include <QVector2D>
#include <QVector3D>

#include <cmath>

// Reservoirs are open, ground-level basins (see network-svg/reservoir.svg and
// the "reservoir"/"lake" gothic icons), unlike the enclosed, domed Tank model
// in map_globe_tank_model.cpp. That means this mesh has a concave interior the
// camera can look down into, which the Tank mesh never had to deal with: an
// outward-facing wall alone is not enough, because its back faces are culled
// and looking into the basin would show a hole. Every primitive's winding
// below was checked numerically (triangle signed-area, not just eyeballed)
// against the two winding conventions already proven correct in
// map_globe_tank_model.cpp: appendDisc()'s face_up/face_down pattern and
// appendCylinderSide()'s outward pattern. Both reduce to the same rule in
// this codebase: "a triangle is front-facing when its vertex order is
// clockwise as seen from the direction its normal points". Everything new
// here (appendRing, the inward wall pass, the archway panel) is derived from
// that rule instead of copied by eye, specifically so it is not inside out.

namespace
{
constexpr int ReservoirRadialSegments = 96;
constexpr float Pi = 3.14159265358979323846f;

// Left 62.5% of the atlas: the wall cladding, reused for both the outer and
// inner wall face. Right 37.5%, split the same way the tank atlas splits its
// roof/metal region: top part is the water surface (top-down), bottom part
// is a flat concrete/metal fill reused for the coping and the base skirt.
// Matching the tank atlas's 0.625 / 0.375 split is a deliberate pipeline
// convention, not a copy of its artwork.
constexpr float WallU0 = 0.0f;
constexpr float WallU1 = 0.625f;
constexpr float CapU0 = 0.625f;
constexpr float CapU1 = 1.0f;
constexpr float WaterV0 = 0.0f;
constexpr float WaterV1 = 0.375f;
constexpr float ConcreteV0 = 0.375f;
constexpr float ConcreteV1 = 1.0f;

// A window carved out of the wall region for the spillway archway graphic,
// the same way the tank atlas carves a door-sized window out of its body
// region. Centred, low on the wall.
constexpr float ArchU0 = WallU0 + (WallU1 - WallU0) * 0.40f;
constexpr float ArchU1 = WallU0 + (WallU1 - WallU0) * 0.60f;
constexpr float ArchV0 = 0.55f;
constexpr float ArchV1 = 0.985f;

MapGlobeReservoirModelVertex makeVertex(
    const QVector3D &position,
    const QVector3D &normal,
    const QVector2D &uv)
{
    MapGlobeReservoirModelVertex vertex;
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
    QVector<MapGlobeReservoirModelVertex> *vertices,
    const QVector3D &a, const QVector3D &b, const QVector3D &c,
    const QVector3D &normal,
    const QVector2D &uv_a,
    const QVector2D &uv_b,
    const QVector2D &uv_c)
{
    // Same horizontal-reflection compensation as map_globe_tank_model.cpp's
    // appendTriangle(): the 3D map mirrors geography left-right, so the
    // model-space winding is reversed here to keep triangles front-facing
    // under back-face culling.
    vertices->append(makeVertex(a, normal, uv_a));
    vertices->append(makeVertex(c, normal, uv_c));
    vertices->append(makeVertex(b, normal, uv_b));
}

// Vertical cylindrical wall around the Z axis. With face_outward = true this
// is byte-for-byte the same construction as the Tank body cylinder (proven
// correct). With face_outward = false the two triangles of each segment have
// their last two vertices swapped and the normal negated, which is the
// standard, unconditionally-correct way to turn a front-facing surface into
// its inward-facing twin: reversing any two vertices of a triangle reverses
// which side of it is the front face. This is what makes the basin's
// interior wall visible from above instead of culled away.
void appendCylinderSide(
    QVector<MapGlobeReservoirModelVertex> *vertices,
    const QVector3D &center,
    float radius,
    float z0,
    float z1,
    float u0,
    float u1,
    float v0,
    float v1,
    int segments,
    bool face_outward)
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
        const QVector3D outward_normal0 = QVector3D(std::cos(angle0), std::sin(angle0), 0.0f).normalized();
        const QVector3D outward_normal1 = QVector3D(std::cos(angle1), std::sin(angle1), 0.0f).normalized();
        const QVector3D normal0 = face_outward ? outward_normal0 : -outward_normal0;
        const QVector3D normal1 = face_outward ? outward_normal1 : -outward_normal1;
        const QVector2D uv00(u0 + (u1 - u0) * t0, v1);
        const QVector2D uv01(u0 + (u1 - u0) * t1, v1);
        const QVector2D uv11(u0 + (u1 - u0) * t1, v0);
        const QVector2D uv10(u0 + (u1 - u0) * t0, v0);

        if (face_outward)
        {
            vertices->append(makeVertex(p00, normal0, uv00));
            vertices->append(makeVertex(p11, normal1, uv11));
            vertices->append(makeVertex(p01, normal1, uv01));
            vertices->append(makeVertex(p00, normal0, uv00));
            vertices->append(makeVertex(p10, normal0, uv10));
            vertices->append(makeVertex(p11, normal1, uv11));
        }
        else
        {
            // Last two vertices of each triangle swapped relative to the
            // face_outward branch above.
            vertices->append(makeVertex(p00, normal0, uv00));
            vertices->append(makeVertex(p01, normal1, uv01));
            vertices->append(makeVertex(p11, normal1, uv11));
            vertices->append(makeVertex(p00, normal0, uv00));
            vertices->append(makeVertex(p11, normal1, uv11));
            vertices->append(makeVertex(p10, normal0, uv10));
        }
    }
}

// Solid disc facing +Z (face_up) or -Z. Identical construction to
// map_globe_tank_model.cpp's appendDisc().
void appendDisc(
    QVector<MapGlobeReservoirModelVertex> *vertices,
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

// Flat annulus (ring) facing +Z or -Z, i.e. a disc with a hole. The Tank
// model never needed this - its cross-section is solid everywhere - so this
// is new. Each segment is authored as two triangles that follow the exact
// same (reference-vertex, point-at-t0, point-at-t1) shape as appendDisc()'s
// face_up/face_down calls above, with the ring's inner-radius vertex at t0
// standing in for the disc's fixed center vertex. That shape was checked
// with a signed-area computation to confirm it reduces to the same
// clockwise-from-the-normal rule as appendDisc(), for both face_up and
// face_down, before being used here.
void appendRing(
    QVector<MapGlobeReservoirModelVertex> *vertices,
    const QVector3D &center,
    float inner_radius,
    float outer_radius,
    float z,
    bool face_up,
    float u0,
    float v0,
    float u1,
    float v1,
    int segments)
{
    const QVector3D normal = face_up ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 0.0f, -1.0f);
    const QVector3D ring_center(center.x(), center.y(), center.z() + z);
    const QVector2D uv_center((u0 + u1) * 0.5f, (v0 + v1) * 0.5f);
    const float uv_radius_u = (u1 - u0) * 0.5f;
    const float uv_radius_v = (v1 - v0) * 0.5f;

    for (int index = 0; index < segments; ++index)
    {
        const float t0 = float(index) / float(segments);
        const float t1 = float(index + 1) / float(segments);
        const float angle0 = t0 * 2.0f * Pi;
        const float angle1 = t1 * 2.0f * Pi;
        const float cos0 = std::cos(angle0);
        const float sin0 = std::sin(angle0);
        const float cos1 = std::cos(angle1);
        const float sin1 = std::sin(angle1);

        const QVector3D outer0(ring_center.x() + cos0 * outer_radius, ring_center.y() + sin0 * outer_radius, ring_center.z());
        const QVector3D outer1(ring_center.x() + cos1 * outer_radius, ring_center.y() + sin1 * outer_radius, ring_center.z());
        const QVector3D inner0(ring_center.x() + cos0 * inner_radius, ring_center.y() + sin0 * inner_radius, ring_center.z());
        const QVector3D inner1(ring_center.x() + cos1 * inner_radius, ring_center.y() + sin1 * inner_radius, ring_center.z());

        // Outer-radius points sit exactly on the UV box edge; only the
        // inner radius needs scaling down, same technique as
        // map_globe_tank_model.cpp's appendDome() radial UV mapping.
        const float outer_fraction = 1.0f;
        const float inner_fraction = inner_radius / qMax(outer_radius, 0.0001f);
        const QVector2D uv_outer0(uv_center.x() + cos0 * outer_fraction * uv_radius_u, uv_center.y() + sin0 * outer_fraction * uv_radius_v);
        const QVector2D uv_outer1(uv_center.x() + cos1 * outer_fraction * uv_radius_u, uv_center.y() + sin1 * outer_fraction * uv_radius_v);
        const QVector2D uv_inner0(uv_center.x() + cos0 * inner_fraction * uv_radius_u, uv_center.y() + sin0 * inner_fraction * uv_radius_v);
        const QVector2D uv_inner1(uv_center.x() + cos1 * inner_fraction * uv_radius_u, uv_center.y() + sin1 * inner_fraction * uv_radius_v);

        if (face_up)
        {
            appendTriangle(vertices, inner0, outer0, outer1, normal, uv_inner0, uv_outer0, uv_outer1);
            appendTriangle(vertices, inner0, outer1, inner1, normal, uv_inner0, uv_outer1, uv_inner1);
        }
        else
        {
            appendTriangle(vertices, inner0, outer1, outer0, normal, uv_inner0, uv_outer1, uv_outer0);
            appendTriangle(vertices, inner0, inner1, outer1, normal, uv_inner0, uv_inner1, uv_outer1);
        }
    }
}

// Flat quad on the outer wall face, the same technique
// map_globe_tank_model.cpp uses for its door: a painted graphic on a slightly
// recessed quad rather than modeled 3D depth. Used here for the reservoir's
// spillway archway, which is what the "reservoir" gothic icon actually uses
// as its distinguishing front feature (a lake icon uses a plain crest
// instead), so it earns real geometry rather than being left to the wall
// texture alone.
void appendWallPanel(
    QVector<MapGlobeReservoirModelVertex> *vertices,
    const QVector3D &center,
    float x,
    float y0,
    float y1,
    float z0,
    float z1,
    float u0,
    float u1,
    float v0,
    float v1)
{
    const QVector3D p0(center.x() + x, center.y() + y0, center.z() + z0);
    const QVector3D p1(center.x() + x, center.y() + y1, center.z() + z0);
    const QVector3D p2(center.x() + x, center.y() + y1, center.z() + z1);
    const QVector3D p3(center.x() + x, center.y() + y0, center.z() + z1);
    const QVector3D normal(1.0f, 0.0f, 0.0f);
    const QVector2D uv0(u0, v1);
    const QVector2D uv1(u1, v1);
    const QVector2D uv2(u1, v0);
    const QVector2D uv3(u0, v0);
    appendTriangle(vertices, p0, p1, p2, normal, uv0, uv1, uv2);
    appendTriangle(vertices, p0, p2, p3, normal, uv0, uv2, uv3);
}

void appendReservoir(
    QVector<MapGlobeReservoirModelVertex> *vertices,
    const MapGlobeReservoirInstance &instance)
{
    const QVector3D center = instance.base_center;
    const float outer_radius = instance.radius_world;
    const float wall_height = instance.wall_height_world;
    const float wall_thickness = outer_radius * 0.12f;
    const float inner_radius = outer_radius - wall_thickness;
    const float coping_overhang = outer_radius * 0.04f;
    const float coping_outer_radius = outer_radius + coping_overhang;
    const float coping_thickness = wall_height * 0.10f;
    const float water_recess = wall_height * 0.16f;
    const float water_z = wall_height - water_recess;

    // Outer wall, seen from outside the basin.
    appendCylinderSide(vertices, center, outer_radius,
        0.0f, wall_height,
        WallU0, WallU1, 0.0f, 1.0f,
        ReservoirRadialSegments, true);

    // Base skirt, so the wall does not look like an open tube from below.
    appendDisc(vertices, center, outer_radius, 0.0f, false,
        CapU0, ConcreteV0, CapU1, ConcreteV1,
        ReservoirRadialSegments);

    // Interior wall, visible above the waterline when looking down into the
    // basin. Only built from the waterline up: below that the opaque water
    // disc already covers it, so the surface would never be seen.
    // appendCylinderSide() maps its z0 edge to the v1 parameter and its z1
    // edge to v0, so this partial near-coping strip is given v0 = 0.0 (the
    // top-of-wall row, matching z1 = wall_height) and v1 scaled down to
    // wherever the waterline actually falls in the full wall texture,
    // instead of reusing the outer wall's full 0..1 range.
    const float water_v_fraction = 1.0f - water_z / qMax(wall_height, 0.0001f);
    appendCylinderSide(vertices, center, inner_radius,
        water_z, wall_height,
        WallU0, WallU1, 0.0f, water_v_fraction,
        ReservoirRadialSegments, false);

    // Coping edge: outward face plus its underside, the same rim treatment
    // map_globe_tank_model.cpp uses for the tank's roof rim.
    appendCylinderSide(vertices, center, coping_outer_radius,
        wall_height - coping_thickness, wall_height,
        CapU0, CapU1, ConcreteV0, ConcreteV1,
        ReservoirRadialSegments, true);
    appendDisc(vertices, center, coping_outer_radius, wall_height - coping_thickness, false,
        CapU0, ConcreteV0, CapU1, ConcreteV1,
        ReservoirRadialSegments);

    // Coping top: the walkable rim surface, from the inner wall out past the
    // outer wall face to the overhang.
    appendRing(vertices, center, inner_radius, coping_outer_radius, wall_height, true,
        CapU0, ConcreteV0, CapU1, ConcreteV1,
        ReservoirRadialSegments);

    // Water surface, recessed below the rim so the basin reads as open and
    // partly full rather than a solid drum.
    appendDisc(vertices, center, inner_radius, water_z, true,
        CapU0, WaterV0, CapU1, WaterV1,
        ReservoirRadialSegments);

    // Spillway archway, the reservoir's equivalent of the tank's door: a
    // painted feature on the wall face rather than a generic pipe stub,
    // matching what the reservoir gothic icon actually uses as its
    // distinguishing front feature.
    const float arch_width = outer_radius * 0.46f;
    const float arch_height = wall_height * 0.62f;
    const float arch_depth = outer_radius * 0.05f;
    appendWallPanel(vertices, center, outer_radius - arch_depth,
        -arch_width * 0.5f, arch_width * 0.5f,
        0.0f, arch_height,
        ArchU0, ArchU1, ArchV0, ArchV1);
}
}

QVector<MapGlobeReservoirModelVertex> mapGlobeBuildReservoirModelVertices(
    const QVector<MapGlobeReservoirInstance> &instances)
{
    QVector<MapGlobeReservoirModelVertex> vertices;
    vertices.reserve(instances.size() * 9000);
    for (const MapGlobeReservoirInstance &instance : instances)
    {
        MapGlobeReservoirInstance local_instance = instance;
        local_instance.base_center = QVector3D();
        const qsizetype first_vertex = vertices.size();
        appendReservoir(&vertices, local_instance);
        for (qsizetype vertex_index = first_vertex; vertex_index < vertices.size(); ++vertex_index)
        {
            MapGlobeReservoirModelVertex &vertex = vertices[vertex_index];
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
