#include "map/render/map_globe_heatmap_scene.h"

#include "geo/geo_web_mercator.h"

#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr double HeatmapMarkerTargetBucketSpan = 4.0;

quint64 heatmapMarkerBucketKey(int bucket_x, int bucket_y)
{
    return (quint64(quint32(bucket_x)) << 32)
        | quint64(quint32(bucket_y));
}

int wrappedHeatmapBucketX(qint64 bucket_x, qint64 bucket_count)
{
    if (bucket_count <= 0)
        return 0;
    qint64 wrapped = bucket_x % bucket_count;
    if (wrapped < 0)
        wrapped += bucket_count;
    return int(wrapped);
}
}

bool MapGlobeHeatmapMarker::operator==(const MapGlobeHeatmapMarker &other) const
{
    return this->render_id == other.render_id
        && this->longitude_deg == other.longitude_deg
        && this->latitude_deg == other.latitude_deg
        && this->active == other.active
        && this->color == other.color;
}

MapGlobeHeatmapScene::MapGlobeHeatmapScene(int maximum_bucket_zoom)
    : maximum_bucket_zoom(qMax(0, maximum_bucket_zoom))
{
}

bool MapGlobeHeatmapScene::setOverlay(
    const QVector<MapGlobeHeatmapMarker> &markers,
    double radius_m,
    double solid_fraction)
{
    const double bounded_radius_m = qMax(0.0, radius_m);
    const double bounded_solid_fraction = qBound(0.0, solid_fraction, 0.9);
    const bool markers_changed = this->markers != markers;
    bool marker_layout_changed = this->markers.size() != markers.size();
    if (!marker_layout_changed)
    {
        for (int marker_index = 0; marker_index < markers.size(); ++marker_index)
        {
            const MapGlobeHeatmapMarker &old_marker = this->markers.at(marker_index);
            const MapGlobeHeatmapMarker &new_marker = markers.at(marker_index);
            if (old_marker.render_id != new_marker.render_id
                || old_marker.longitude_deg != new_marker.longitude_deg
                || old_marker.latitude_deg != new_marker.latitude_deg)
            {
                marker_layout_changed = true;
                break;
            }
        }
    }

    const bool radius_changed = !qFuzzyCompare(
        1.0 + this->radius_m, 1.0 + bounded_radius_m);
    const bool solid_fraction_changed = !qFuzzyCompare(
        1.0 + this->solid_fraction, 1.0 + bounded_solid_fraction);
    if (!markers_changed && !radius_changed && !solid_fraction_changed)
        return false;

    if (markers_changed)
    {
        this->markers = markers;
        this->active_marker_count = int(std::count_if(
            this->markers.cbegin(), this->markers.cend(),
            [](const MapGlobeHeatmapMarker &marker)
        {
            return marker.active;
        }));
    }

    if (marker_layout_changed)
        rebuildMarkerBuckets();
    if (marker_layout_changed || radius_changed)
    {
        ++this->stamp_layout_revision;
        if (this->stamp_layout_revision == 0)
            this->stamp_layout_revision = 1;
    }

    this->radius_m = bounded_radius_m;
    this->solid_fraction = bounded_solid_fraction;
    ++this->heatmap_revision;
    if (this->heatmap_revision == 0)
        this->heatmap_revision = 1;
    return true;
}

void MapGlobeHeatmapScene::rebuildMarkerBuckets()
{
    this->marker_projections.clear();
    this->marker_projections.resize(this->markers.size());
    this->marker_buckets_by_zoom.clear();
    this->marker_buckets_by_zoom.resize(this->maximum_bucket_zoom + 1);
    for (QHash<quint64, QVector<int>> &buckets : this->marker_buckets_by_zoom)
        buckets.reserve(this->markers.size());

    for (int marker_index = 0; marker_index < this->markers.size(); ++marker_index)
    {
        const MapGlobeHeatmapMarker &marker = this->markers.at(marker_index);
        if (!std::isfinite(marker.longitude_deg)
            || !std::isfinite(marker.latitude_deg))
        {
            continue;
        }

        const double marker_tile_x_zoom0 = GeoWebMercator::lonToTileX(
            GeoWebMercator::normalizeLongitude(marker.longitude_deg), 0);
        const double marker_tile_y_zoom0 = GeoWebMercator::latToTileY(
            marker.latitude_deg, 0);
        if (!std::isfinite(marker_tile_x_zoom0)
            || !std::isfinite(marker_tile_y_zoom0))
        {
            continue;
        }

        MarkerProjection &projection = this->marker_projections[marker_index];
        projection.tile_x_zoom0 = marker_tile_x_zoom0;
        projection.tile_y_zoom0 = marker_tile_y_zoom0;
        projection.valid = true;

        for (int bucket_zoom = 0;
             bucket_zoom <= this->maximum_bucket_zoom;
             ++bucket_zoom)
        {
            const qint64 bucket_count = qint64(1) << bucket_zoom;
            const double bucket_scale = double(bucket_count);
            const int bucket_x = wrappedHeatmapBucketX(
                qint64(std::floor(marker_tile_x_zoom0 * bucket_scale)),
                bucket_count);
            const int bucket_y = int(qBound(
                qint64(0),
                qint64(std::floor(marker_tile_y_zoom0 * bucket_scale)),
                bucket_count - 1));
            this->marker_buckets_by_zoom[bucket_zoom]
                [heatmapMarkerBucketKey(bucket_x, bucket_y)]
                    .append(marker_index);
        }
    }
}

QVector<int> MapGlobeHeatmapScene::markerCandidates(
    const MapGlobeHeatmapTile &tile,
    double radius_tile_fraction,
    int *visited_bucket_cells) const
{
    if (visited_bucket_cells != nullptr)
        *visited_bucket_cells = 0;

    QVector<int> result;
    if (this->marker_projections.isEmpty()
        || this->marker_buckets_by_zoom.isEmpty()
        || !std::isfinite(radius_tile_fraction)
        || radius_tile_fraction < 0.0)
    {
        return result;
    }

    int bucket_zoom = qBound(0, tile.zoom, this->maximum_bucket_zoom);
    double bucket_scale = std::ldexp(1.0, bucket_zoom - tile.zoom);
    double horizontal_span =
        (1.0 + 2.0 * radius_tile_fraction) * bucket_scale;
    if (!std::isfinite(bucket_scale) || bucket_scale <= 0.0
        || !std::isfinite(horizontal_span))
    {
        return allValidMarkerIndices();
    }

    while (bucket_zoom > 0
           && horizontal_span > HeatmapMarkerTargetBucketSpan)
    {
        --bucket_zoom;
        bucket_scale *= 0.5;
        horizontal_span *= 0.5;
    }

    const qint64 bucket_count = qint64(1) << bucket_zoom;
    if (horizontal_span >= double(bucket_count))
        return allValidMarkerIndices();
    const QHash<quint64, QVector<int>> &buckets =
        this->marker_buckets_by_zoom.at(bucket_zoom);
    if (buckets.isEmpty())
        return result;

    const double minimum_bucket_x_value =
        (double(tile.virtual_x) - radius_tile_fraction) * bucket_scale;
    const double maximum_bucket_x_value =
        (double(tile.virtual_x) + 1.0 + radius_tile_fraction) * bucket_scale;
    const double minimum_bucket_y_value =
        (double(tile.tile_y) - radius_tile_fraction) * bucket_scale;
    const double maximum_bucket_y_value =
        (double(tile.tile_y) + 1.0 + radius_tile_fraction) * bucket_scale;
    if (!std::isfinite(minimum_bucket_x_value)
        || !std::isfinite(maximum_bucket_x_value)
        || !std::isfinite(minimum_bucket_y_value)
        || !std::isfinite(maximum_bucket_y_value))
    {
        return allValidMarkerIndices();
    }

    const qint64 minimum_bucket_x = qint64(std::floor(minimum_bucket_x_value));
    const qint64 maximum_bucket_x = qint64(std::floor(maximum_bucket_x_value));
    const qint64 unclamped_minimum_bucket_y =
        qint64(std::floor(minimum_bucket_y_value));
    const qint64 unclamped_maximum_bucket_y =
        qint64(std::floor(maximum_bucket_y_value));
    if (unclamped_maximum_bucket_y < 0
        || unclamped_minimum_bucket_y >= bucket_count)
    {
        return result;
    }

    const qint64 minimum_bucket_y = qMax(qint64(0), unclamped_minimum_bucket_y);
    const qint64 maximum_bucket_y = qMin(
        bucket_count - 1, unclamped_maximum_bucket_y);
    const qint64 horizontal_bucket_count =
        maximum_bucket_x - minimum_bucket_x + 1;
    const qint64 vertical_bucket_count =
        maximum_bucket_y - minimum_bucket_y + 1;
    if (horizontal_bucket_count <= 0 || vertical_bucket_count <= 0)
        return result;
    if (horizontal_bucket_count >= bucket_count)
        return allValidMarkerIndices();

    for (qint64 bucket_y = minimum_bucket_y;
         bucket_y <= maximum_bucket_y;
         ++bucket_y)
    {
        for (qint64 bucket_x = minimum_bucket_x;
             bucket_x <= maximum_bucket_x;
             ++bucket_x)
        {
            if (visited_bucket_cells != nullptr)
                ++*visited_bucket_cells;
            const quint64 key = heatmapMarkerBucketKey(
                wrappedHeatmapBucketX(bucket_x, bucket_count),
                int(bucket_y));
            const QHash<quint64, QVector<int>>::const_iterator iterator =
                buckets.constFind(key);
            if (iterator == buckets.cend())
                continue;
            result.append(iterator.value());
        }
    }
    return result;
}

QVector<int> MapGlobeHeatmapScene::allValidMarkerIndices() const
{
    QVector<int> indices;
    indices.reserve(this->markers.size());
    for (int marker_index = 0;
         marker_index < this->marker_projections.size();
         ++marker_index)
    {
        if (this->marker_projections.at(marker_index).valid)
            indices.append(marker_index);
    }
    return indices;
}

QVector<MapGlobeHeatmapStamp> MapGlobeHeatmapScene::activeStampsFromLayout(
    const QVector<MapGlobeHeatmapStamp> &layout,
    bool *valid_indices,
    MapGlobeHeatmapRasterStats *stats) const
{
    QVector<MapGlobeHeatmapStamp> active_stamps;
    active_stamps.reserve(layout.size());
    *valid_indices = true;
    for (const MapGlobeHeatmapStamp &layout_stamp : layout)
    {
        if (layout_stamp.marker_index < 0
            || layout_stamp.marker_index >= this->markers.size())
        {
            *valid_indices = false;
            active_stamps.clear();
            break;
        }

        const MapGlobeHeatmapMarker &marker =
            this->markers.at(layout_stamp.marker_index);
        if (marker.render_id != layout_stamp.marker_render_id)
        {
            *valid_indices = false;
            active_stamps.clear();
            break;
        }
        if (!marker.active)
            continue;

        MapGlobeHeatmapStamp active_stamp = layout_stamp;
        active_stamp.color = marker.color;
        active_stamps.append(std::move(active_stamp));
    }
    if (*valid_indices && stats != nullptr)
        stats->marker_tile_pairs = active_stamps.size();
    return active_stamps;
}

QVector<MapGlobeHeatmapStamp> MapGlobeHeatmapScene::stampsForTile(
    const MapGlobeHeatmapTile &tile,
    MapGlobeHeatmapTileLayoutCache *layout_cache,
    MapGlobeHeatmapRasterStats *stats) const
{
    if (stats != nullptr)
        *stats = MapGlobeHeatmapRasterStats();

    const bool layout_matches = layout_cache != nullptr
        && layout_cache->revision == this->stamp_layout_revision
        && layout_cache->zoom == tile.zoom
        && layout_cache->virtual_x == tile.virtual_x
        && layout_cache->tile_y == tile.tile_y;
    if (layout_matches)
    {
        bool valid_indices = false;
        QVector<MapGlobeHeatmapStamp> active_stamps = activeStampsFromLayout(
            layout_cache->stamps, &valid_indices, stats);
        if (valid_indices)
        {
            if (stats != nullptr)
                stats->stamp_layout_cache_hit = true;
            return active_stamps;
        }
    }

    if (this->markers.isEmpty() || !(this->radius_m > 0.0) || tile.is_cap)
    {
        QVector<MapGlobeHeatmapStamp> empty_layout;
        if (layout_cache != nullptr)
        {
            layout_cache->stamps = empty_layout;
            layout_cache->revision = this->stamp_layout_revision;
            layout_cache->zoom = tile.zoom;
            layout_cache->virtual_x = tile.virtual_x;
            layout_cache->tile_y = tile.tile_y;
        }
        return empty_layout;
    }

    const double tile_lat_top_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y), tile.zoom);
    const double tile_lat_bottom_deg = GeoWebMercator::tileYToLat(
        double(tile.tile_y) + 1.0, tile.zoom);
    const double tile_center_lat_deg =
        (tile_lat_top_deg + tile_lat_bottom_deg) * 0.5;
    const double meters_per_pixel = GeoWebMercator::metersPerPixel(
        tile_center_lat_deg, tile.zoom);
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
    {
        if (layout_cache != nullptr)
        {
            layout_cache->stamps.clear();
            layout_cache->revision = this->stamp_layout_revision;
            layout_cache->zoom = tile.zoom;
            layout_cache->virtual_x = tile.virtual_x;
            layout_cache->tile_y = tile.tile_y;
        }
        return {};
    }

    const double radius_pixels = this->radius_m / meters_per_pixel;
    if (!std::isfinite(radius_pixels) || radius_pixels <= 0.0)
    {
        if (layout_cache != nullptr)
        {
            layout_cache->stamps.clear();
            layout_cache->revision = this->stamp_layout_revision;
            layout_cache->zoom = tile.zoom;
            layout_cache->virtual_x = tile.virtual_x;
            layout_cache->tile_y = tile.tile_y;
        }
        return {};
    }
    const double radius_tile_fraction =
        radius_pixels / double(GeoWebMercator::TileSize);

    int visited_bucket_cells = 0;
    const QVector<int> candidate_indices = markerCandidates(
        tile, radius_tile_fraction, &visited_bucket_cells);
    if (stats != nullptr)
    {
        stats->candidate_markers = candidate_indices.size();
        stats->candidate_bucket_cells = visited_bucket_cells;
    }

    QVector<MapGlobeHeatmapStamp> layout;
    layout.reserve(candidate_indices.size());
    const double pixels_per_fraction = double(TextureSize);
    const double tile_scale = std::ldexp(1.0, tile.zoom);
    if (std::isfinite(tile_scale))
    {
        for (int marker_index : candidate_indices)
        {
            if (marker_index < 0
                || marker_index >= this->markers.size()
                || marker_index >= this->marker_projections.size())
            {
                continue;
            }

            const MapGlobeHeatmapMarker &marker = this->markers.at(marker_index);
            const MarkerProjection &projection =
                this->marker_projections.at(marker_index);
            if (!projection.valid)
                continue;

            const double marker_tile_x = GeoWebMercator::nearestWrappedTileX(
                projection.tile_x_zoom0 * tile_scale,
                double(tile.virtual_x), tile.zoom);
            const double marker_tile_y = projection.tile_y_zoom0 * tile_scale;
            if (!std::isfinite(marker_tile_x)
                || !std::isfinite(marker_tile_y))
            {
                continue;
            }

            const double fraction_x = marker_tile_x - double(tile.virtual_x);
            const double fraction_y = marker_tile_y - double(tile.tile_y);
            if (fraction_x + radius_tile_fraction < 0.0
                || fraction_x - radius_tile_fraction > 1.0
                || fraction_y + radius_tile_fraction < 0.0
                || fraction_y - radius_tile_fraction > 1.0)
            {
                continue;
            }

            MapGlobeHeatmapStamp stamp;
            stamp.center_x_pixels = fraction_x * pixels_per_fraction;
            stamp.center_y_pixels = fraction_y * pixels_per_fraction;
            stamp.radius_pixels = radius_pixels;
            stamp.marker_index = marker_index;
            stamp.marker_render_id = marker.render_id;
            stamp.color = marker.color;
            layout.append(stamp);
        }
    }

    if (layout_cache != nullptr)
    {
        layout_cache->stamps = layout;
        layout_cache->revision = this->stamp_layout_revision;
        layout_cache->zoom = tile.zoom;
        layout_cache->virtual_x = tile.virtual_x;
        layout_cache->tile_y = tile.tile_y;
        bool valid_indices = false;
        return activeStampsFromLayout(
            layout_cache->stamps, &valid_indices, stats);
    }

    bool valid_indices = false;
    return activeStampsFromLayout(layout, &valid_indices, stats);
}

QImage MapGlobeHeatmapScene::renderStamps(
    const QVector<MapGlobeHeatmapStamp> &stamps) const
{
    if (stamps.isEmpty())
        return QImage();

    QImage image(TextureSize, TextureSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(Qt::NoPen);

    const double half_fraction = this->solid_fraction
        + (1.0 - this->solid_fraction) * 0.4375;
    for (const MapGlobeHeatmapStamp &stamp : stamps)
    {
        const QPointF center_pixels(stamp.center_x_pixels, stamp.center_y_pixels);
        QColor full_color = stamp.color;
        full_color.setAlpha(255);
        QColor half_color = stamp.color;
        half_color.setAlpha(128);
        QColor edge_color = stamp.color;
        edge_color.setAlpha(0);

        QRadialGradient gradient(center_pixels, stamp.radius_pixels);
        gradient.setColorAt(0.0, full_color);
        if (this->solid_fraction > 0.0)
            gradient.setColorAt(this->solid_fraction, full_color);
        gradient.setColorAt(half_fraction, half_color);
        gradient.setColorAt(1.0, edge_color);
        painter.setBrush(gradient);
        painter.drawEllipse(center_pixels, stamp.radius_pixels, stamp.radius_pixels);
    }
    painter.end();
    return image;
}

bool MapGlobeHeatmapScene::isEmpty() const
{
    return this->markers.isEmpty();
}

int MapGlobeHeatmapScene::markerCount() const
{
    return this->markers.size();
}

int MapGlobeHeatmapScene::activeMarkerCount() const
{
    return this->active_marker_count;
}

double MapGlobeHeatmapScene::radiusM() const
{
    return this->radius_m;
}

double MapGlobeHeatmapScene::solidFraction() const
{
    return this->solid_fraction;
}

quint64 MapGlobeHeatmapScene::revision() const
{
    return this->heatmap_revision;
}

quint64 MapGlobeHeatmapScene::layoutRevision() const
{
    return this->stamp_layout_revision;
}

int MapGlobeHeatmapScene::bucketLevelCount() const
{
    return this->marker_buckets_by_zoom.size();
}
