#include "map/bgfx/map_bgfx_entity_style.h"
#include "map/render/map_icon_atlas.h"

#include <QGuiApplication>

#include <cmath>
#include <cstdio>
#include <limits>

// GPU network colouring data (MapBgfxEntityStyle): the per-entity texels the
// vertex shaders read (shaders/entity_style.sh). CPU side only: prepare()
// builds the texels without a bgfx device.
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

const double NoValue = std::numeric_limits<double>::quiet_NaN();

NetworkRenderNode node(quint32 render_id, InfrastructureEntity entity_type)
{
    NetworkRenderNode result;
    result.render_id = render_id;
    result.entity_type = entity_type;
    return result;
}

NetworkRenderLink link(quint32 render_id, InfrastructureEntity entity_type)
{
    NetworkRenderLink result;
    result.render_id = render_id;
    result.entity_type = entity_type;
    return result;
}

// The texel of a render id (x, y, z, w).
const float *texel(const QVector<float> &texels, quint32 render_id)
{
    return texels.constData() + qsizetype(render_id) * 4;
}

bool hasFlag(const float *value, quint32 flag)
{
    return (quint32(value[2]) & flag) != 0;
}
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);

    NetworkRenderSnapshot snapshot;
    snapshot.nodes.append(node(1, InfrastructureEntity::Junction));
    snapshot.nodes.append(node(2, InfrastructureEntity::Tank));
    snapshot.nodes.append(node(3, InfrastructureEntity::DemandPoint));
    snapshot.links.append(link(1, InfrastructureEntity::Pipe));
    snapshot.links.append(link(2, InfrastructureEntity::Pump));

    MapBgfxEntityStyle style;
    style.setEntities(snapshot);
    MapNetworkRenderSymbology symbology;
    symbology.visual_node = VisualNode::Pressure;
    symbology.node_values = {NoValue, 5.0, NoValue, 7.0};
    symbology.visual_heatmap = VisualHeatmap::Pressure;
    symbology.heatmap_values = {NoValue, NoValue, 3.0, NoValue};
    symbology.visual_link = VisualLink::FlowRate;
    symbology.link_values = {NoValue, 1.5, NoValue};
    symbology.flow_directions = {0, -1, 1};
    style.setSymbology(symbology);
    style.prepare();

    const QVector<float> &nodes = style.nodeTexels();
    const QVector<float> &links = style.linkTexels();
    // 1. Layout: whole rows of TextureWidth RGBA texels, texel = render id.
    req(nodes.size() == MapBgfxEntityStyle::TextureWidth * 4, "node texels: one full row");
    req(links.size() == MapBgfxEntityStyle::TextureWidth * 4, "link texels: one full row");

    // 2. Values and their flags.
    req(texel(nodes, 1)[0] == 5.0f && hasFlag(texel(nodes, 1), MapBgfxEntityStyle::HasValue),
        "junction value and HasValue");
    req(!hasFlag(texel(nodes, 2), MapBgfxEntityStyle::HasValue), "NaN: no value");
    req(texel(nodes, 3)[0] == 7.0f, "demand point value");
    req(texel(nodes, 2)[1] == 3.0f && hasFlag(texel(nodes, 2), MapBgfxEntityStyle::HasHeat),
        "heatmap value in y");
    req(!hasFlag(texel(nodes, 1), MapBgfxEntityStyle::HasHeat), "no heatmap value: no spot");
    req(texel(links, 1)[0] == 1.5f && texel(links, 1)[1] == -1.0f, "link value and reverse flow");
    req(texel(links, 2)[1] == 1.0f && !hasFlag(texel(links, 2), MapBgfxEntityStyle::HasValue),
        "pump flow without a colour value");

    // 3. Entity classes: junctions and devices (base colours), icons.
    req(hasFlag(texel(nodes, 1), MapBgfxEntityStyle::Kind)
            && !hasFlag(texel(nodes, 2), MapBgfxEntityStyle::Kind),
        "Kind marks junctions only");
    req(hasFlag(texel(links, 2), MapBgfxEntityStyle::Kind)
            && !hasFlag(texel(links, 1), MapBgfxEntityStyle::Kind),
        "Kind marks pumps and valves, not pipes");
    req(hasFlag(texel(nodes, 2), MapBgfxEntityStyle::HasIcon) == mapHasIcon(InfrastructureEntity::Tank)
            && hasFlag(texel(links, 2), MapBgfxEntityStyle::HasIcon) == mapHasIcon(InfrastructureEntity::Pump)
            && !hasFlag(texel(nodes, 1), MapBgfxEntityStyle::HasIcon),
        "HasIcon follows mapHasIcon()");

    // 4. Hidden / selected / diagnostics by entity key: render ids are only
    //    unique per class, so junction 1 never marks pipe 1.
    const quint64 junction_1 = (quint64(quint8(InfrastructureEntity::Junction)) << 32) | 1u;
    const quint64 pump_2 = (quint64(quint8(InfrastructureEntity::Pump)) << 32) | 2u;
    const quint64 demand_point_3 = (quint64(quint8(InfrastructureEntity::DemandPoint)) << 32) | 3u;
    const quint64 pipe_1 = (quint64(quint8(InfrastructureEntity::Pipe)) << 32) | 1u;
    style.setHiddenEntities({junction_1});
    style.setSelectedEntity(pump_2);
    QHash<quint64, bool> diagnostics;
    diagnostics.insert(demand_point_3, true);
    diagnostics.insert(pipe_1, false);
    style.setDiagnosticEntities(diagnostics);
    style.prepare();
    req(hasFlag(texel(style.nodeTexels(), 1), MapBgfxEntityStyle::Hidden)
            && !hasFlag(texel(style.linkTexels(), 1), MapBgfxEntityStyle::Hidden),
        "hidden junction 1, not pipe 1");
    req(hasFlag(texel(style.linkTexels(), 2), MapBgfxEntityStyle::Selected)
            && !hasFlag(texel(style.nodeTexels(), 2), MapBgfxEntityStyle::Selected),
        "selected pump 2, not tank 2");
    req(hasFlag(texel(style.nodeTexels(), 3), MapBgfxEntityStyle::Error)
            && hasFlag(texel(style.nodeTexels(), 3), MapBgfxEntityStyle::Stale),
        "stale diagnostic: Error and Stale");
    req(hasFlag(texel(style.linkTexels(), 1), MapBgfxEntityStyle::Error)
            && !hasFlag(texel(style.linkTexels(), 1), MapBgfxEntityStyle::Stale),
        "error diagnostic: Error only");
    req(texel(style.nodeTexels(), 1)[0] == 5.0f, "state flags keep the values");

    // 5. A time step: new values, the same classes and states.
    symbology.node_values = {NoValue, 6.0, 8.0, NoValue};
    symbology.flow_directions = {0, 1, 0};
    style.setSymbology(symbology);
    style.prepare();
    req(texel(style.nodeTexels(), 1)[0] == 6.0f && texel(style.nodeTexels(), 2)[0] == 8.0f
            && !hasFlag(texel(style.nodeTexels(), 3), MapBgfxEntityStyle::HasValue),
        "time step: values replaced");
    req(texel(style.linkTexels(), 1)[1] == 1.0f && texel(style.linkTexels(), 2)[1] == 0.0f,
        "time step: flow directions replaced");
    req(hasFlag(texel(style.nodeTexels(), 1), MapBgfxEntityStyle::Hidden),
        "time step: hidden state kept");

    // 6. Visual None: no per-entity values at all (base colours).
    MapNetworkRenderSymbology none;
    style.setSymbology(none);
    style.prepare();
    req(!hasFlag(texel(style.nodeTexels(), 1), MapBgfxEntityStyle::HasValue)
            && hasFlag(texel(style.nodeTexels(), 1), MapBgfxEntityStyle::Kind),
        "visual None: classes only");

    // 7. Large networks: rows grow with the render id range.
    NetworkRenderSnapshot large;
    for (quint32 render_id = 1; render_id <= 5000; ++render_id)
        large.nodes.append(node(render_id, InfrastructureEntity::Junction));
    style.setEntities(large);
    style.prepare();
    req(style.nodeTexels().size() == 3 * MapBgfxEntityStyle::TextureWidth * 4,
        "5001 slots: three rows");
    req(hasFlag(texel(style.nodeTexels(), 5000), MapBgfxEntityStyle::Kind), "last render id in the last row");

    std::printf(fails ? "FAILED %d\n" : "bgfx entity style tests passed\n", fails);
    return fails ? 1 : 0;
}
