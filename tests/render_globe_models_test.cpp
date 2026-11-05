#include "map/render/map_globe_render_instances.h"
#include "map/render/map_globe_reservoir_model.h"
#include "map/render/map_globe_tank_model.h"
#include "map/render/map_globe_vertical_transform.h"
#include "map/render/map_globe_surface_vertex.h"

#include <QVector>
#include <QVector3D>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;

void expect(bool condition, const char *description)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

template <typename Vertex>
bool finiteVertex(const Vertex &vertex)
{
    const double normal_length = std::sqrt(
        double(vertex.normal_x) * vertex.normal_x
        + double(vertex.normal_y) * vertex.normal_y
        + double(vertex.normal_z) * vertex.normal_z);
    return std::isfinite(vertex.position_x)
        && std::isfinite(vertex.position_y)
        && std::isfinite(vertex.position_z)
        && std::isfinite(vertex.u)
        && std::isfinite(vertex.v)
        && std::isfinite(normal_length)
        && std::abs(normal_length - 1.0) < 1e-4;
}

template <typename Vertex>
void inspectMesh(const QVector<Vertex> &mesh, quint32 id, float selected)
{
    expect(!mesh.isEmpty(), "non-empty globe model mesh");
    expect(mesh.size() % 3 == 0, "mesh has complete triangle vertices");
    for (const Vertex &vertex : mesh)
    {
        expect(finiteVertex(vertex), "vertex position, UV and unit normal are finite");
        expect(vertex.render_id == id, "render ID preserved in every model vertex");
        expect(vertex.selected == selected, "selection metadata preserved in every model vertex");
    }
}

template <typename Vertex>
void inspectTranslation(const QVector<Vertex> &base, const QVector<Vertex> &shifted,
                        const QVector3D &offset)
{
    expect(base.size() == shifted.size(), "rigid translation preserves topology");
    for (qsizetype index = 0; index < base.size() && index < shifted.size(); ++index)
    {
        const Vertex &a = base.at(index);
        const Vertex &b = shifted.at(index);
        expect(std::abs(double((b.position_x - a.position_x) - offset.x())) < 0.002,
            "world-space model X translation");
        expect(std::abs(double((b.position_y - a.position_y) - offset.y())) < 0.002,
            "world-space model Y translation");
        expect(std::abs(double((b.position_z - a.position_z) - offset.z())) < 0.002,
            "world-space model Z translation");
        expect(std::abs(double(b.normal_x - a.normal_x)) < 1e-5
            && std::abs(double(b.normal_y - a.normal_y)) < 1e-5
            && std::abs(double(b.normal_z - a.normal_z)) < 1e-5,
            "translation leaves normals unchanged");
    }
}

void testTank()
{
    MapGlobeTankInstance instance;
    instance.render_id = 0x12345u;
    instance.selected = 1.0f;
    instance.radius_world = 8.0f;
    instance.body_height_world = 14.0f;
    instance.roof_height_world = 5.0f;
    instance.base_height_world = 2.0f;
    const QVector<MapGlobeTankModelVertex> base = mapGlobeBuildTankModelVertices({instance});
    inspectMesh(base, instance.render_id, instance.selected);
    const QVector3D offset(120.0f, -30.0f, 18.0f);
    instance.base_center = offset;
    const QVector<MapGlobeTankModelVertex> shifted = mapGlobeBuildTankModelVertices({instance});
    inspectTranslation(base, shifted, offset);
    expect(mapGlobeBuildTankModelVertices({}).isEmpty(), "empty tank instances produce no mesh");
}

void testReservoir()
{
    MapGlobeReservoirInstance instance;
    instance.render_id = 0xabcdefu;
    instance.selected = 0.5f;
    instance.radius_world = 10.0f;
    instance.wall_height_world = 6.0f;
    const QVector<MapGlobeReservoirModelVertex> base = mapGlobeBuildReservoirModelVertices({instance});
    inspectMesh(base, instance.render_id, instance.selected);
    const QVector3D offset(-22.0f, 34.0f, 7.0f);
    instance.base_center = offset;
    const QVector<MapGlobeReservoirModelVertex> shifted = mapGlobeBuildReservoirModelVertices({instance});
    inspectTranslation(base, shifted, offset);
    expect(mapGlobeBuildReservoirModelVertices({}).isEmpty(), "empty reservoir instances produce no mesh");
}
}

int main()
{
    testTank();
    testReservoir();
    const MapGlobeVerticalTransform transform(2.0, 0.0);
    expect(std::abs(transform.terrainHeightM(8.0) - 16.0) < 1e-9,
        "vertical exaggeration remains consistent");
    if (failures != 0)
    {
        std::cerr << failures << " globe geometry assertions failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Production globe model geometry: PASS\n";
    return EXIT_SUCCESS;
}
