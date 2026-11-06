#ifndef MAP_GLOBE_HEATMAP_SCENE_H
#define MAP_GLOBE_HEATMAP_SCENE_H

#include <QColor>
#include <QHash>
#include <QImage>
#include <QVector>
#include <QtGlobal>

struct MapGlobeHeatmapMarker
{
    quint32 render_id = 0;
    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
    bool active = false;
    QColor color;

    bool operator==(const MapGlobeHeatmapMarker &other) const;
};

struct MapGlobeHeatmapStamp
{
    double center_x_pixels = 0.0;
    double center_y_pixels = 0.0;
    double radius_pixels = 0.0;
    int marker_index = -1;
    quint32 marker_render_id = 0;
    QColor color;
};

struct MapGlobeHeatmapTile
{
    int zoom = 0;
    int virtual_x = 0;
    int tile_y = 0;
    bool is_cap = false;
};

struct MapGlobeHeatmapRasterStats
{
    int candidate_markers = 0;
    int candidate_bucket_cells = 0;
    int marker_tile_pairs = 0;
    bool stamp_layout_cache_hit = false;
};

struct MapGlobeHeatmapTileLayoutCache
{
    QVector<MapGlobeHeatmapStamp> stamps;
    quint64 revision = 0;
    int zoom = -1;
    int virtual_x = 0;
    int tile_y = 0;
};

class MapGlobeHeatmapScene
{
public:
    static constexpr int TextureSize = 256;

    explicit MapGlobeHeatmapScene(int maximum_bucket_zoom);

    bool setOverlay(
        const QVector<MapGlobeHeatmapMarker> &markers,
        double radius_m,
        double solid_fraction);

    QVector<MapGlobeHeatmapStamp> stampsForTile(
        const MapGlobeHeatmapTile &tile,
        MapGlobeHeatmapTileLayoutCache *layout_cache,
        MapGlobeHeatmapRasterStats *stats = nullptr) const;
    QImage renderStamps(const QVector<MapGlobeHeatmapStamp> &stamps) const;

    bool isEmpty() const;
    int markerCount() const;
    int activeMarkerCount() const;
    double radiusM() const;
    double solidFraction() const;
    quint64 revision() const;
    quint64 layoutRevision() const;
    int bucketLevelCount() const;

private:
    struct MarkerProjection
    {
        double tile_x_zoom0 = 0.0;
        double tile_y_zoom0 = 0.0;
        bool valid = false;
    };

    void rebuildMarkerBuckets();
    QVector<int> markerCandidates(
        const MapGlobeHeatmapTile &tile,
        double radius_tile_fraction,
        int *visited_bucket_cells) const;
    QVector<int> allValidMarkerIndices() const;
    QVector<MapGlobeHeatmapStamp> activeStampsFromLayout(
        const QVector<MapGlobeHeatmapStamp> &layout,
        bool *valid_indices,
        MapGlobeHeatmapRasterStats *stats) const;

    int maximum_bucket_zoom = 0;
    QVector<MapGlobeHeatmapMarker> markers;
    QVector<MarkerProjection> marker_projections;
    QVector<QHash<quint64, QVector<int>>> marker_buckets_by_zoom;
    double radius_m = 0.0;
    double solid_fraction = 0.0;
    quint64 heatmap_revision = 1;
    quint64 stamp_layout_revision = 1;
    int active_marker_count = 0;
};

#endif // MAP_GLOBE_HEATMAP_SCENE_H
