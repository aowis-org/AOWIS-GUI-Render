#include "map/data/map_terrain_repository.h"

#include <QDateTime>
#include <QDebug>
#include <QStringList>
#include <QTimer>
#ifndef __EMSCRIPTEN__
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#endif

namespace
{
constexpr int TerrainCacheMaximumCostKiB = 128 * 1024;
constexpr qint64 TerrainRetryInitialDelayMs = 1000;
constexpr qint64 TerrainRetryMaximumDelayMs = 30000;
#ifndef __EMSCRIPTEN__
constexpr int TerrainDecodeThreadCountMaximum = 4;
#endif

qint64 positiveModulo(qint64 value, qint64 divisor)
{
    const qint64 remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

// The map-tile cache's column test (MapTileRepository): the range wraps
// around the antimeridian, a range at least as wide as the zoom level covers
// every column.
bool terrainTileXInsideRange(qint64 tile_x, qint64 tile_count, qint64 tile_x_min, qint64 tile_x_max)
{
    const qint64 range_width = tile_x_max - tile_x_min + 1;
    if (range_width >= tile_count)
        return true;

    const qint64 first_wrapped_x = positiveModulo(tile_x_min, tile_count);
    const qint64 offset = positiveModulo(tile_x - first_wrapped_x, tile_count);
    return offset < range_width;
}

// Whether a mapTerrainTileKey() ("terrain/v<version>/<dataset>/<z>/<x>/<y>")
// names a tile of dataset inside range.
bool terrainKeyInsideRange(const QString &key, const QString &dataset,
                           const MapTerrainCacheDeletionRange &range)
{
    const QStringList parts = key.split(QLatin1Char('/'));
    if (parts.size() != 6 || parts.at(0) != QStringLiteral("terrain") || parts.at(2) != dataset)
        return false;

    bool zoom_valid = false;
    bool x_valid = false;
    bool y_valid = false;
    const int zoom = parts.at(3).toInt(&zoom_valid);
    const qint64 tile_x = parts.at(4).toLongLong(&x_valid);
    const qint64 tile_y = parts.at(5).toLongLong(&y_valid);
    if (!zoom_valid || !x_valid || !y_valid || zoom != range.zoom
        || zoom < 0 || zoom > MapTerrainTileMaximumZoom)
    {
        return false;
    }

    const qint64 tile_count = qint64(1) << zoom;
    return tile_y >= range.tile_y_min && tile_y <= range.tile_y_max
        && terrainTileXInsideRange(tile_x, tile_count, range.tile_x_min, range.tile_x_max);
}
}

MapTerrainRepository::MapTerrainRepository(const MapServerTransport &transport, QObject *parent)
    : QObject(parent),
      transport(transport)
{
    this->cache.setMaxCost(TerrainCacheMaximumCostKiB);
#ifndef __EMSCRIPTEN__
    const int decode_threads = qMax(1, qMin(TerrainDecodeThreadCountMaximum,
        qMax(1, QThread::idealThreadCount() / 2)));
    this->terrain_decode_pool.setMaxThreadCount(decode_threads);
    this->terrain_decode_pool.setExpiryTimeout(30000);
#endif

    initServerMapInterface();
}

MapTerrainRepository::~MapTerrainRepository()
{
#ifndef __EMSCRIPTEN__
    this->terrain_decode_pool.clear();
    this->terrain_decode_pool.waitForDone();
#endif
}

const MapTerrainTile *MapTerrainRepository::tile(const QString &key) const
{
    return this->cache.object(key);
}

const MapTerrainTile *MapTerrainRepository::tile(const QString &dataset, int zoom,
                                                  quint32 x, quint32 y) const
{
    MapTerrainTileAddress address;
    address.zoom = zoom;
    address.x = x;
    address.y = y;
    return tile(mapTerrainTileKey(dataset, address));
}

void MapTerrainRepository::requestTile(const QString &dataset, int zoom, quint32 x, quint32 y)
{
    MapTerrainTileAddress address;
    address.zoom = zoom;
    address.x = x;
    address.y = y;

    const QString key = mapTerrainTileKey(dataset, address);
    const QString endpoint = mapTerrainTileEndpoint(dataset, address);
    if (key.isEmpty() || endpoint.isEmpty())
    {
        emit signalTerrainTileFailed(key,
                                     QStringLiteral("Invalid terrain dataset or XYZ address"));
        return;
    }

    if (this->cache.contains(key) || this->terrain_pending.contains(key) ||
        this->interface_map == nullptr)
    {
        return;
    }

    const QHash<QString, TerrainFailure>::const_iterator failure =
        this->terrain_failures.constFind(key);
    if (failure != this->terrain_failures.constEnd() &&
        QDateTime::currentMSecsSinceEpoch() < failure->retry_after_msecs)
    {
        return;
    }

    PendingTerrainRequest request;
    request.dataset = dataset;
    request.address = address;
    request.generation = this->terrain_generation;

    this->terrain_pending.insert(key);
    this->terrain_requests_pending.insert(key, request);
    this->interface_map->requestTerrainTile(endpoint, key);
}

const MapTerrainDatasetInfo *MapTerrainRepository::activeDataset() const
{
    return this->active_dataset.has_value() ? &this->active_dataset.value() : nullptr;
}

void MapTerrainRepository::requestDatasetCatalog()
{
    if (this->interface_map != nullptr)
        this->interface_map->requestTerrainDatasets();
}

void MapTerrainRepository::datasetCatalogReceived(const QByteArray &data)
{
    QString error;
    const std::optional<MapTerrainDatasetCatalog> catalog =
        decodeMapTerrainDatasetCatalog(data, &error);
    if (!catalog.has_value())
    {
        datasetCatalogFailed(error);
        return;
    }
    if (!error.isEmpty())
        qWarning().noquote() << error;

    this->dataset_catalog_failure_count = 0;
    const MapTerrainDatasetInfo *info = catalog->find(catalog->default_dataset);
    if (info == nullptr)
    {
        // A configured default without a provider (e.g. only cached tiles of
        // a dataset): its zoom range is unknown, so no relief.
        qWarning().noquote()
            << QStringLiteral("Terrain service describes no dataset %1 (its default); "
                              "drawing the globe without relief")
                   .arg(catalog->default_dataset);
        if (this->active_dataset.has_value())
        {
            this->active_dataset.reset();
            emit signalTerrainDatasetChanged();
        }
        return;
    }

    qInfo().noquote()
        << QStringLiteral("Terrain dataset %1: %2 m nominal resolution, zoom %3 to %4")
               .arg(info->dataset)
               .arg(info->nominal_resolution_m)
               .arg(info->minimum_zoom)
               .arg(info->maximum_zoom);
    this->active_dataset = *info;
    emit signalTerrainDatasetChanged();
}

void MapTerrainRepository::datasetCatalogFailed(const QString &error)
{
    // Retried like a tile, with the same backoff: the map server may still be
    // starting, or terrain may be disabled there for now.
    this->dataset_catalog_failure_count = qMin(this->dataset_catalog_failure_count + 1, 6);
    const qint64 retry_delay = qMin(
        TerrainRetryInitialDelayMs * (qint64(1) << (this->dataset_catalog_failure_count - 1)),
        TerrainRetryMaximumDelayMs);
    qWarning() << "Terrain dataset catalog request failed:" << error;

    const quint64 generation = this->terrain_generation;
    QTimer::singleShot(int(retry_delay), this, [this, generation]
    {
        if (generation == this->terrain_generation && !this->active_dataset.has_value())
            requestDatasetCatalog();
    });
}

void MapTerrainRepository::requestUpstreamActivity()
{
    if (this->interface_map != nullptr)
        this->interface_map->requestTerrainUpstreamActivity();
}

void MapTerrainRepository::cancelUpstreamDownloads()
{
    if (this->interface_map != nullptr)
        this->interface_map->cancelTerrainUpstreamDownloads();
}

void MapTerrainRepository::deleteCache(
    const QString &dataset, const QVector<MapTerrainCacheDeletionRange> &tile_ranges,
    const std::optional<MapTerrainCacheDeletionRange> &source_range)
{
    if (this->interface_map == nullptr || !isValidMapTerrainDatasetId(dataset))
    {
        emit signalTerrainCacheDeletionFinished(
            0, 0, QStringLiteral("No terrain service is available"));
        return;
    }

    const quint64 batch_id = this->next_terrain_deletion_batch_id++;
    TerrainDeletionBatch batch;
    batch.dataset = dataset;
    batch.source_requested = source_range.has_value();

    QVector<TerrainDeletionRequest> requests;
    for (const MapTerrainCacheDeletionRange &range : tile_ranges)
    {
        if (range.zoom < 0 || range.zoom > MapTerrainTileMaximumZoom
            || range.tile_x_min > range.tile_x_max || range.tile_y_min > range.tile_y_max)
        {
            continue;
        }
        TerrainDeletionRequest request;
        request.batch_id = batch_id;
        request.scope = MapTerrainCacheDeletionScope::NormalizedTiles;
        request.range = range;
        requests.append(request);
    }
    if (source_range.has_value())
    {
        TerrainDeletionRequest request;
        request.batch_id = batch_id;
        request.scope = MapTerrainCacheDeletionScope::SourceData;
        request.range = source_range.value();
        requests.append(request);
    }
    if (requests.isEmpty())
    {
        emit signalTerrainCacheDeletionFinished(0, 0, QString());
        return;
    }

    batch.outstanding = int(requests.size());
    // The source data goes first: a tile requested after its deletion but
    // before the source's would be regenerated from the old source data.
    QVector<TerrainDeletionRequest> send_now;
    for (const TerrainDeletionRequest &request : requests)
    {
        if (batch.source_requested && request.scope == MapTerrainCacheDeletionScope::NormalizedTiles)
            batch.deferred_tile_requests.append(request);
        else
            send_now.append(request);
    }
    this->terrain_deletion_batches.insert(batch_id, batch);

    // The interface may answer synchronously (no service in this mode).
    for (const TerrainDeletionRequest &request : send_now)
        sendTerrainDeletionRequest(dataset, request);
}

void MapTerrainRepository::sendTerrainDeletionRequest(const QString &dataset,
                                                      const TerrainDeletionRequest &request)
{
    if (this->interface_map == nullptr)
        return;

    // Tiles: drop what this GUI has of them at once. The renderers keep what
    // they drew and request the tiles again; a request answered from before
    // the service's deletion is dropped again when the deletion answers
    // (finishTerrainDeletionRequest()).
    if (request.scope == MapTerrainCacheDeletionScope::NormalizedTiles
        && invalidateTiles(dataset, request.range))
    {
        emit signalTerrainTilesDeleted();
    }

    const quint64 request_id = this->next_terrain_deletion_request_id++;
    this->terrain_deletion_requests.insert(request_id, request);
    this->interface_map->deleteTerrainCache(
        request_id, request.scope, dataset, request.range.zoom,
        request.range.tile_x_min, request.range.tile_x_max,
        request.range.tile_y_min, request.range.tile_y_max);
}

bool MapTerrainRepository::invalidateTiles(const QString &dataset,
                                           const MapTerrainCacheDeletionRange &range)
{
    bool dropped_cached = false;
    const QList<QString> cached_keys = this->cache.keys();
    for (const QString &key : cached_keys)
    {
        if (!terrainKeyInsideRange(key, dataset, range))
            continue;
        this->cache.remove(key);
        dropped_cached = true;
    }

    const QList<QString> failed_keys = this->terrain_failures.keys();
    for (const QString &key : failed_keys)
    {
        if (terrainKeyInsideRange(key, dataset, range))
            this->terrain_failures.remove(key);
    }

    // In flight: forget the request, its answer may predate the deletion
    // (terrainDataReceived() only accepts pending keys); the next
    // requestTile() asks again.
    const QList<QString> pending_keys = this->terrain_pending.values();
    for (const QString &key : pending_keys)
    {
        if (!terrainKeyInsideRange(key, dataset, range))
            continue;
        this->terrain_pending.remove(key);
        this->terrain_requests_pending.remove(key);
    }
    return dropped_cached;
}

void MapTerrainRepository::terrainCacheDeleted(quint64 request_id, int deleted_count)
{
    finishTerrainDeletionRequest(request_id, deleted_count, QString());
}

void MapTerrainRepository::terrainCacheDeletionFailed(quint64 request_id, const QString &error)
{
    finishTerrainDeletionRequest(request_id, 0, error.isEmpty()
        ? QStringLiteral("Terrain cache deletion failed") : error);
}

void MapTerrainRepository::finishTerrainDeletionRequest(quint64 request_id, int deleted_count,
                                                        const QString &error)
{
    const QHash<quint64, TerrainDeletionRequest>::iterator request_iterator =
        this->terrain_deletion_requests.find(request_id);
    if (request_iterator == this->terrain_deletion_requests.end())
        return;
    const TerrainDeletionRequest request = request_iterator.value();
    this->terrain_deletion_requests.erase(request_iterator);

    const QHash<quint64, TerrainDeletionBatch>::iterator batch_iterator =
        this->terrain_deletion_batches.find(request.batch_id);
    if (batch_iterator == this->terrain_deletion_batches.end())
        return;
    TerrainDeletionBatch &batch = batch_iterator.value();

    if (!error.isEmpty())
    {
        qWarning().noquote() << QStringLiteral("Terrain cache deletion failed: %1").arg(error);
        if (batch.error.isEmpty())
            batch.error = error;
    }
    if (request.scope == MapTerrainCacheDeletionScope::NormalizedTiles)
    {
        if (error.isEmpty() && deleted_count >= 0)
            batch.deleted_tiles += deleted_count;
        else if (error.isEmpty())
            batch.tile_count_known = false;
        // A tile answered between the first invalidation and the service's
        // deletion carries the deleted data: drop it again, ask again.
        if (error.isEmpty() && invalidateTiles(batch.dataset, request.range))
            emit signalTerrainTilesDeleted();
    }
    else
    {
        if (error.isEmpty() && deleted_count >= 0)
            batch.deleted_source_files += deleted_count;
        else if (error.isEmpty())
            batch.source_count_known = false;
    }

    --batch.outstanding;
    if (request.scope == MapTerrainCacheDeletionScope::SourceData
        && !batch.deferred_tile_requests.isEmpty())
    {
        // The source data is gone (or could not all be deleted: the tiles
        // still go, as asked): now the tiles. Taken out of the batch first,
        // the sends can answer synchronously and erase it.
        const QVector<TerrainDeletionRequest> deferred = batch.deferred_tile_requests;
        const QString dataset = batch.dataset;
        batch.deferred_tile_requests.clear();
        for (const TerrainDeletionRequest &tile_request : deferred)
            sendTerrainDeletionRequest(dataset, tile_request);
        return;
    }
    if (batch.outstanding > 0)
        return;

    const int deleted_tiles = batch.tile_count_known ? batch.deleted_tiles : -1;
    const int deleted_source_files = batch.source_requested
        ? (batch.source_count_known ? batch.deleted_source_files : -1) : 0;
    const QString batch_error = batch.error;
    this->terrain_deletion_batches.erase(batch_iterator);
    emit signalTerrainCacheDeletionFinished(deleted_tiles, deleted_source_files, batch_error);
}

void MapTerrainRepository::abortTerrainDeletions(const QString &error)
{
    if (this->terrain_deletion_batches.isEmpty())
    {
        this->terrain_deletion_requests.clear();
        return;
    }
    const QList<TerrainDeletionBatch> batches = this->terrain_deletion_batches.values();
    this->terrain_deletion_batches.clear();
    this->terrain_deletion_requests.clear();
    for (const TerrainDeletionBatch &batch : batches)
        emit signalTerrainCacheDeletionFinished(batch.deleted_tiles, batch.deleted_source_files, error);
}

void MapTerrainRepository::initServerMapInterface()
{
    if (this->interface_map != nullptr)
    {
        this->interface_map->disconnect(this);
        this->interface_map->deleteLater();
        this->interface_map = nullptr;
    }

    abortTerrainDeletions(QStringLiteral("The map server connection changed before the "
                                         "terrain cache deletion finished"));
    this->terrain_generation++;
    this->terrain_pending.clear();
    this->terrain_requests_pending.clear();
    this->terrain_failures.clear();
    this->dataset_catalog_failure_count = 0;
    if (this->active_dataset.has_value())
    {
        this->active_dataset.reset();
        emit signalTerrainDatasetChanged();
    }

    if (this->transport.create)
        this->interface_map = this->transport.create(this);

    if (this->interface_map == nullptr)
        return;

    connect(this->interface_map, &InterfaceServerMap::signalTerrainTileDataReceived,
            this, &MapTerrainRepository::terrainDataReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainTileFailed,
            this, &MapTerrainRepository::terrainFailed);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainUpstreamActivity,
            this, &MapTerrainRepository::signalUpstreamActivityChanged);
    connect(this->interface_map, &InterfaceServerMap::signalUpstreamControlError,
            this, &MapTerrainRepository::signalUpstreamControlError);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainDatasetsReceived,
            this, &MapTerrainRepository::datasetCatalogReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainDatasetsFailed,
            this, &MapTerrainRepository::datasetCatalogFailed);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainCacheDeleted,
            this, &MapTerrainRepository::terrainCacheDeleted);
    connect(this->interface_map, &InterfaceServerMap::signalTerrainCacheDeletionFailed,
            this, &MapTerrainRepository::terrainCacheDeletionFailed);

    // Which dataset to draw and how fine it goes: nothing is requested from
    // the terrain service before this answer.
    requestDatasetCatalog();
}

void MapTerrainRepository::terrainDataReceived(const QString &key, const QByteArray &data)
{
    const QHash<QString, PendingTerrainRequest>::const_iterator request_iterator =
        this->terrain_requests_pending.constFind(key);
    if (!this->terrain_pending.contains(key) || request_iterator == this->terrain_requests_pending.constEnd())
        return;

    if (data.isEmpty())
    {
        terrainFailed(key, QStringLiteral("Terrain response is empty"));
        return;
    }

    const PendingTerrainRequest request = request_iterator.value();
#ifndef __EMSCRIPTEN__
    MapTerrainRepository *repository = this;
    this->terrain_decode_pool.start(QRunnable::create([repository, key, data, request]
    {
        QString error;
        const std::optional<MapTerrainTile> decoded = decodeMapTerrainTile(data, &error);

        QMetaObject::invokeMethod(repository, [repository, key, request, decoded, error]
        {
            repository->finishTerrainDecode(key, request.generation, decoded, error);
        }, Qt::QueuedConnection);
    }));
#else
    QString error;
    const std::optional<MapTerrainTile> decoded = decodeMapTerrainTile(data, &error);
    finishTerrainDecode(key, request.generation, decoded, error);
#endif
}

void MapTerrainRepository::finishTerrainDecode(const QString &key, quint64 generation,
                                                const std::optional<MapTerrainTile> &tile,
                                                const QString &error)
{
    if (generation != this->terrain_generation || !this->terrain_pending.contains(key))
        return;

    const QHash<QString, PendingTerrainRequest>::const_iterator request_iterator =
        this->terrain_requests_pending.constFind(key);
    if (request_iterator == this->terrain_requests_pending.constEnd())
        return;

    const PendingTerrainRequest request = request_iterator.value();
    if (!tile.has_value())
    {
        terrainFailed(key, error.isEmpty()
            ? QStringLiteral("Terrain tile decoding failed")
            : error);
        return;
    }

    if (tile->dataset != request.dataset ||
        tile->address.zoom != request.address.zoom ||
        tile->address.x != request.address.x ||
        tile->address.y != request.address.y)
    {
        terrainFailed(
            key,
            QStringLiteral("Terrain tile embedded dataset/XYZ metadata does not match the request"));
        return;
    }

    this->terrain_pending.remove(key);
    this->terrain_requests_pending.remove(key);

    const qint64 cost_bytes =
        qint64(tile->elevations_m.size()) * qint64(sizeof(float)) +
        qint64(tile->dataset.size()) * qint64(sizeof(QChar)) + qint64(sizeof(MapTerrainTile));
    const qint64 cost_kib_rounded = (cost_bytes + 1023) / 1024;
    const int cost_kib = qMax(
        1, int(qMin<qint64>(cost_kib_rounded, TerrainCacheMaximumCostKiB)));

    if (!this->cache.insert(key, new MapTerrainTile(tile.value()), cost_kib))
    {
        terrainFailed(key, QStringLiteral("Failed to insert terrain tile into memory cache"));
        return;
    }

    this->terrain_failures.remove(key);
    emit signalTerrainTileAvailable(key);
}

void MapTerrainRepository::terrainFailed(const QString &key, const QString &error)
{
    this->terrain_pending.remove(key);
    this->terrain_requests_pending.remove(key);

    TerrainFailure failure = this->terrain_failures.value(key);
    failure.count = qMin(failure.count + 1, 6);

    const qint64 delay_multiplier = qint64(1) << (failure.count - 1);
    const qint64 retry_delay = qMin(
        TerrainRetryInitialDelayMs * delay_multiplier, TerrainRetryMaximumDelayMs);
    failure.retry_after_msecs = QDateTime::currentMSecsSinceEpoch() + retry_delay;

    this->terrain_failures.insert(key, failure);
    qWarning() << "Terrain tile request failed:" << key << error;
    emit signalTerrainTileFailed(key, error);

    const qint64 retry_after_msecs = failure.retry_after_msecs;
    QTimer::singleShot(int(retry_delay), this, [this, key, retry_after_msecs]
    {
        const QHash<QString, TerrainFailure>::const_iterator current_failure =
            this->terrain_failures.constFind(key);
        if (current_failure == this->terrain_failures.constEnd() ||
            current_failure->retry_after_msecs != retry_after_msecs)
        {
            return;
        }

        emit signalTerrainTileRetryReady(key);
    });
}
