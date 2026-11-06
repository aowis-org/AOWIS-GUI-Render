// Globe network CPU side in Render (AOWIS::RenderGlobeNetwork): the render
// contract's entity traits, the icon atlas (drawn in code, no host
// resources) and far-to-near icon ordering. Checks do not depend on
// assert(), so they also run in Release (NDEBUG) builds.

#include "map/render/map_globe_icon_draw_order.h"
#include "map/render/map_globe_network_scene.h"
#include "map/render/map_icon_atlas.h"
#include "network/infrastructure_entity_traits.h"

#include <QGuiApplication>
#include <QImage>
#include <QVector3D>
#include <QVector>

#include <cstdio>

namespace
{
int g_failures = 0;
int g_checks = 0;

void expectTrue(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

void testTraits()
{
    expectTrue(InfrastructureEntityTraits::isHydraulicConnectionNode(InfrastructureEntity::Tank)
                   && !InfrastructureEntityTraits::isHydraulicConnectionNode(InfrastructureEntity::DemandPoint),
               "tanks connect links, demand points do not");
    expectTrue(InfrastructureEntityTraits::isHydraulicPointEntity(InfrastructureEntity::DemandPoint),
               "demand points are point entities");
    expectTrue(InfrastructureEntityTraits::isHydraulicNetworkLink(InfrastructureEntity::Valve)
                   && InfrastructureEntityTraits::isHydraulicDeviceLink(InfrastructureEntity::Pump)
                   && !InfrastructureEntityTraits::isHydraulicDeviceLink(InfrastructureEntity::Pipe),
               "pumps and valves are device links, pipes are not");
}

void testIconAtlas()
{
    const QImage atlas = mapIconAtlasImage();
    expectTrue(!atlas.isNull() && atlas.format() == QImage::Format_RGBA8888_Premultiplied,
               "the atlas is drawn in code as premultiplied RGBA8888");

    const InfrastructureEntity with_icons[] = {
        InfrastructureEntity::Reservoir, InfrastructureEntity::Tank, InfrastructureEntity::Pump,
        InfrastructureEntity::Valve, InfrastructureEntity::DemandPoint};
    bool all_valid = true;
    bool all_drawn = true;
    for (InfrastructureEntity entity : with_icons)
    {
        const MapIconAtlasEntry entry = mapIconAtlasEntry(entity);
        all_valid = all_valid && mapHasIcon(entity) && entry.valid
            && entry.uv_rect.width() > 0.0 && entry.uv_rect.right() <= 1.0
            && entry.uv_rect.bottom() <= 1.0;
        if (!entry.valid)
            continue;
        // Something opaque inside the icon's cell.
        const QPoint centre(int(entry.uv_rect.center().x() * atlas.width()),
                            int(entry.uv_rect.center().y() * atlas.height()));
        bool any_ink = false;
        for (int dy = -6; dy <= 6 && !any_ink; ++dy)
        {
            for (int dx = -6; dx <= 6 && !any_ink; ++dx)
                any_ink = atlas.pixelColor(centre + QPoint(dx, dy)).alpha() > 0;
        }
        all_drawn = all_drawn && any_ink;
    }
    expectTrue(all_valid, "reservoir, tank, pump, valve and demand point have atlas cells");
    expectTrue(all_drawn, "every atlas cell is drawn");
    expectTrue(!mapHasIcon(InfrastructureEntity::Junction) && !mapIconAtlasEntry(InfrastructureEntity::Junction).valid,
               "junctions are drawn as markers, not icons");
}

void testIconDrawOrder()
{
    // Three icons (six vertices each) at 30, 10 and 20 m from the eye.
    QVector<MapNetworkIconVertex> vertices;
    const float distances[] = {30.0f, 10.0f, 20.0f};
    for (float distance : distances)
    {
        MapNetworkIconVertex vertex;
        vertex.center_x = distance;
        for (int corner = 0; corner < 6; ++corner)
            vertices.append(vertex);
    }
    // The result is a vertex index stream: six indices per icon quad, quads
    // far to near (icon 0 at 30 m, icon 2 at 20 m, icon 1 at 10 m).
    const QVector<quint32> order = mapGlobeIconDrawOrder(vertices, QVector3D(0.0f, 0.0f, 0.0f));
    expectTrue(order.size() == 18 && order.at(0) == 0u && order.at(6) == 12u && order.at(12) == 6u,
               "icons are drawn far to near");
    expectTrue(order.size() == 18 && order.at(5) == 5u && order.at(17) == 11u,
               "each quad keeps its six vertices in order");
    vertices.removeLast();
    expectTrue(mapGlobeIconDrawOrder(vertices, QVector3D()).isEmpty(),
               "an incomplete icon quad list yields no order");
}

void testSceneConstructs()
{
    MapGlobeNetworkScene scene;
    expectTrue(true, "the globe network scene links and constructs without a host");
}
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    testTraits();
    testIconAtlas();
    testIconDrawOrder();
    testSceneConstructs();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d of %d globe network checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: %d globe network checks\n", g_checks);
    return 0;
}
