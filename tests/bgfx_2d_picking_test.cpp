#include "map/bgfx/map_bgfx_2d_snapshot_adapter.h"
#include "map/bgfx/map_bgfx_2d_scene.h"
#include "map/bgfx/map_bgfx_2d_mesh.h"
#include "map/bgfx/map_bgfx_2d_picking.h"

#include <cmath>
#include <cstdio>

// bgfx 2D picking (slice 5): QRhi priorities and radii against the drawn
// geometry (decluttered nodes, link icons at the link middle, wrap at
// +-180 deg), plus the scene features the highlights and the declutter
// toggle rely on (entityKey, filtered(), preparationId()).
namespace
{
int fails = 0;

void req(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++fails;
    }
}

CoordinateWGS84 coordinate(double longitude, double latitude)
{
    CoordinateWGS84 result;
    result.longitude_deg = longitude;
    result.latitude_deg = latitude;
    return result;
}

struct TestNetwork
{
    NetworkRenderSnapshot snapshot;
    MapRetainedNetworkScene retained;
    MapBgfx2dScene scene;

    quint32 addNode(quint32 render_id, double lon, double lat, InfrastructureEntity entity_type)
    {
        NetworkRenderNode node;
        node.render_id = render_id;
        node.entity_type = entity_type;
        node.coordinate_wgs84 = coordinate(lon, lat);
        this->snapshot.nodes.append(node);
        return render_id;
    }
    quint32 addLink(quint32 render_id, const QVector<CoordinateWGS84> &vertices,
                    InfrastructureEntity entity_type)
    {
        NetworkRenderLink link;
        link.render_id = render_id;
        link.entity_type = entity_type;
        for (const CoordinateWGS84 &vertex : vertices)
            link.vertices_wgs84.append(vertex);
        this->snapshot.links.append(link);
        return render_id;
    }
    const MapBgfx2dScene &prepare()
    {
        this->snapshot.geometry_revision = 1;
        this->retained.setSnapshot(this->snapshot);
        this->scene.prepare(mapBgfx2dNetworkInput(this->retained));
        return this->scene;
    }
};

MapBgfx2dPicking::Options defaultOptions()
{
    MapBgfx2dPicking::Options options;
    options.icon_size_px = 24.0;
    options.node_half_px = 6.0;
    options.link_width_px = 3.0;
    options.show_icons = true;
    options.show_junctions = true;
    // Same set as mapHasIcon(): everything but junctions and pipes.
    options.has_icon = [](InfrastructureEntity entity_type)
    {
        return entity_type == InfrastructureEntity::Tank
            || entity_type == InfrastructureEntity::Reservoir
            || entity_type == InfrastructureEntity::Pump
            || entity_type == InfrastructureEntity::Valve;
    };
    return options;
}

MapBgfx2d::ScreenPoint offset(const MapBgfx2d::ScreenPoint &point, double dx, double dy)
{
    return {point.x + dx, point.y + dy};
}

const MapBgfx2dPoint *findNode(const MapBgfx2dScene &scene, quint32 render_id)
{
    for (const MapBgfx2dPoint &point : scene.networkNodes())
    {
        if (point.render_id == render_id)
            return &point;
    }
    return nullptr;
}
}

int main()
{
    // Junction, tank and a pipe along a parallel; a bent pump elsewhere.
    TestNetwork network;
    const quint32 junction = network.addNode(1, 9.000, 47.0, InfrastructureEntity::Junction);
    const quint32 tank = network.addNode(2, 9.002, 47.0, InfrastructureEntity::Tank);
    QVector<CoordinateWGS84> pipe_vertices;
    pipe_vertices.append(coordinate(9.000, 47.0));
    pipe_vertices.append(coordinate(9.002, 47.0));
    const quint32 pipe = network.addLink(1, pipe_vertices, InfrastructureEntity::Pipe);
    QVector<CoordinateWGS84> pump_vertices;
    pump_vertices.append(coordinate(9.010, 47.0));
    pump_vertices.append(coordinate(9.011, 47.0));
    pump_vertices.append(coordinate(9.014, 47.0));
    const quint32 pump = network.addLink(2, pump_vertices, InfrastructureEntity::Pump);
    const MapBgfx2dScene &scene = network.prepare();

    const double zoom = 16.5; // everything on screen, entities ~66 px apart
    const MapBgfx2d::Camera camera = MapBgfx2d::cameraFor(9.006, 47.0, zoom, 1600, 900);
    const MapBgfx2dPicking::Options options = defaultOptions();
    const MapBgfx2d::ScreenPoint junction_px =
        MapBgfx2d::screenFromWorld(camera, MapBgfx2d::worldFromWgs84(9.000, 47.0));
    const MapBgfx2d::ScreenPoint tank_px =
        MapBgfx2d::screenFromWorld(camera, MapBgfx2d::worldFromWgs84(9.002, 47.0));
    const MapBgfx2d::ScreenPoint pipe_mid_px =
        MapBgfx2d::screenFromWorld(camera, MapBgfx2d::worldFromWgs84(9.001, 47.0));
    const MapBgfx2d::ScreenPoint pump_mid_px =
        MapBgfx2d::screenFromWorld(camera, MapBgfx2d::worldFromWgs84(9.012, 47.0));
    std::printf("junction (%.1f, %.1f) tank (%.1f, %.1f) pump middle (%.1f, %.1f)\n",
                junction_px.x, junction_px.y, tank_px.x, tank_px.y, pump_mid_px.x, pump_mid_px.y);

    // 1. Nodes: nearest within max(10, 6 + 5) = 11 px; beyond it the pipe.
    {
        const MapBgfx2dPicking::Hit near = MapBgfx2dPicking::pick(
            scene, camera, offset(junction_px, 0.0, 10.5), options);
        req(near.render_id == junction && near.entity_type == InfrastructureEntity::Junction,
            "junction picked within 11 px");
        const MapBgfx2dPicking::Hit far = MapBgfx2dPicking::pick(
            scene, camera, offset(junction_px, 0.0, 11.5), options);
        req(!far.isValid(), "nothing within reach 11.5 px below the junction");
        const MapBgfx2dPicking::Hit along_pipe = MapBgfx2dPicking::pick(
            scene, camera, offset(pipe_mid_px, 0.0, 7.5), options);
        req(along_pipe.render_id == pipe && along_pipe.entity_type == InfrastructureEntity::Pipe,
            "pipe picked within max(8, 1.5 + 5) px");
        req(!MapBgfx2dPicking::pick(scene, camera, offset(pipe_mid_px, 0.0, 8.5), options).isValid(),
            "pipe not picked beyond 8 px");
        // Node over link where both are in reach.
        const MapBgfx2dPicking::Hit on_junction = MapBgfx2dPicking::pick(
            scene, camera, offset(junction_px, 3.0, 0.0), options);
        req(on_junction.entity_type == InfrastructureEntity::Junction,
            "node has priority over the link it touches");
    }

    // 2. Icons first, with their own radius max(12, 12 + 6) = 18 px.
    {
        const MapBgfx2dPicking::Hit icon = MapBgfx2dPicking::pick(
            scene, camera, offset(tank_px, 0.0, -16.0), options);
        req(icon.render_id == tank && icon.entity_type == InfrastructureEntity::Tank,
            "tank picked through its icon at 16 px");
        MapBgfx2dPicking::Options no_icons = options;
        no_icons.show_icons = false;
        req(!MapBgfx2dPicking::pick(scene, camera, offset(tank_px, 0.0, -16.0), no_icons).isValid(),
            "hidden icons are not pickable");
        const MapBgfx2dPicking::Hit pump_icon = MapBgfx2dPicking::pick(
            scene, camera, offset(pump_mid_px, 0.0, -14.0), options);
        req(pump_icon.render_id == pump && pump_icon.entity_type == InfrastructureEntity::Pump,
            "pump picked through its icon at the middle of the link length");
        const MapBgfx2d::ScreenPoint pump_bend_px =
            MapBgfx2d::screenFromWorld(camera, MapBgfx2d::worldFromWgs84(9.011, 47.0));
        req(!MapBgfx2dPicking::pick(scene, camera, offset(pump_bend_px, 0.0, -14.0), options).isValid(),
            "no pump icon at the middle vertex");
    }

    // 3. Hidden junctions are not pickable; the pipe under them is.
    {
        MapBgfx2dPicking::Options hidden = options;
        hidden.show_junctions = false;
        const MapBgfx2dPicking::Hit hit = MapBgfx2dPicking::pick(
            scene, camera, offset(junction_px, 3.0, 0.0), hidden);
        req(hit.entity_type == InfrastructureEntity::Pipe, "hidden junction falls through to the pipe");
    }

    // 4. Decluttered nodes are picked where they are drawn; toggling the
    //    declutter re-prepares (new preparationId) and the GPU mesh follows.
    {
        TestNetwork coincident;
        const quint32 inlet = coincident.addNode(10, 8.0, 46.0, InfrastructureEntity::Junction);
        const quint32 outlet = coincident.addNode(11, 8.0, 46.0, InfrastructureEntity::Junction);
        coincident.prepare();
        MapBgfx2dScene &declutter_scene = coincident.scene;
        const MapBgfx2d::Camera close = MapBgfx2d::cameraFor(8.0, 46.0, 23.0, 1200, 800);
        const MapBgfx2dPoint *inlet_point = findNode(declutter_scene, inlet);
        const MapBgfx2dPoint *outlet_point = findNode(declutter_scene, outlet);
        req(inlet_point != nullptr && outlet_point != nullptr, "both coincident nodes prepared");
        if (inlet_point != nullptr && outlet_point != nullptr)
        {
            const MapBgfx2d::ScreenPoint inlet_px = MapBgfx2d::screenFromWorld(close, inlet_point->position);
            const MapBgfx2d::ScreenPoint outlet_px = MapBgfx2d::screenFromWorld(close, outlet_point->position);
            std::printf("decluttered nodes %.1f px apart at zoom 23\n",
                        std::hypot(inlet_px.x - outlet_px.x, inlet_px.y - outlet_px.y));
            req(MapBgfx2dPicking::pick(declutter_scene, close, inlet_px, options).render_id == inlet
                    && MapBgfx2dPicking::pick(declutter_scene, close, outlet_px, options).render_id == outlet,
                "each decluttered node picked at its drawn position");
        }
        MapBgfx2dMesh mesh;
        req(mesh.prepare(declutter_scene, MapBgfx2dMesh::IconLookup()), "initial mesh");
        const quint64 first_id = declutter_scene.preparationId();
        declutter_scene.setDeclutteringEnabled(false);
        req(declutter_scene.prepare(mapBgfx2dNetworkInput(coincident.retained)), "declutter toggle re-prepares");
        req(declutter_scene.preparationId() != first_id, "new preparation id");
        req(mesh.prepare(declutter_scene, MapBgfx2dMesh::IconLookup()),
            "mesh rebuilt after the declutter toggle (same geometry revision)");
        const MapBgfx2dPoint *raw_inlet = findNode(declutter_scene, inlet);
        const MapBgfx2dPoint *raw_outlet = findNode(declutter_scene, outlet);
        req(raw_inlet != nullptr && raw_outlet != nullptr
                && raw_inlet->position.x == raw_outlet->position.x
                && raw_inlet->position.y == raw_outlet->position.y,
            "without decluttering coincident nodes stay coincident");
        req(!declutter_scene.prepare(mapBgfx2dNetworkInput(coincident.retained))
                && !mesh.prepare(declutter_scene, MapBgfx2dMesh::IconLookup()),
            "no rebuild without a change");
    }

    // 5. Links crossing +-180 deg are picked on the visible side.
    {
        TestNetwork dateline;
        QVector<CoordinateWGS84> vertices;
        vertices.append(coordinate(179.9995, 0.0));
        vertices.append(coordinate(-179.9995, 0.0));
        const quint32 crossing = dateline.addLink(7, vertices, InfrastructureEntity::Pipe);
        const MapBgfx2dScene &dateline_scene = dateline.prepare();
        const MapBgfx2d::Camera east = MapBgfx2d::cameraFor(-179.9998, 0.0, 18.0, 1200, 800);
        const MapBgfx2d::Camera west = MapBgfx2d::cameraFor(179.9998, 0.0, 18.0, 1200, 800);
        req(MapBgfx2dPicking::pick(dateline_scene, east, {600.0, 402.0}, options).render_id == crossing
                && MapBgfx2dPicking::pick(dateline_scene, west, {600.0, 402.0}, options).render_id == crossing,
            "antimeridian link picked from both sides");
    }

    // 6. Entity keys and highlight subsets.
    {
        req(MapBgfx2dScene::entityKey(InfrastructureEntity::Pipe, 1)
                != MapBgfx2dScene::entityKey(InfrastructureEntity::Junction, 1),
            "entity keys separate equal render ids of different types");
        QSet<quint64> keys;
        keys.insert(MapBgfx2dScene::entityKey(InfrastructureEntity::Tank, tank));
        keys.insert(MapBgfx2dScene::entityKey(InfrastructureEntity::Pump, pump));
        const MapBgfx2dScene subset = scene.filtered(keys);
        req(subset.networkNodes().size() == 1 && subset.networkNodes().first().render_id == tank
                && subset.linkPolylines().size() == 1 && subset.linkPolylines().first().render_id == pump
                && subset.attachments().isEmpty(),
            "filtered() keeps exactly the keyed entities");
        const MapBgfx2dPoint *original_tank = findNode(scene, tank);
        req(original_tank != nullptr
                && subset.networkNodes().first().position.x == original_tank->position.x
                && subset.networkNodes().first().position.y == original_tank->position.y,
            "highlight positions are the drawn positions");
        req(subset.preparationId() != scene.preparationId() && subset.isPrepared(),
            "a subset is a prepared scene of its own");
        MapBgfx2dMesh highlight;
        highlight.setChevronsEnabled(false);
        highlight.prepare(subset, MapBgfx2dMesh::IconLookup());
        req(highlight.chevronCount() == 0 && highlight.nodeVertexCount() == 6
                && highlight.linkVertexCount() == 2 * 4 && highlight.iconVertexCount() == 0,
            "highlight mesh: link and node geometry only");
        const MapBgfx2dScene empty = scene.filtered(QSet<quint64>());
        req(empty.networkNodes().isEmpty() && empty.linkPolylines().isEmpty(), "empty selection");
    }

    std::printf(fails ? "FAILED %d\n" : "picking verification passed\n", fails);
    return fails ? 1 : 0;
}
