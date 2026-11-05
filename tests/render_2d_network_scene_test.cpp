#include "map/bgfx/map_bgfx_2d_scene_variants.h"
#include "map/bgfx/map_bgfx_2d_mesh.h"

#include <cstdio>

static bool check(bool ok, const char *msg)
{
    if (!ok)
        std::fprintf(stderr, "2d network: %s\n", msg);
    return ok;
}

int main()
{
    MapBgfx2dNetworkInput input;
    input.valid = true;
    input.geometry_revision = 9;
    input.nodes.append({4, 99, {8.0, 46.0}, true});
    input.nodes.append({5, 98, {8.0, 46.0}, false});
    MapBgfx2dInputLink line;
    line.render_id = 7;
    line.entity_type = 97;
    line.pipe = true;
    line.start_node_render_id = 4;
    line.end_node_render_id = 5;
    line.vertices.append({8.0, 46.0});
    line.vertices.append({8.0, 46.0});
    input.links.append(line);
    input.attachments.append({5, {8.0, 46.0}, {8.001, 46.0}});
    MapBgfx2dSceneVariants variants;
    if (!check(variants.prepare(input), "first preparation failed")
        || !check(variants.active().networkNodes().size() == 2, "node count")
        || !check(variants.active().linkPolylines().size() == 1, "link count")
        || !check(variants.active().attachments().size() == 1, "attachment count")
        || !check(!variants.prepare(input), "unchanged revision re-prepared"))
        return 1;
    const MapBgfx2d::WorldPoint first = variants.active().networkNodes()[0].position;
    const MapBgfx2d::WorldPoint second = variants.active().networkNodes()[1].position;
    if (!check(first.x != second.x || first.y != second.y, "decluttering ineffective"))
        return 1;
    MapBgfx2dMesh mesh;
    if (!check(mesh.prepare(variants.active(), {}), "mesh staging failed")
        || !check(mesh.nodeVertexCount() == 12, "node vertices")
        || !check(mesh.linkIndexCount() == 6, "link indices")
        || !check(mesh.attachmentVertexCount() == 4, "attachment vertices"))
        return 1;
    if (!check(variants.setDeclutteringEnabled(false, input), "editor variant missing"))
        return 1;
    const MapBgfx2d::WorldPoint plain_first = variants.active().networkNodes()[0].position;
    const MapBgfx2d::WorldPoint plain_second = variants.active().networkNodes()[1].position;
    if (!check(plain_first.x == plain_second.x && plain_first.y == plain_second.y,
               "editor changed exact geometry"))
        return 1;
    return 0;
}
