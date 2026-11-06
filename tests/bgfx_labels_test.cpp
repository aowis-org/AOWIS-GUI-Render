#include "map/bgfx/map_bgfx_2d_snapshot_adapter.h"
#include "map/bgfx/map_bgfx_labels.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"
#include "map/bgfx/map_bgfx_pan_reuse.h"
#include "map/render/map_retained_network_scene.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLocale>
#include <QVector>

#include <cmath>
#include <cstdio>

// Map labels on the bgfx 2D map without a GPU: which labels the decluttered
// placement keeps, where they go around their symbols, priorities, values,
// the move preview, viewport edges, the rasterization budget and stability
// while panning.
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

constexpr double ViewportWidth = 800.0;
constexpr double ViewportHeight = 600.0;
// Zoom 18 at 47 deg N: 0.0001 deg of longitude is about 18.6 px.
constexpr double Zoom = 18.0;
constexpr double NodeHalfPx = 3.0;
constexpr double IconPx = 16.0;

CoordinateWGS84 coordinate(double longitude, double latitude)
{
    CoordinateWGS84 result;
    result.longitude_deg = longitude;
    result.latitude_deg = latitude;
    return result;
}

NetworkRenderNode node(quint32 render_id, const QString &id, InfrastructureEntity type,
                       double longitude, double latitude)
{
    NetworkRenderNode result;
    result.render_id = render_id;
    result.id = id;
    result.uuid = QUuid::createUuid();
    result.entity_type = type;
    result.coordinate_wgs84 = coordinate(longitude, latitude);
    return result;
}

NetworkRenderLink link(quint32 render_id, const QString &id, InfrastructureEntity type,
                       const QVector<CoordinateWGS84> &vertices)
{
    NetworkRenderLink result;
    result.render_id = render_id;
    result.id = id;
    result.uuid = QUuid::createUuid();
    result.entity_type = type;
    for (const CoordinateWGS84 &vertex : vertices)
        result.vertices_wgs84.append(vertex);
    return result;
}

MapBgfx2d::Camera camera(double longitude = 9.0, double latitude = 47.0)
{
    return MapBgfx2d::cameraFor(longitude, latitude, Zoom, ViewportWidth, ViewportHeight);
}

MapBgfxHudPoint project(const MapBgfx2d::Camera &view, double longitude, double latitude)
{
    const MapBgfx2d::ScreenPoint point =
        MapBgfx2d::screenFromWorld(view, MapBgfx2d::worldFromWgs84(longitude, latitude));
    return MapBgfxHudPoint{point.x, point.y};
}

bool overlap(const MapBgfxHudRect &a, const MapBgfxHudRect &b)
{
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

MapBgfxHudRect nodeSymbol(InfrastructureEntity type)
{
    if (type == InfrastructureEntity::Tank || type == InfrastructureEntity::Reservoir)
        return MapBgfxHudRect{-0.5 * IconPx, -0.5 * IconPx, IconPx, IconPx};
    return MapBgfxHudRect{-NodeHalfPx, -NodeHalfPx, 2.0 * NodeHalfPx, 2.0 * NodeHalfPx};
}

MapBgfxHudRect linkSymbol(InfrastructureEntity type)
{
    if (type == InfrastructureEntity::Pipe)
        return MapBgfxHudRect();
    return MapBgfxHudRect{-0.5 * IconPx, -0.5 * IconPx, IconPx, IconPx};
}

struct Harness
{
    MapRetainedNetworkScene retained;
    MapBgfx2dScene scene;
    NetworkRenderSnapshot snapshot;
    MapBgfxLabels labels;
    MapBgfxHudTextCache cache{2048};
    MapBgfxHudCanvas canvas;
    MapLabelSettings settings;
    // Dense by render id, NaN: no value (MapNetworkRenderSymbology).
    QVector<double> node_values;
    QVector<double> link_values;
    MapBgfxLabelValueFormat node_format;
    MapBgfxLabelValueFormat link_format;
    QSet<quint64> hidden;
    QFont font;

    Harness()
    {
        this->font.setPixelSize(11);
        this->settings.node_ids = true;
        this->settings.link_ids = true;
    }
    void prepare()
    {
        ++this->snapshot.geometry_revision;
        this->retained.setSnapshot(this->snapshot);
        this->scene.setDeclutteringEnabled(false);
        this->scene.prepare(mapBgfx2dNetworkInput(this->retained));
    }
    bool build(const MapBgfx2d::Camera &view)
    {
        return build(view, QSizeF(ViewportWidth, ViewportHeight));
    }
    // A camera and viewport grown by a pan margin (MapBgfxPanReuse::Build).
    bool build(const MapBgfx2d::Camera &view, const QSizeF &viewport)
    {
        MapBgfxLabels::Inputs inputs;
        inputs.viewport = viewport;
        inputs.camera = view;
        inputs.scene = &this->scene;
        inputs.snapshot = &this->retained.snapshot();
        inputs.settings = this->settings;
        inputs.node_values = &this->node_values;
        inputs.link_values = &this->link_values;
        inputs.node_value_format = this->node_format;
        inputs.link_value_format = this->link_format;
        inputs.hidden_keys = &this->hidden;
        inputs.node_symbol = nodeSymbol;
        inputs.link_symbol = linkSymbol;
        inputs.font = this->font;
        return this->labels.build(&this->canvas, &this->cache, inputs);
    }
    const MapBgfxLabels::Placed *find(const QString &text) const
    {
        for (const MapBgfxLabels::Placed &placed : this->labels.placed())
        {
            if (placed.text == text)
                return &placed;
        }
        return nullptr;
    }
    bool placedOverlapFree() const
    {
        const QVector<MapBgfxLabels::Placed> &placed = this->labels.placed();
        for (qsizetype a = 0; a < placed.size(); ++a)
        {
            for (qsizetype b = a + 1; b < placed.size(); ++b)
            {
                if (overlap(placed.at(a).box, placed.at(b).box))
                    return false;
            }
        }
        return true;
    }
};

void testNodeAndLinkPlacement()
{
    Harness harness;
    harness.snapshot.nodes.append(node(1, "J-1", InfrastructureEntity::Junction, 8.9990, 47.0));
    harness.snapshot.nodes.append(node(2, "J-2", InfrastructureEntity::Junction, 9.0010, 47.0));
    harness.snapshot.links.append(link(1, "P-1", InfrastructureEntity::Pipe,
        {coordinate(8.9990, 47.0), coordinate(9.0010, 47.0)}));
    harness.prepare();
    const MapBgfx2d::Camera view = camera();
    req(harness.build(view), "labels: everything drawn in one build");
    req(harness.labels.stats().placed == 3, "labels: two nodes and one pipe placed");
    req(harness.labels.stats().drawn == 3, "labels: three label sprites drawn");
    req(harness.canvas.vertices().size() == 12, "labels: one quad per label");

    const MapBgfxHudPoint j1 = project(view, 8.9990, 47.0);
    const MapBgfxLabels::Placed *label_j1 = harness.find("J-1");
    req(label_j1 != nullptr, "labels: J-1 placed");
    if (label_j1 != nullptr)
    {
        // Right of the dot: symbol right + gap, vertically centred.
        req(std::fabs(label_j1->box.x - std::round(j1.x + NodeHalfPx + MapBgfxLabels::SymbolGapPx)) < 0.01,
            "labels: node label right of its dot");
        req(std::fabs(label_j1->box.y + 0.5 * label_j1->box.height - j1.y) <= 0.5 + 1e-9,
            "labels: node label vertically centred on its dot");
        req(label_j1->text_argb == MapBgfxLabels::NodeTextColor, "labels: node text colour");
    }
    const MapBgfxLabels::Placed *label_p1 = harness.find("P-1");
    req(label_p1 != nullptr, "labels: pipe P-1 placed");
    if (label_p1 != nullptr)
    {
        const MapBgfxHudPoint middle = project(view, 9.0, 47.0);
        // Horizontal pipe: centred above its middle.
        req(std::fabs(label_p1->box.x + 0.5 * label_p1->box.width - middle.x) <= 0.5 + 1e-9,
            "labels: pipe label centred on the pipe's middle");
        req(label_p1->box.bottom() <= middle.y - MapBgfxLabels::SymbolGapPx + 0.5,
            "labels: pipe label above a horizontal pipe");
        req(label_p1->text_argb == MapBgfxLabels::LinkTextColor, "labels: link text colour");
    }
    req(harness.placedOverlapFree(), "labels: placed labels never overlap");

    // Steep pipe: right of its middle.
    Harness steep;
    steep.snapshot.links.append(link(5, "P-5", InfrastructureEntity::Pipe,
        {coordinate(9.0, 46.9992), coordinate(9.0, 47.0008)}));
    steep.prepare();
    steep.build(view);
    const MapBgfxLabels::Placed *label_p5 = steep.find("P-5");
    req(label_p5 != nullptr, "labels: steep pipe labelled");
    if (label_p5 != nullptr)
    {
        const MapBgfxHudPoint middle = project(view, 9.0, 47.0);
        req(label_p5->box.x >= middle.x + MapBgfxLabels::SymbolGapPx - 0.5,
            "labels: steep pipe label right of the pipe");
    }

    // Too short on screen: no label.
    Harness short_link;
    short_link.snapshot.links.append(link(6, "P-6", InfrastructureEntity::Pipe,
        {coordinate(9.0, 47.0), coordinate(9.00005, 47.0)}));
    short_link.prepare();
    short_link.build(view);
    req(short_link.labels.stats().placed == 0, "labels: a pipe shorter than the minimum gets none");
    Harness short_pump;
    short_pump.snapshot.links.append(link(7, "PU-7", InfrastructureEntity::Pump,
        {coordinate(9.0, 47.0), coordinate(9.00005, 47.0)}));
    short_pump.prepare();
    short_pump.build(view);
    req(short_pump.find("PU-7") != nullptr, "labels: a short pump is still labelled");
}

void testDeclutterAndPriority()
{
    // A junction dot 11 px right of a tank: it blocks the tank's first
    // choice (right), the tank (higher priority) takes the next free one,
    // and no label covers a symbol.
    Harness harness;
    harness.snapshot.nodes.append(node(1, "JUNCTION-LONG-NAME", InfrastructureEntity::Junction,
                                       9.00006, 47.0));
    harness.snapshot.nodes.append(node(2, "T-1", InfrastructureEntity::Tank, 9.0, 47.0));
    harness.prepare();
    const MapBgfx2d::Camera view = camera();
    harness.build(view);
    const MapBgfxLabels::Placed *tank = harness.find("T-1");
    req(tank != nullptr, "declutter: tank labelled");
    const MapBgfxHudPoint tank_point = project(view, 9.0, 47.0);
    const MapBgfxHudPoint junction_point = project(view, 9.00006, 47.0);
    const MapBgfxHudRect tank_symbol{tank_point.x - 0.5 * IconPx, tank_point.y - 0.5 * IconPx,
                                     IconPx, IconPx};
    const MapBgfxHudRect junction_symbol{junction_point.x - NodeHalfPx,
                                         junction_point.y - NodeHalfPx,
                                         2.0 * NodeHalfPx, 2.0 * NodeHalfPx};
    for (const MapBgfxLabels::Placed &placed : harness.labels.placed())
    {
        req(!overlap(placed.box, tank_symbol), "declutter: no label over the tank icon");
        req(!overlap(placed.box, junction_symbol), "declutter: no label over the junction dot");
    }
    if (tank != nullptr)
    {
        req(std::fabs(tank->box.x - std::round(tank_symbol.right() + MapBgfxLabels::SymbolGapPx))
                > 0.01,
            "declutter: the junction dot right of the tank blocks the right position");
    }
    req(harness.placedOverlapFree(), "declutter: no overlaps");

    // Many coincident junctions: one label survives.
    Harness pile;
    for (quint32 index = 1; index <= 20; ++index)
        pile.snapshot.nodes.append(node(index, QStringLiteral("J-%1").arg(int(index)),
                                        InfrastructureEntity::Junction, 9.0, 47.0));
    pile.prepare();
    pile.build(view);
    req(pile.labels.stats().candidates == 20, "declutter: 20 candidates");
    req(pile.labels.stats().placed <= 4, "declutter: a pile keeps at most its four slots");
    req(pile.find("J-1") != nullptr, "declutter: the lowest render id wins (stable order)");
    req(pile.placedOverlapFree(), "declutter: pile without overlaps");

    req(MapBgfxLabels::priority(InfrastructureEntity::Tank, false)
            < MapBgfxLabels::priority(InfrastructureEntity::Pump, true),
        "priority: tanks before pumps");
    req(MapBgfxLabels::priority(InfrastructureEntity::Pump, true)
            < MapBgfxLabels::priority(InfrastructureEntity::Junction, false),
        "priority: pumps before junctions");
    req(MapBgfxLabels::priority(InfrastructureEntity::Junction, false)
            < MapBgfxLabels::priority(InfrastructureEntity::Pipe, true),
        "priority: junctions before pipes");
    req(MapBgfxLabels::priority(InfrastructureEntity::Pipe, true)
            < MapBgfxLabels::priority(InfrastructureEntity::DemandPoint, false),
        "priority: pipes before demand points");
}

void testValuesAndSettings()
{
    Harness harness;
    harness.snapshot.nodes.append(node(1, "J-1", InfrastructureEntity::Junction, 8.9990, 47.0));
    harness.snapshot.links.append(link(4, "PU-1", InfrastructureEntity::Pump,
        {coordinate(8.9990, 47.0), coordinate(9.0010, 47.0)}));
    harness.prepare();
    harness.node_values = {std::nan(""), 10.0};
    harness.link_values = {std::nan(""), std::nan(""), std::nan(""), std::nan(""), 0.5};
    harness.node_format.valid = true;
    harness.node_format.scale = 1.4219;
    harness.node_format.offset = 0.0;
    harness.link_format.valid = true;
    harness.link_format.scale = 1000.0;
    harness.settings.node_values = true;
    harness.settings.link_values = true;
    const MapBgfx2d::Camera view = camera();
    harness.build(view);
    req(harness.find("J-1\n14.2") != nullptr, "values: node ID and displayed value on two lines");
    req(harness.find("PU-1\n500") != nullptr, "values: link ID and displayed value");

    harness.settings.node_ids = false;
    harness.settings.link_ids = false;
    harness.build(view);
    req(harness.find("14.2") != nullptr, "values: value only");
    req(harness.find("500") != nullptr, "values: link value only");

    // No metric: value labels stay off even when requested.
    harness.node_format.valid = false;
    harness.link_values.clear();
    harness.build(view);
    req(harness.labels.stats().placed == 0, "values: nothing without a metric or a value");

    harness.settings = MapLabelSettings();
    req(harness.build(view), "settings: all off builds nothing");
    req(harness.canvas.vertices().isEmpty(), "settings: empty canvas");

    // Only node IDs: the pump's icon still blocks, it gets no label.
    harness.settings.node_ids = true;
    harness.build(view);
    req(harness.labels.stats().placed == 1 && harness.find("J-1") != nullptr,
        "settings: node IDs only");
    const MapBgfxHudPoint pump_middle = project(view, 9.0, 47.0);
    const MapBgfxHudRect pump_icon{pump_middle.x - 0.5 * IconPx, pump_middle.y - 0.5 * IconPx,
                                   IconPx, IconPx};
    for (const MapBgfxLabels::Placed &placed : harness.labels.placed())
        req(!overlap(placed.box, pump_icon), "settings: the pump icon stays an obstacle");
}

void testRename()
{
    // An ID edit bumps only the visual revision; the retained snapshot takes
    // the new IDs over and the labels follow without a geometry rebuild.
    Harness harness;
    harness.snapshot.nodes.append(node(1, "OLD", InfrastructureEntity::Junction, 9.0, 47.0));
    harness.prepare();
    const quint64 preparation = harness.scene.preparationId();
    const MapBgfx2d::Camera view = camera();
    harness.build(view);
    req(harness.find("OLD") != nullptr, "rename: old ID first");
    harness.snapshot.nodes[0].id = QStringLiteral("NEW");
    ++harness.snapshot.visual_revision;
    const MapRetainedNetworkUpdate update = harness.retained.setSnapshot(harness.snapshot);
    req(!update.geometry_changed && update.visual_changed, "rename: visual update only");
    harness.scene.prepare(mapBgfx2dNetworkInput(harness.retained));
    req(harness.scene.preparationId() == preparation, "rename: no re-preparation");
    harness.build(view);
    req(harness.find("NEW") != nullptr && harness.find("OLD") == nullptr,
        "rename: the label shows the new ID");
}

void testHiddenAndViewport()
{
    Harness harness;
    harness.snapshot.nodes.append(node(1, "J-1", InfrastructureEntity::Junction, 9.0, 47.0));
    harness.snapshot.nodes.append(node(2, "J-2", InfrastructureEntity::Junction, 9.0005, 47.0));
    harness.prepare();
    harness.hidden.insert(MapBgfx2dScene::entityKey(InfrastructureEntity::Junction, 2));
    const MapBgfx2d::Camera view = camera();
    harness.build(view);
    req(harness.find("J-1") != nullptr && harness.find("J-2") == nullptr,
        "hidden: a moved (hidden) node has no label");

    // A node at the right edge: its label flips to the left.
    Harness edge;
    const double longitude_right = 9.0 + (0.5 * ViewportWidth - 8.0) / 186413.0 * 1.0;
    edge.snapshot.nodes.append(node(1, "EDGE-NODE", InfrastructureEntity::Junction,
                                    longitude_right, 47.0));
    edge.prepare();
    edge.build(view);
    const MapBgfxLabels::Placed *label = edge.find("EDGE-NODE");
    req(label != nullptr, "viewport: edge node labelled");
    if (label != nullptr)
    {
        const MapBgfxHudPoint point = project(view, longitude_right, 47.0);
        req(label->box.right() <= point.x, "viewport: label left of a node at the right edge");
        req(label->box.right() <= ViewportWidth - MapBgfxLabels::ViewportMarginPx,
            "viewport: label inside the viewport");
    }

    // Off screen: no candidate.
    Harness away;
    away.snapshot.nodes.append(node(1, "FAR", InfrastructureEntity::Junction, 9.1, 47.0));
    away.prepare();
    away.build(view);
    req(away.labels.stats().candidates == 0, "viewport: off-screen nodes are no candidates");

    // A long pipe crossing the screen far beyond both edges is labelled at
    // the middle of its visible part.
    Harness across;
    across.snapshot.links.append(link(1, "LONG", InfrastructureEntity::Pipe,
        {coordinate(8.99, 47.0), coordinate(9.03, 47.0)}));
    across.prepare();
    across.build(view);
    const MapBgfxLabels::Placed *long_label = across.find("LONG");
    req(long_label != nullptr, "viewport: long pipe labelled");
    if (long_label != nullptr)
    {
        req(std::fabs(long_label->box.x + 0.5 * long_label->box.width - 0.5 * ViewportWidth) <= 1.0,
            "viewport: long pipe labelled at the middle of the screen");
    }
}

void testBudgetAndStability()
{
    // A grid of 400 junctions 3 grid cells apart: more labels than one
    // build may rasterize.
    Harness harness;
    quint32 render_id = 1;
    for (int row = 0; row < 20; ++row)
    {
        for (int column = 0; column < 20; ++column)
        {
            harness.snapshot.nodes.append(node(render_id, QStringLiteral("N%1").arg(int(render_id)),
                                               InfrastructureEntity::Junction,
                                               8.9982 + 0.00019 * column,
                                               46.9990 + 0.00010 * row));
            ++render_id;
        }
    }
    harness.prepare();
    const MapBgfx2d::Camera view = camera();
    const bool complete = harness.build(view);
    const int placed = harness.labels.stats().placed;
    req(placed > MapBgfxLabels::MaximumRasterizationsPerBuild,
        "budget: more labels placed than one build rasterizes");
    req(!complete && harness.labels.stats().deferred
                         == placed - MapBgfxLabels::MaximumRasterizationsPerBuild,
        "budget: the rest is deferred");
    req(harness.placedOverlapFree(), "budget: no overlaps in a dense grid");
    // Builds until complete, the first one included.
    int builds = 1;
    bool done = false;
    while (!done && builds < 10)
    {
        done = harness.build(view);
        ++builds;
    }
    req(harness.labels.stats().deferred == 0 && harness.labels.stats().drawn == placed,
        "budget: later builds draw everything");
    req(builds == int(std::ceil(double(placed) / MapBgfxLabels::MaximumRasterizationsPerBuild)),
        "budget: one batch of rasterizations per build");

    // Panning by whole pixels keeps every label relative to the network.
    QHash<QString, MapBgfxHudRect> before;
    for (const MapBgfxLabels::Placed &placed_label : harness.labels.placed())
        before.insert(placed_label.text, placed_label.box);
    MapBgfx2d::Camera panned = view;
    panned.center.x += 10.0 / view.world_pixels;
    harness.build(panned);
    int moved_consistently = 0;
    int compared = 0;
    for (const MapBgfxLabels::Placed &placed_label : harness.labels.placed())
    {
        const QHash<QString, MapBgfxHudRect>::const_iterator found =
            before.constFind(placed_label.text);
        if (found == before.cend())
            continue;
        ++compared;
        if (std::fabs(found.value().x - 10.0 - placed_label.box.x) < 1.01
            && std::fabs(found.value().y - placed_label.box.y) < 0.01)
            ++moved_consistently;
    }
    req(compared > placed / 2, "stability: most labels survive a 10 px pan");
    req(moved_consistently == compared, "stability: labels move with the map");
}

// Pure pans reuse the last labels translated (MapBgfxSurface::renderLabels()
// with MapBgfxPanReuse): a build with the pan margin, drawn with the reuse
// offset, shows every label of an exact build at the same place, and the
// labels beyond the edge are already there when they scroll in.
void testPanReuse()
{
    Harness harness;
    quint32 render_id = 1;
    // Sparse grid (no conflicts), wider than the viewport.
    for (int row = 0; row < 6; ++row)
    {
        for (int column = 0; column < 14; ++column)
        {
            harness.snapshot.nodes.append(node(render_id, QStringLiteral("P%1").arg(int(render_id)),
                                               InfrastructureEntity::Junction,
                                               8.9970 + 0.00045 * column,
                                               46.9992 + 0.00025 * row));
            ++render_id;
        }
    }
    harness.prepare();
    const QSizeF viewport(ViewportWidth, ViewportHeight);
    const double ratio = 1.25;
    harness.cache.setDevicePixelRatio(ratio);
    MapBgfxPanReuse pan;
    const MapBgfx2d::Camera start = camera();

    // At rest: no margin, reused only for the identical camera.
    MapBgfxPanReuse::Build build = pan.begin(start, viewport, ratio, true);
    req(build.margin == 0.0 && build.offset.isNull(), "pan reuse: no margin at rest");
    QPointF offset;
    req(pan.reuse(start, viewport, ratio, &offset) && offset.isNull(),
        "pan reuse: unchanged camera reuses without offset");
    MapBgfx2d::Camera panned = start;
    panned.center.x += 3.0 / start.world_pixels;
    req(!pan.reuse(panned, viewport, ratio, &offset), "pan reuse: no margin, no pan reuse");

    // The first pan frame rebuilds with the margin.
    build = pan.begin(panned, viewport, ratio, true);
    const double expected_margin = std::ceil(MapBgfxPanReuse::MarginFraction * ViewportWidth * ratio) / ratio;
    req(std::fabs(build.margin - expected_margin) < 1e-9, "pan reuse: margin of a fifth of the screen");
    req(std::fabs(build.margin * ratio - std::round(build.margin * ratio)) < 1e-9,
        "pan reuse: margin is whole physical pixels");
    req(build.viewport.width() == ViewportWidth + 2.0 * build.margin
            && build.camera.viewport_width == start.viewport_width + 2.0 * build.margin,
        "pan reuse: grown viewport and camera");
    int builds = 0;
    while (!harness.build(build.camera, build.viewport) && builds < 10)
        ++builds;
    const QVector<MapBgfxLabels::Placed> grown = harness.labels.placed();

    // Later frames within the margin: translated, on the physical pixel grid.
    MapBgfx2d::Camera later = panned;
    later.center.x += 97.3 / start.world_pixels;
    later.center.y -= 41.6 / start.world_pixels;
    req(pan.reuse(later, viewport, ratio, &offset), "pan reuse: pan within the margin reuses");
    req(std::fabs(offset.x() * ratio - std::round(offset.x() * ratio)) < 1e-9
            && std::fabs(offset.y() * ratio - std::round(offset.y() * ratio)) < 1e-9,
        "pan reuse: offset on the physical pixel grid");

    // The exact build for that camera agrees with the translated one.
    builds = 0;
    while (!harness.build(later) && builds < 10)
        ++builds;
    int compared = 0;
    int agreeing = 0;
    for (const MapBgfxLabels::Placed &exact : harness.labels.placed())
    {
        if (exact.box.x < 0.0 || exact.box.y < 0.0 || exact.box.right() > ViewportWidth
            || exact.box.bottom() > ViewportHeight)
            continue;
        ++compared;
        for (const MapBgfxLabels::Placed &reused : grown)
        {
            if (reused.text == exact.text
                && std::fabs(reused.box.x + offset.x() - exact.box.x) <= 1.0 / ratio + 1e-6
                && std::fabs(reused.box.y + offset.y() - exact.box.y) <= 1.0 / ratio + 1e-6)
            {
                ++agreeing;
                break;
            }
        }
    }
    req(compared > 20, "pan reuse: labels to compare");
    req(agreeing == compared, "pan reuse: translated labels sit where an exact build puts them");
    // Labels right of the screen when the margin build was made are in it.
    const MapBgfxHudPoint far_right = project(panned, 8.9970 + 0.00045 * 12, 46.9992);
    req(far_right.x > ViewportWidth && far_right.x < ViewportWidth + build.margin - 60.0,
        "pan reuse: test node beyond the right edge, inside the margin");
    bool far_right_built = false;
    for (const MapBgfxLabels::Placed &reused : grown)
        far_right_built = far_right_built || reused.text == QStringLiteral("P13");
    req(far_right_built, "pan reuse: labels beyond the edge are built in the margin");

    // Leaving the margin, zooming or resizing rebuilds; zooming without margin.
    MapBgfx2d::Camera beyond = panned;
    beyond.center.x += (build.margin + 1.0) / start.world_pixels;
    req(!pan.reuse(beyond, viewport, ratio, &offset), "pan reuse: leaving the margin rebuilds");
    MapBgfx2d::Camera zoomed = later;
    zoomed.world_pixels *= 1.01;
    req(!pan.reuse(zoomed, viewport, ratio, &offset), "pan reuse: zoom rebuilds");
    req(!pan.reuse(later, QSizeF(ViewportWidth + 1.0, ViewportHeight), ratio, &offset),
        "pan reuse: resize rebuilds");
    req(pan.begin(zoomed, viewport, ratio, true).margin == 0.0, "pan reuse: zoom frames get no margin");
    req(pan.begin(later, viewport, ratio, false).margin == 0.0,
        "pan reuse: screen-anchored content gets no margin");
}

void testAtlasLimit()
{
    // A small atlas holds only a few labels: placement stops before the
    // frame would overflow it, so nothing is ever evicted mid-frame.
    Harness harness;
    for (quint32 index = 1; index <= 60; ++index)
    {
        harness.snapshot.nodes.append(node(index, QStringLiteral("LONG-JUNCTION-NAME-%1").arg(int(index)),
                                           InfrastructureEntity::Junction,
                                           8.9990, 46.9985 + 0.00005 * double(index)));
    }
    harness.prepare();
    MapBgfxHudTextCache small{256};
    MapBgfxLabels labels;
    MapBgfxHudCanvas canvas;
    MapBgfxLabels::Inputs inputs;
    inputs.viewport = QSizeF(ViewportWidth, ViewportHeight);
    inputs.camera = camera();
    inputs.scene = &harness.scene;
    inputs.snapshot = &harness.retained.snapshot();
    inputs.settings = harness.settings;
    inputs.node_symbol = nodeSymbol;
    inputs.link_symbol = linkSymbol;
    inputs.font = harness.font;
    labels.build(&canvas, &small, inputs);
    double footprint = 0.0;
    for (const MapBgfxLabels::Placed &placed : labels.placed())
        footprint += small.haloTextFootprint(QSizeF(placed.box.width, placed.box.height),
                                             MapBgfxLabels::HaloPx);
    req(labels.stats().placed > 0, "atlas: some labels placed");
    req(labels.stats().placed < labels.stats().candidates, "atlas: placement capped");
    req(footprint <= MapBgfxLabels::AtlasFillLimit * small.capacity(),
        "atlas: the frame's labels fit into the atlas");
    req(!small.evictedSinceFrameStart(), "atlas: no eviction while drawing");
    req(labels.stats().drawn == labels.stats().placed, "atlas: every placed label drawn");
}

void testHaloSprite()
{
    MapBgfxHudTextCache cache{512};
    QFont font;
    font.setPixelSize(10);
    const QSizeF block = cache.measureBlock(QStringLiteral("AB\nC"), font);
    const QFontMetricsF metrics(font);
    req(std::fabs(block.width() - metrics.horizontalAdvance(QStringLiteral("AB"))) < 1e-9,
        "halo: block width is the widest line");
    req(std::fabs(block.height() - 2.0 * metrics.height()) < 1e-9, "halo: block height per line");
    req(!cache.containsHaloText(QStringLiteral("AB\nC"), font, 0xff000000u, 0xffffffffu, 1.5),
        "halo: not cached before");
    const MapBgfxHudSprite sprite =
        cache.haloText(QStringLiteral("AB\nC"), font, 0xff000000u, 0xffffffffu, 1.5);
    req(sprite.valid, "halo: sprite stored");
    req(cache.containsHaloText(QStringLiteral("AB\nC"), font, 0xff000000u, 0xffffffffu, 1.5),
        "halo: cached after");
    req(std::fabs(sprite.origin_x - (1.5 + 1.0)) < 1e-9 && std::fabs(sprite.origin_y - 2.5) < 1e-9,
        "halo: layout origin inside the sprite (halo + padding)");
    req(sprite.width >= block.width() + 5.0 && sprite.height >= block.height() + 5.0,
        "halo: sprite covers block and halo");
    req(!cache.containsHaloText(QStringLiteral("AB\nC"), font, 0xff000000u, 0xffffffffu, 2.0),
        "halo: the outline width is part of the key");
}
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    // Values are formatted with the default locale.
    QLocale::setDefault(QLocale::c());

    testNodeAndLinkPlacement();
    testDeclutterAndPriority();
    testValuesAndSettings();
    testRename();
    testHiddenAndViewport();
    testBudgetAndStability();
    testPanReuse();
    testAtlasLimit();
    testHaloSprite();

    if (fails == 0)
        std::printf("bgfx map label tests passed\n");
    return fails == 0 ? 0 : 1;
}
