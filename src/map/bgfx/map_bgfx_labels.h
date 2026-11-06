#ifndef MAP_BGFX_LABELS_H
#define MAP_BGFX_LABELS_H

#include "map/bgfx/map_bgfx_2d_projection.h"
#include "map/bgfx/map_bgfx_2d_scene.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"
#include "map/render/map_label_settings.h"
#include "network/network_render_snapshot.h"

#include <QFont>
#include <QHash>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <functional>

// Canonical metric value -> displayed value (unit of the legend): affine,
// because every AOWIS unit conversion is (temperatures included).
struct MapBgfxLabelValueFormat
{
    // false: no metric chosen / nothing to show; value labels stay off.
    bool valid = false;
    double scale = 1.0;
    double offset = 0.0;

    double displayed(double canonical) const { return canonical * this->scale + this->offset; }
    bool operator==(const MapBgfxLabelValueFormat &other) const
    {
        return this->valid == other.valid && this->scale == other.scale
            && this->offset == other.offset;
    }
    bool operator!=(const MapBgfxLabelValueFormat &other) const { return !(*this == other); }
};

// Map labels on the bgfx 2D map (map_label_settings.h): IDs and/or values
// next to nodes and links, rebuilt into a MapBgfxHudCanvas whenever the
// camera or the content changes and drawn with the HUD program between the
// network (and the editor overlay) and the HUD.
//
// Placement is greedy and decluttered, in screen space:
//
// 1. Candidates are the entities on screen, at their drawn positions (the
//    prepared 2D scene, so decluttered monitor nodes are labelled where
//    their dot is). Links are labelled at the middle of their on-screen
//    length; pipes shorter than MinimumLinkLengthPx on screen get none
//    (pumps and valves always get one, like their icon).
// 2. Every on-screen node symbol (dot / icon) and device-link icon is an
//    obstacle: labels never cover the network's symbols.
// 3. Candidates are tried in priority order - tanks and reservoirs, pumps and
//    valves, junctions, pipes, demand points; ties by render id, so the
//    result is stable while panning - each at up to four positions around
//    its symbol (right, left, above, below; links: above, below, right,
//    left, or right/left first along steep links). The first position that
//    lies inside the viewport and overlaps no placed label and no foreign
//    obstacle wins; otherwise the label is dropped. A uniform grid of
//    GridCellPx cells keeps each test local.
// 4. Placement stops at MaximumLabels or when the labels' atlas footprint
//    would exceed AtlasFillLimit of the label atlas, so one frame's labels
//    always fit into it.
//
// Text runs are rasterized once (MapBgfxHudTextCache::haloText(): dark text
// with a white outline, readable over imagery and over lines). At most
// MaximumRasterizationsPerBuild new runs are rasterized per build; the rest
// keep their place and appear in the next frames (build() reports it), so a
// jump to a dense area never stalls one frame for long.
//
// Pure Qt Gui + math (no bgfx, no widgets): unit-tested without a GPU.
class MapBgfxLabels
{
public:
    // Symbol box of an entity relative to its anchor (logical px): the
    // label is placed around it and every on-screen one is an obstacle.
    using SymbolRect = std::function<MapBgfxHudRect(InfrastructureEntity entity_type)>;

    struct Inputs
    {
        QSizeF viewport;
        // The network's camera (the editor's full-network move included),
        // so labels stay exactly at the drawn network.
        MapBgfx2d::Camera camera;
        const MapBgfx2dScene *scene = nullptr;
        // IDs by render id (NetworkRenderNode::id / NetworkRenderLink::id).
        const NetworkRenderSnapshot *snapshot = nullptr;
        MapLabelSettings settings;
        // Canonical metric values by render id, NaN: none
        // (MapNetworkRenderSymbology).
        const QVector<double> *node_values = nullptr;
        const QVector<double> *link_values = nullptr;
        MapBgfxLabelValueFormat node_value_format;
        MapBgfxLabelValueFormat link_value_format;
        // Entity keys (MapBgfx2dScene::entityKey()) that are not drawn (the
        // editor's move preview): no label, no obstacle.
        const QSet<quint64> *hidden_keys = nullptr;
        // Node symbols around the node position; device-link icons around
        // the link's middle (pipes: return an empty rect). Empty functions:
        // points.
        SymbolRect node_symbol;
        SymbolRect link_symbol;
        // Pixel-sized label font.
        QFont font;
    };

    struct Stats
    {
        int candidates = 0;
        int placed = 0;
        int drawn = 0;
        // Placed but not rasterized yet (shown in a later build).
        int deferred = 0;
        // More on-screen nodes than DenseNodeCount: junction and demand
        // point labels and obstacles are skipped.
        bool dense = false;
    };

    // One placed label (logical px of the label block, without the halo).
    struct Placed
    {
        MapBgfxHudRect box;
        QString text;
        quint32 text_argb = 0;
        quint64 entity_key = 0;
    };

    // Builds the labels into canvas (cleared first). Returns false when
    // labels were deferred (call again next frame even if nothing changed).
    bool build(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, const Inputs &inputs);
    const Stats &stats() const { return this->last_stats; }
    const QVector<Placed> &placed() const { return this->placed_labels; }

    // Text and outline colours (0xAARRGGBB): nodes near black, links dark
    // blue, so node and link labels are told apart at a glance.
    static constexpr quint32 NodeTextColor = 0xff1e1e1eu;
    static constexpr quint32 LinkTextColor = 0xff0b3a78u;
    static constexpr quint32 HaloColor = 0xf2ffffffu;
    static constexpr double HaloPx = 1.5;
    // Room between a symbol and its label, and between two labels.
    static constexpr double SymbolGapPx = 3.0;
    static constexpr double LabelGapPx = 2.0;
    // Labels keep this distance to the viewport edge.
    static constexpr double ViewportMarginPx = 2.0;
    static constexpr double MinimumLinkLengthPx = 24.0;
    static constexpr double GridCellPx = 48.0;
    static constexpr int MaximumLabels = 2500;
    static constexpr double AtlasFillLimit = 0.7;
    static constexpr int MaximumRasterizationsPerBuild = 160;
    static constexpr int DenseNodeCount = 30000;

    // Priority classes (lower first).
    static int priority(InfrastructureEntity entity_type, bool link);

private:
    struct Candidate
    {
        int priority = 0;
        quint32 render_id = 0;
        quint64 entity_key = 0;
        bool link = false;
        // Symbol box on screen (zero size: a point).
        MapBgfxHudRect symbol;
        // Steep link at its middle: right/left before above/below.
        bool steep = false;
        QString text;
    };

    // Per prepared scene and snapshot revision: IDs by entity key.
    void synchronizeIdentity(const MapBgfx2dScene &scene, const NetworkRenderSnapshot &snapshot);
    QString labelText(const QString &id, bool show_id, bool show_value,
                      const QVector<double> *values,
                      const MapBgfxLabelValueFormat &format, quint32 render_id) const;
    QSizeF blockSize(const QString &text, MapBgfxHudTextCache *cache, const QFont &font);

    // Occupancy grid over the viewport.
    void resetGrid(const QSizeF &viewport);
    bool overlaps(const MapBgfxHudRect &rect, qsizetype ignore_obstacle) const;
    void insertLabel(const MapBgfxHudRect &rect);
    qsizetype insertObstacle(const MapBgfxHudRect &rect);
    void cellRange(const MapBgfxHudRect &rect, int *x0, int *y0, int *x1, int *y1) const;

    quint64 identity_preparation_id = 0;
    const NetworkRenderSnapshot *identity_snapshot = nullptr;
    quint64 identity_geometry_revision = 0;
    quint64 identity_visual_revision = 0;
    QHash<quint64, QString> id_by_entity_key;

    // Block sizes by text for the current font (cleared on a font change
    // and when it grows too large).
    QString size_cache_font_key;
    QHash<QString, QSizeF> block_size_by_text;

    int grid_columns = 0;
    int grid_rows = 0;
    // Per cell: indices into label_rects (>= 0) or obstacle_rects (~index).
    QVector<QVector<qsizetype>> grid_cells;
    QVector<MapBgfxHudRect> label_rects;
    QVector<MapBgfxHudRect> obstacle_rects;

    QVector<Placed> placed_labels;
    Stats last_stats;
};

#endif // MAP_BGFX_LABELS_H
