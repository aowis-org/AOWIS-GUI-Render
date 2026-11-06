#include "map/bgfx/map_bgfx_labels.h"

#include "map/bgfx/map_bgfx_hud.h"
#include "map/render/map_network_render_symbology.h"

#include <algorithm>
#include <cmath>

namespace
{
bool rectsOverlap(const MapBgfxHudRect &a, const MapBgfxHudRect &b)
{
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

MapBgfxHudRect expanded(const MapBgfxHudRect &rect, double margin)
{
    return MapBgfxHudRect{rect.x - margin, rect.y - margin,
                          rect.width + 2.0 * margin, rect.height + 2.0 * margin};
}

// A symbol box placed at a screen anchor.
MapBgfxHudRect symbolAt(const MapBgfxLabels::SymbolRect &symbol, InfrastructureEntity entity_type,
                        double x, double y)
{
    MapBgfxHudRect rect;
    if (symbol)
        rect = symbol(entity_type);
    if (!(rect.width > 0.0) || !(rect.height > 0.0))
        return MapBgfxHudRect{x, y, 0.0, 0.0};
    rect.x += x;
    rect.y += y;
    return rect;
}

// Size of the text cache measurement memo before it is dropped (values of a
// running simulation produce new texts every time step).
constexpr qsizetype MaximumCachedSizes = 40000;
}

int MapBgfxLabels::priority(InfrastructureEntity entity_type, bool link)
{
    if (link)
        return entity_type == InfrastructureEntity::Pipe ? 3 : 1;
    switch (entity_type)
    {
        case InfrastructureEntity::Tank:
        case InfrastructureEntity::Reservoir:
            return 0;
        case InfrastructureEntity::DemandPoint:
            return 4;
        default:
            return 2;
    }
}

void MapBgfxLabels::synchronizeIdentity(const MapBgfx2dScene &scene,
                                        const NetworkRenderSnapshot &snapshot)
{
    // IDs change without geometry (renames bump the visual revision only).
    if (this->identity_preparation_id == scene.preparationId()
        && this->identity_snapshot == &snapshot
        && this->identity_geometry_revision == snapshot.geometry_revision
        && this->identity_visual_revision == snapshot.visual_revision)
    {
        return;
    }
    this->id_by_entity_key.clear();
    this->id_by_entity_key.reserve(snapshot.nodes.size() + snapshot.links.size());
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        this->id_by_entity_key.insert(
            MapBgfx2dScene::entityKey(node.entity_type, node.render_id), node.id);
    }
    for (const NetworkRenderLink &link : snapshot.links)
    {
        this->id_by_entity_key.insert(
            MapBgfx2dScene::entityKey(link.entity_type, link.render_id), link.id);
    }
    this->identity_preparation_id = scene.preparationId();
    this->identity_snapshot = &snapshot;
    this->identity_geometry_revision = snapshot.geometry_revision;
    this->identity_visual_revision = snapshot.visual_revision;
}

QString MapBgfxLabels::labelText(const QString &id, bool show_id, bool show_value,
                                 const QVector<double> *values,
                                 const MapBgfxLabelValueFormat &format, quint32 render_id) const
{
    QString value_text;
    if (show_value && format.valid && values != nullptr)
    {
        const double value = mapNetworkSymbologyValue(*values, render_id);
        if (std::isfinite(value))
            value_text = MapBgfxHud::formatLegendValue(format.displayed(value));
    }
    const bool with_id = show_id && !id.isEmpty();
    if (with_id && !value_text.isEmpty())
        return id + QLatin1Char('\n') + value_text;
    if (with_id)
        return id;
    return value_text;
}

QSizeF MapBgfxLabels::blockSize(const QString &text, MapBgfxHudTextCache *cache, const QFont &font)
{
    const QHash<QString, QSizeF>::const_iterator found = this->block_size_by_text.constFind(text);
    if (found != this->block_size_by_text.cend())
        return found.value();
    if (this->block_size_by_text.size() >= MaximumCachedSizes)
        this->block_size_by_text.clear();
    const QSizeF size = cache->measureBlock(text, font);
    this->block_size_by_text.insert(text, size);
    return size;
}

void MapBgfxLabels::resetGrid(const QSizeF &viewport)
{
    const int columns = std::max(1, int(std::ceil(viewport.width() / GridCellPx)));
    const int rows = std::max(1, int(std::ceil(viewport.height() / GridCellPx)));
    if (columns != this->grid_columns || rows != this->grid_rows)
    {
        this->grid_columns = columns;
        this->grid_rows = rows;
        this->grid_cells = QVector<QVector<qsizetype>>(qsizetype(columns) * qsizetype(rows));
    }
    else
    {
        for (QVector<qsizetype> &cell : this->grid_cells)
            cell.clear();
    }
    this->label_rects.clear();
    this->obstacle_rects.clear();
}

void MapBgfxLabels::cellRange(const MapBgfxHudRect &rect, int *x0, int *y0, int *x1, int *y1) const
{
    *x0 = std::clamp(int(std::floor(rect.x / GridCellPx)), 0, this->grid_columns - 1);
    *y0 = std::clamp(int(std::floor(rect.y / GridCellPx)), 0, this->grid_rows - 1);
    *x1 = std::clamp(int(std::floor(rect.right() / GridCellPx)), 0, this->grid_columns - 1);
    *y1 = std::clamp(int(std::floor(rect.bottom() / GridCellPx)), 0, this->grid_rows - 1);
}

bool MapBgfxLabels::overlaps(const MapBgfxHudRect &rect, qsizetype ignore_obstacle) const
{
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    cellRange(rect, &x0, &y0, &x1, &y1);
    for (int row = y0; row <= y1; ++row)
    {
        for (int column = x0; column <= x1; ++column)
        {
            const QVector<qsizetype> &cell =
                this->grid_cells.at(qsizetype(row) * this->grid_columns + column);
            for (const qsizetype entry : cell)
            {
                if (entry >= 0)
                {
                    if (rectsOverlap(rect, this->label_rects.at(entry)))
                        return true;
                }
                else
                {
                    const qsizetype obstacle = ~entry;
                    if (obstacle != ignore_obstacle
                        && rectsOverlap(rect, this->obstacle_rects.at(obstacle)))
                        return true;
                }
            }
        }
    }
    return false;
}

void MapBgfxLabels::insertLabel(const MapBgfxHudRect &rect)
{
    const qsizetype index = this->label_rects.size();
    this->label_rects.append(rect);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    cellRange(rect, &x0, &y0, &x1, &y1);
    for (int row = y0; row <= y1; ++row)
    {
        for (int column = x0; column <= x1; ++column)
            this->grid_cells[qsizetype(row) * this->grid_columns + column].append(index);
    }
}

qsizetype MapBgfxLabels::insertObstacle(const MapBgfxHudRect &rect)
{
    if (!(rect.width > 0.0) || !(rect.height > 0.0))
        return -1;
    const qsizetype index = this->obstacle_rects.size();
    this->obstacle_rects.append(rect);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    cellRange(rect, &x0, &y0, &x1, &y1);
    for (int row = y0; row <= y1; ++row)
    {
        for (int column = x0; column <= x1; ++column)
            this->grid_cells[qsizetype(row) * this->grid_columns + column].append(~index);
    }
    return index;
}

bool MapBgfxLabels::build(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                          const Inputs &inputs)
{
    canvas->clear();
    canvas->setWhiteTexel(cache->whiteU(), cache->whiteV());
    this->placed_labels.clear();
    this->last_stats = Stats();
    if (inputs.scene == nullptr || inputs.snapshot == nullptr || !inputs.settings.anyEnabled()
        || !(inputs.viewport.width() > 0.0) || !(inputs.viewport.height() > 0.0))
    {
        return true;
    }
    const MapLabelSettings &settings = inputs.settings;
    const bool node_values = settings.node_values && inputs.node_value_format.valid;
    const bool link_values = settings.link_values && inputs.link_value_format.valid;
    const bool label_nodes = settings.node_ids || node_values;
    const bool label_links = settings.link_ids || link_values;
    if (!label_nodes && !label_links)
        return true;

    synchronizeIdentity(*inputs.scene, *inputs.snapshot);
    if (this->size_cache_font_key != inputs.font.key())
    {
        this->size_cache_font_key = inputs.font.key();
        this->block_size_by_text.clear();
    }

    const MapBgfx2d::Camera &camera = inputs.camera;
    const double width = inputs.viewport.width();
    const double height = inputs.viewport.height();
    // Symbols up to this far outside still block labels near the edge.
    const double reach = 64.0;
    const std::function<bool(double, double)> near_viewport = [width, height, reach](double x, double y)
    {
        return x >= -reach && y >= -reach && x <= width + reach && y <= height + reach;
    };
    const std::function<bool(quint64)> hidden = [&inputs](quint64 key)
    {
        return inputs.hidden_keys != nullptr && !inputs.hidden_keys->isEmpty()
            && inputs.hidden_keys->contains(key);
    };

    resetGrid(inputs.viewport);

    // Nodes on screen: obstacles first, candidates collected on the way.
    struct NodeOnScreen
    {
        qsizetype scene_index = 0;
        double x = 0.0;
        double y = 0.0;
    };
    QVector<NodeOnScreen> nodes_on_screen;
    const QVector<MapBgfx2dPoint> &nodes = inputs.scene->networkNodes();
    for (qsizetype index = 0; index < nodes.size(); ++index)
    {
        const MapBgfx2dPoint &node = nodes.at(index);
        const MapBgfx2d::ScreenPoint point = MapBgfx2d::screenFromWorld(camera, node.position);
        if (!near_viewport(point.x, point.y))
            continue;
        if (hidden(MapBgfx2dScene::entityKey(node.entity_type, node.render_id)))
            continue;
        NodeOnScreen on_screen;
        on_screen.scene_index = index;
        on_screen.x = point.x;
        on_screen.y = point.y;
        nodes_on_screen.append(on_screen);
    }
    this->last_stats.dense = nodes_on_screen.size() > DenseNodeCount;
    // Dense: only the few important entities (tanks, reservoirs, pumps,
    // valves) are labelled, and only they block.
    const int lowest_priority = this->last_stats.dense ? 1 : 4;

    QVector<Candidate> candidates;
    QVector<qsizetype> candidate_obstacle;
    for (const NodeOnScreen &on_screen : nodes_on_screen)
    {
        const MapBgfx2dPoint &node = nodes.at(on_screen.scene_index);
        const int node_priority = priority(static_cast<InfrastructureEntity>(node.entity_type), false);
        if (node_priority > lowest_priority)
            continue;
        const MapBgfxHudRect symbol =
            symbolAt(inputs.node_symbol, static_cast<InfrastructureEntity>(node.entity_type), on_screen.x, on_screen.y);
        const qsizetype obstacle = insertObstacle(symbol);
        if (!label_nodes)
            continue;
        const quint64 key = MapBgfx2dScene::entityKey(node.entity_type, node.render_id);
        Candidate candidate;
        candidate.priority = node_priority;
        candidate.render_id = node.render_id;
        candidate.entity_key = key;
        candidate.link = false;
        candidate.symbol = symbol;
        candidate.text = labelText(this->id_by_entity_key.value(key), settings.node_ids,
                                   node_values, inputs.node_values, inputs.node_value_format,
                                   node.render_id);
        if (candidate.text.isEmpty())
            continue;
        candidates.append(candidate);
        candidate_obstacle.append(obstacle);
    }

    // Links: middle of the on-screen length; device icons are obstacles.
    // A link split by invalid vertices is labelled once (its first piece
    // long enough).
    QSet<quint64> labelled_links;
    const QVector<MapBgfx2dPolyline> &links = inputs.scene->linkPolylines();
    for (const MapBgfx2dPolyline &polyline : links)
    {
        const int link_priority = priority(static_cast<InfrastructureEntity>(polyline.entity_type), true);
        if (link_priority > lowest_priority)
            continue;
        const quint64 key = MapBgfx2dScene::entityKey(polyline.entity_type, polyline.render_id);
        if (hidden(key) || labelled_links.contains(key))
            continue;
        // Whole polyline at the copy nearest to the camera (x is unwrapped
        // continuously in the scene), like the GPU network.
        double min_x = polyline.points.first().x;
        double max_x = min_x;
        for (const MapBgfx2d::WorldPoint &point : polyline.points)
        {
            min_x = std::min(min_x, point.x);
            max_x = std::max(max_x, point.x);
        }
        const double shift = std::round(camera.center.x - 0.5 * (min_x + max_x));
        QVector<MapBgfx2d::ScreenPoint> screen_points;
        screen_points.reserve(polyline.points.size());
        double screen_min_x = 0.0;
        double screen_max_x = 0.0;
        double screen_min_y = 0.0;
        double screen_max_y = 0.0;
        for (const MapBgfx2d::WorldPoint &point : polyline.points)
        {
            const MapBgfx2d::ScreenPoint screen{
                width * 0.5 + (point.x + shift - camera.center.x) * camera.world_pixels,
                height * 0.5 + (point.y - camera.center.y) * camera.world_pixels};
            if (screen_points.isEmpty())
            {
                screen_min_x = screen_max_x = screen.x;
                screen_min_y = screen_max_y = screen.y;
            }
            screen_min_x = std::min(screen_min_x, screen.x);
            screen_max_x = std::max(screen_max_x, screen.x);
            screen_min_y = std::min(screen_min_y, screen.y);
            screen_max_y = std::max(screen_max_y, screen.y);
            screen_points.append(screen);
        }
        if (screen_max_x < 0.0 || screen_min_x > width || screen_max_y < 0.0
            || screen_min_y > height)
            continue;
        // The part inside the viewport decides the label position, so a
        // long pipe crossing the screen is labelled where it is visible:
        // each segment is clipped to the viewport (Liang-Barsky), then the
        // label goes to the middle of the visible length.
        struct VisiblePart
        {
            double t0 = 0.0;
            double length = 0.0;
        };
        QVector<VisiblePart> visible_parts;
        visible_parts.reserve(screen_points.size());
        double visible_length = 0.0;
        for (qsizetype index = 1; index < screen_points.size(); ++index)
        {
            const MapBgfx2d::ScreenPoint &a = screen_points.at(index - 1);
            const MapBgfx2d::ScreenPoint &b = screen_points.at(index);
            const double dx = b.x - a.x;
            const double dy = b.y - a.y;
            const double p[4] = {-dx, dx, -dy, dy};
            const double q[4] = {a.x, width - a.x, a.y, height - a.y};
            double t0 = 0.0;
            double t1 = 1.0;
            bool inside = true;
            for (int side = 0; side < 4 && inside; ++side)
            {
                if (p[side] == 0.0)
                {
                    if (q[side] < 0.0)
                        inside = false;
                    continue;
                }
                const double t = q[side] / p[side];
                if (p[side] < 0.0)
                    t0 = std::max(t0, t);
                else
                    t1 = std::min(t1, t);
                if (t0 > t1)
                    inside = false;
            }
            VisiblePart part;
            part.t0 = t0;
            part.length = inside ? (t1 - t0) * std::hypot(dx, dy) : 0.0;
            visible_parts.append(part);
            visible_length += part.length;
        }
        // Short pipes get no label (they would only crowd their nodes);
        // pumps and valves always do, like their icon.
        if (polyline.pipe
                ? visible_length < MinimumLinkLengthPx : !(visible_length > 0.0))
            continue;
        double remaining = 0.5 * visible_length;
        MapBgfx2d::ScreenPoint middle = screen_points.first();
        bool steep = false;
        for (qsizetype index = 1; index < screen_points.size(); ++index)
        {
            const VisiblePart &part = visible_parts.at(index - 1);
            if (part.length <= 0.0)
                continue;
            if (remaining <= part.length)
            {
                const MapBgfx2d::ScreenPoint &a = screen_points.at(index - 1);
                const MapBgfx2d::ScreenPoint &b = screen_points.at(index);
                const double dx = b.x - a.x;
                const double dy = b.y - a.y;
                const double t = part.t0 + remaining / std::hypot(dx, dy);
                middle = {a.x + dx * t, a.y + dy * t};
                steep = std::fabs(dy) > std::fabs(dx);
                break;
            }
            remaining -= part.length;
        }
        const MapBgfxHudRect symbol =
            symbolAt(inputs.link_symbol, static_cast<InfrastructureEntity>(polyline.entity_type), middle.x, middle.y);
        const qsizetype obstacle = insertObstacle(symbol);
        if (!label_links)
            continue;
        Candidate candidate;
        candidate.priority = link_priority;
        candidate.render_id = polyline.render_id;
        candidate.entity_key = key;
        candidate.link = true;
        candidate.symbol = symbol;
        candidate.steep = steep;
        candidate.text = labelText(this->id_by_entity_key.value(key), settings.link_ids,
                                   link_values, inputs.link_values, inputs.link_value_format,
                                   polyline.render_id);
        if (candidate.text.isEmpty())
            continue;
        labelled_links.insert(key);
        candidates.append(candidate);
        candidate_obstacle.append(obstacle);
    }
    this->last_stats.candidates = int(candidates.size());

    // Priority order, stable by entity.
    QVector<qsizetype> order(candidates.size());
    for (qsizetype index = 0; index < order.size(); ++index)
        order[index] = index;
    std::sort(order.begin(), order.end(), [&candidates](qsizetype a, qsizetype b)
    {
        const Candidate &first = candidates.at(a);
        const Candidate &second = candidates.at(b);
        if (first.priority != second.priority)
            return first.priority < second.priority;
        return first.entity_key < second.entity_key;
    });

    const double atlas_limit = AtlasFillLimit * cache->capacity();
    double atlas_used = 0.0;
    for (const qsizetype candidate_index : order)
    {
        if (this->placed_labels.size() >= MaximumLabels)
            break;
        const Candidate &candidate = candidates.at(candidate_index);
        const QSizeF block = blockSize(candidate.text, cache, inputs.font);
        const double footprint = cache->haloTextFootprint(block, HaloPx);
        if (atlas_used + footprint > atlas_limit)
            break;
        const double w = block.width();
        const double h = block.height();
        const MapBgfxHudRect &s = candidate.symbol;
        const double center_x = s.x + 0.5 * s.width;
        const double center_y = s.y + 0.5 * s.height;
        const MapBgfxHudPoint right{s.right() + SymbolGapPx, center_y - 0.5 * h};
        const MapBgfxHudPoint left{s.x - SymbolGapPx - w, center_y - 0.5 * h};
        const MapBgfxHudPoint above{center_x - 0.5 * w, s.y - SymbolGapPx - h};
        const MapBgfxHudPoint below{center_x - 0.5 * w, s.bottom() + SymbolGapPx};
        MapBgfxHudPoint positions[4];
        if (!candidate.link)
        {
            positions[0] = right;
            positions[1] = left;
            positions[2] = above;
            positions[3] = below;
        }
        else if (candidate.steep)
        {
            positions[0] = right;
            positions[1] = left;
            positions[2] = above;
            positions[3] = below;
        }
        else
        {
            positions[0] = above;
            positions[1] = below;
            positions[2] = right;
            positions[3] = left;
        }
        for (const MapBgfxHudPoint &position : positions)
        {
            // Whole logical pixels: crisp text at ratio 1.
            const MapBgfxHudRect box{std::round(position.x), std::round(position.y), w, h};
            if (box.x < ViewportMarginPx || box.y < ViewportMarginPx
                || box.right() > width - ViewportMarginPx
                || box.bottom() > height - ViewportMarginPx)
                continue;
            if (overlaps(expanded(box, LabelGapPx), candidate_obstacle.at(candidate_index)))
                continue;
            insertLabel(box);
            Placed placed;
            placed.box = box;
            placed.text = candidate.text;
            placed.text_argb = candidate.link ? LinkTextColor : NodeTextColor;
            placed.entity_key = candidate.entity_key;
            this->placed_labels.append(placed);
            atlas_used += footprint;
            break;
        }
    }
    this->last_stats.placed = int(this->placed_labels.size());

    // Draw; rasterize at most MaximumRasterizationsPerBuild new runs. If the
    // atlas had to be cleared meanwhile, the sprites drawn before point at
    // reused space: draw everything again once (the frame's labels fit).
    cache->beginFrame();
    for (int pass = 0; pass < 2; ++pass)
    {
        canvas->clear();
        canvas->setWhiteTexel(cache->whiteU(), cache->whiteV());
        int rasterized = 0;
        int drawn = 0;
        int deferred = 0;
        for (const Placed &placed : this->placed_labels)
        {
            if (!cache->containsHaloText(placed.text, inputs.font, placed.text_argb, HaloColor,
                                         HaloPx))
            {
                if (rasterized >= MaximumRasterizationsPerBuild)
                {
                    ++deferred;
                    continue;
                }
                ++rasterized;
            }
            const MapBgfxHudSprite sprite = cache->haloText(placed.text, inputs.font,
                                                            placed.text_argb, HaloColor, HaloPx);
            if (!sprite.valid)
            {
                ++deferred;
                continue;
            }
            canvas->sprite(placed.box.x, placed.box.y, sprite, 0xffffffffu);
            ++drawn;
        }
        this->last_stats.drawn = drawn;
        this->last_stats.deferred = deferred;
        if (!cache->evictedSinceFrameStart())
            break;
        cache->beginFrame();
    }
    return this->last_stats.deferred == 0;
}
