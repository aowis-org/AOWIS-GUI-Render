#include "map/data/map_tile_repository.h"
#include "map/render/map_render_diagnostics.h"

#include <functional>
#ifdef Q_OS_WASM
#include "map/data/map_tile_browser_decoder.h"
#endif

#include <QDateTime>
#include <QDebug>
#include <QList>
#include <QTimer>
#include <QImage>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>

namespace
{
constexpr int TileCacheMaximumCostKiB = 512 * 1024;
constexpr qint64 TileRetryInitialDelayMs = 1000;
constexpr qint64 TileRetryMaximumDelayMs = 30000;
constexpr int MaximumQueuedTileRequests = 1024;
constexpr quint64 TileRequestBatchRetention = 8;
constexpr int MaximumTileRequestsInFlight = 48;
constexpr int MaximumBackgroundTileRequestsInFlight = 16;
#ifdef Q_OS_WASM
constexpr int TileDecodeThreadCountMaximum = 2;
#else
// Keep decoding from saturating desktop CPU cores while the map is moving.
// The prior eight-worker setting remains selectable for controlled A/B tests.
constexpr int TileDecodeThreadCountMaximum = 4;
#endif
#ifdef Q_OS_WIN
constexpr int TileRequestDispatchIntervalMs = 1;
#else
constexpr int TileRequestDispatchIntervalMs = 0;
#endif

int positiveModulo(int value, int divisor)
{
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

bool tileXInsideRange(int tile_x, int tile_count, int tile_x_min, int tile_x_max)
{
    const qint64 range_width = qint64(tile_x_max) - tile_x_min + 1;
    if (range_width >= tile_count)
        return true;

    const int first_wrapped_x = positiveModulo(tile_x_min, tile_count);
    const int offset = positiveModulo(tile_x - first_wrapped_x, tile_count);
    return offset < range_width;
}
}

MapTileRepository::MapTileRepository(const MapServerTransport &transport, QObject *parent)
    : QObject(parent),
    transport(transport),
    cache(TileCacheMaximumCostKiB)
{
    this->tile_request_timer = new QTimer(this);
    this->tile_request_timer->setSingleShot(true);
    this->tile_request_timer->setTimerType(Qt::PreciseTimer);
    this->tile_request_timer->setInterval(TileRequestDispatchIntervalMs);
    connect(this->tile_request_timer, &QTimer::timeout,
            this, &MapTileRepository::processTileRequestQueue);

    bool decode_override_valid = false;
    const int decode_override = qEnvironmentVariableIntValue(
        "AOWIS_MAP_TILE_DECODE_THREADS", &decode_override_valid);
    const int default_decode_threads = qMax(1, qMin(TileDecodeThreadCountMaximum,
        qMax(1, QThread::idealThreadCount())));
    const int decode_threads = decode_override_valid
        && decode_override >= 1 && decode_override <= 16
        ? decode_override : default_decode_threads;
    this->tile_decode_pool.setMaxThreadCount(decode_threads);
    this->tile_decode_pool.setExpiryTimeout(30000);

    initServerMapInterface();
}

MapTileRepository::~MapTileRepository()
{
    this->tile_decode_pool.clear();
    this->tile_decode_pool.waitForDone();
}

const QImage *MapTileRepository::tile(const QString &key) const
{
    return this->cache.object(key);
}

quint64 MapTileRepository::beginTileRequestBatch(
    const void *owner, const QString &layout_key)
{
    const quintptr owner_id = reinterpret_cast<quintptr>(owner);
    if (owner_id != 0
        && this->active_tile_request_owner == owner_id
        && this->active_tile_request_layout_key == layout_key
        && this->active_tile_request_batch != 0)
    {
        return this->active_tile_request_batch;
    }

    quint64 request_batch = this->next_tile_request_batch++;
    if (request_batch == 0)
        request_batch = this->next_tile_request_batch++;

    this->active_tile_request_owner = owner_id;
    this->active_tile_request_layout_key = layout_key;
    this->active_tile_request_batch = request_batch;

    if (request_batch > TileRequestBatchRetention)
    {
        const quint64 oldest_retained_batch = request_batch - TileRequestBatchRetention;
        QHash<QString, PendingTileRequest>::iterator iterator =
            this->tile_requests_queued.begin();
        while (iterator != this->tile_requests_queued.end())
        {
            if (iterator.value().request_batch >= oldest_retained_batch)
            {
                ++iterator;
                continue;
            }

            const QString key = iterator.key();
            iterator = this->tile_requests_queued.erase(iterator);
            this->tiles_pending.remove(key);
            this->tiles_invalidated_while_pending.remove(key);
        }
    }

    return request_batch;
}

void MapTileRepository::requestTile(const QString &endpoint, const QString &key,
                                    int x, int y, int priority,
                                    quint64 request_batch, bool foreground)
{
    if (this->cache.contains(key) || !this->interface_map ||
        tileDeletionPending(key, x, y))
    {
        return;
    }

    const QHash<QString, TileFailure>::const_iterator failure =
        this->tile_failures.constFind(key);
    if (failure != this->tile_failures.constEnd() &&
        QDateTime::currentMSecsSinceEpoch() < failure->retry_after_msecs)
    {
        return;
    }

    if (this->tiles_in_flight.contains(key))
        return;

    QHash<QString, PendingTileRequest>::iterator queued =
        this->tile_requests_queued.find(key);
    if (queued != this->tile_requests_queued.end())
    {
        if (request_batch > queued.value().request_batch)
        {
            queued.value().endpoint = endpoint;
            queued.value().x = x;
            queued.value().y = y;
            queued.value().priority = qMax(0, priority);
            queued.value().request_batch = request_batch;
            queued.value().request_sequence = this->next_tile_request_sequence++;
            queued.value().foreground = foreground;
        }
        else if (request_batch == queued.value().request_batch)
        {
            queued.value().priority = qMin(queued.value().priority, qMax(0, priority));
            queued.value().foreground = queued.value().foreground || foreground;
        }
        return;
    }

    // The HTTP transfer may already have completed while PNG decoding is still
    // running. Keep that state pending without issuing a duplicate request.
    if (this->tiles_pending.contains(key))
        return;

    PendingTileRequest request;
    request.endpoint = endpoint;
    request.x = x;
    request.y = y;
    request.priority = qMax(0, priority);
    request.request_batch = request_batch;
    request.request_sequence = this->next_tile_request_sequence++;
    request.foreground = foreground;

    this->tiles_pending.insert(key);
    this->tile_requests_queued.insert(key, request);
    trimTileRequestQueue();
    scheduleTileRequestDispatch();
}

int MapTileRepository::tileRequestInFlightLimit() const
{
    // A transport may need a lower limit: the GUI's Windows Standalone
    // client keeps six (Qt's HTTP/1 per-host concurrency), see the host's
    // MapServerTransport.
    return this->transport.tile_requests_in_flight > 0
        ? this->transport.tile_requests_in_flight
        : MaximumTileRequestsInFlight;
}

int MapTileRepository::backgroundTileRequestInFlightLimit() const
{
    return this->transport.background_tile_requests_in_flight > 0
        ? this->transport.background_tile_requests_in_flight
        : MaximumBackgroundTileRequestsInFlight;
}

void MapTileRepository::supersedeTileSources(const QSet<QString> &wanted_sources)
{
    const std::function<bool(const QString &)> wanted = [&wanted_sources](const QString &key)
    {
        const qsizetype separator = key.indexOf(QLatin1Char('/'));
        return separator > 0 && wanted_sources.contains(key.left(separator));
    };

    QHash<QString, PendingTileRequest>::iterator queued = this->tile_requests_queued.begin();
    while (queued != this->tile_requests_queued.end())
    {
        if (wanted(queued.key()))
        {
            ++queued;
            continue;
        }
        const QString key = queued.key();
        queued = this->tile_requests_queued.erase(queued);
        this->tiles_pending.remove(key);
        this->tiles_invalidated_while_pending.remove(key);
    }

    // In flight: still pending (the answer is decoded and cached), just no
    // longer occupying a request slot.
    const QList<QString> in_flight = this->tiles_in_flight.values();
    for (const QString &key : in_flight)
    {
        if (wanted(key))
            continue;
        this->tiles_in_flight.remove(key);
        this->background_tiles_in_flight.remove(key);
    }
    scheduleTileRequestDispatch();
}

void MapTileRepository::scheduleTileRequestDispatch()
{
    if (!this->interface_map || this->tile_requests_queued.isEmpty() ||
        this->tiles_in_flight.size() >= tileRequestInFlightLimit() ||
        this->tile_request_timer->isActive())
    {
        return;
    }

    this->tile_request_timer->start();
}

void MapTileRepository::processTileRequestQueue()
{
    if (!this->interface_map)
        return;

    const int in_flight_limit = tileRequestInFlightLimit();
    const int background_in_flight_limit = backgroundTileRequestInFlightLimit();
#ifdef Q_OS_WIN
    const int dispatch_limit = 1;
#else
    const int dispatch_limit = in_flight_limit;
#endif
    int dispatched = 0;

    while (!this->tile_requests_queued.isEmpty() &&
           this->tiles_in_flight.size() < in_flight_limit &&
           dispatched < dispatch_limit)
    {
        QHash<QString, PendingTileRequest>::iterator best =
            this->tile_requests_queued.end();
        QHash<QString, PendingTileRequest>::iterator iterator =
            this->tile_requests_queued.begin();
        while (iterator != this->tile_requests_queued.end())
        {
            if (best == this->tile_requests_queued.end() ||
                iterator.value().request_batch > best.value().request_batch ||
                (iterator.value().request_batch == best.value().request_batch &&
                 iterator.value().foreground && !best.value().foreground) ||
                (iterator.value().request_batch == best.value().request_batch &&
                 iterator.value().foreground == best.value().foreground &&
                 iterator.value().priority < best.value().priority) ||
                (iterator.value().request_batch == best.value().request_batch &&
                 iterator.value().foreground == best.value().foreground &&
                 iterator.value().priority == best.value().priority &&
                 iterator.value().request_sequence > best.value().request_sequence))
            {
                best = iterator;
            }
            ++iterator;
        }

        if (best == this->tile_requests_queued.end())
            break;

        if (!best.value().foreground
            && this->background_tiles_in_flight.size() >=
                background_in_flight_limit)
        {
            break;
        }

        const QString key = best.key();
        const PendingTileRequest request = best.value();
        this->tile_requests_queued.erase(best);

        if (this->cache.contains(key) ||
            tileDeletionPending(key, request.x, request.y))
        {
            this->tiles_pending.remove(key);
            this->tiles_invalidated_while_pending.remove(key);
            continue;
        }

        this->tiles_in_flight.insert(key);
        if (!request.foreground)
            this->background_tiles_in_flight.insert(key);
        ++dispatched;
        this->interface_map->requestTile(
            request.endpoint, key, request.x, request.y);
    }

    if (dispatched > 0 && !this->tile_requests_queued.isEmpty()
        && this->tiles_in_flight.size() < in_flight_limit)
    {
        scheduleTileRequestDispatch();
    }
}

void MapTileRepository::trimTileRequestQueue()
{
    while (this->tile_requests_queued.size() > MaximumQueuedTileRequests)
    {
        QHash<QString, PendingTileRequest>::iterator worst =
            this->tile_requests_queued.end();
        QHash<QString, PendingTileRequest>::iterator iterator =
            this->tile_requests_queued.begin();
        while (iterator != this->tile_requests_queued.end())
        {
            if (worst == this->tile_requests_queued.end() ||
                iterator.value().request_batch < worst.value().request_batch ||
                (iterator.value().request_batch == worst.value().request_batch &&
                 !iterator.value().foreground && worst.value().foreground) ||
                (iterator.value().request_batch == worst.value().request_batch &&
                 iterator.value().foreground == worst.value().foreground &&
                 iterator.value().priority > worst.value().priority) ||
                (iterator.value().request_batch == worst.value().request_batch &&
                 iterator.value().foreground == worst.value().foreground &&
                 iterator.value().priority == worst.value().priority &&
                 iterator.value().request_sequence < worst.value().request_sequence))
            {
                worst = iterator;
            }
            ++iterator;
        }

        if (worst == this->tile_requests_queued.end())
            break;

        const QString key = worst.key();
        this->tile_requests_queued.erase(worst);
        this->tiles_pending.remove(key);
        this->tiles_invalidated_while_pending.remove(key);
    }
}

void MapTileRepository::deleteTiles(const QString &provider, int zoom,
                                    int tile_x_min, int tile_x_max,
                                    int tile_y_min, int tile_y_max)
{
    if (provider.isEmpty() || zoom < 0 || zoom > 30 ||
        tile_x_min > tile_x_max || tile_y_min > tile_y_max)
    {
        return;
    }

    PendingTileDeletion deletion;
    deletion.key_prefix = QString("%1/%2/").arg(provider).arg(zoom);
    deletion.tile_count = 1 << zoom;
    deletion.tile_x_min = tile_x_min;
    deletion.tile_x_max = tile_x_max;
    deletion.tile_y_min = tile_y_min;
    deletion.tile_y_max = tile_y_max;
    invalidateTiles(deletion);

    if (!this->interface_map)
    {
        emit signalTileRetryReady(deletion.key_prefix);
        return;
    }

    quint64 request_id = this->next_tile_deletion_id++;
    if (request_id == 0)
        request_id = this->next_tile_deletion_id++;

    this->tile_deletions_pending.insert(request_id, deletion);
    this->interface_map->deleteTiles(
        request_id, provider, zoom,
        tile_x_min, tile_x_max, tile_y_min, tile_y_max);
}

void MapTileRepository::invalidateTiles(const PendingTileDeletion &deletion)
{
    QSet<QString> candidate_keys = this->tiles_pending;
    const QList<QString> cached_keys = this->cache.keys();
    for (const QString &key : cached_keys)
        candidate_keys.insert(key);

    const QList<QString> failed_keys = this->tile_failures.keys();
    for (const QString &key : failed_keys)
        candidate_keys.insert(key);

    for (const QString &key : candidate_keys)
    {
        if (!key.startsWith(deletion.key_prefix))
            continue;

        const QString coordinates = key.mid(deletion.key_prefix.size());
        const qsizetype separator_index = coordinates.indexOf('/');
        if (separator_index <= 0 || coordinates.indexOf('/', separator_index + 1) >= 0)
            continue;

        bool tile_x_valid = false;
        bool tile_y_valid = false;
        const int tile_x = coordinates.left(separator_index).toInt(&tile_x_valid);
        const int tile_y = coordinates.mid(separator_index + 1).toInt(&tile_y_valid);
        if (!tile_x_valid || !tile_y_valid ||
            tile_y < deletion.tile_y_min || tile_y > deletion.tile_y_max ||
            !tileXInsideRange(tile_x, deletion.tile_count,
                              deletion.tile_x_min, deletion.tile_x_max))
        {
            continue;
        }

        this->cache.remove(key);
        this->tile_failures.remove(key);

        if (this->tile_requests_queued.remove(key) > 0)
        {
            this->tiles_pending.remove(key);
            this->tiles_invalidated_while_pending.remove(key);
            continue;
        }
        if (this->tiles_pending.contains(key))
            this->tiles_invalidated_while_pending.insert(key);
        else
            this->tiles_invalidated_while_pending.remove(key);
    }
}

bool MapTileRepository::tileDeletionPending(const QString &key, int x, int y) const
{
    QHash<quint64, PendingTileDeletion>::const_iterator iterator =
        this->tile_deletions_pending.constBegin();
    while (iterator != this->tile_deletions_pending.constEnd())
    {
        const PendingTileDeletion &deletion = iterator.value();
        if (key.startsWith(deletion.key_prefix) &&
            y >= deletion.tile_y_min && y <= deletion.tile_y_max &&
            tileXInsideRange(x, deletion.tile_count,
                             deletion.tile_x_min, deletion.tile_x_max))
        {
            return true;
        }

        ++iterator;
    }

    return false;
}

void MapTileRepository::finishTileDeletion(quint64 request_id, const QString &error)
{
    QHash<quint64, PendingTileDeletion>::iterator iterator =
        this->tile_deletions_pending.find(request_id);
    if (iterator == this->tile_deletions_pending.end())
        return;

    const PendingTileDeletion deletion = iterator.value();
    this->tile_deletions_pending.erase(iterator);

    if (!error.isEmpty())
        qWarning() << "Tile cache deletion failed:" << error;
    else
        emit signalTilesDeleted();

    emit signalTileRetryReady(deletion.key_prefix);
}

void MapTileRepository::requestUpstreamActivity()
{
    if (this->interface_map != nullptr)
        this->interface_map->requestMapTileUpstreamActivity();
}

void MapTileRepository::cancelUpstreamDownloads()
{
    if (this->interface_map != nullptr)
        this->interface_map->cancelMapTileUpstreamDownloads();
}

void MapTileRepository::invalidateProviderCatalog()
{
    // Provider UUIDs are stable, but their URLs are editable. Old HTTP
    // replies and decoded tiles must not populate the replacement catalog.
    this->cache.clear();
    initServerMapInterface();
    emit signalTilesDeleted();
}

void MapTileRepository::initServerMapInterface()
{
    if (this->interface_map)
    {
        this->interface_map->disconnect(this);
        this->interface_map->deleteLater();
        this->interface_map = nullptr;
    }

    this->tile_generation++;
    this->tile_request_timer->stop();
    this->tile_requests_queued.clear();
    this->tiles_in_flight.clear();
    this->background_tiles_in_flight.clear();
    this->active_tile_request_owner = 0;
    this->active_tile_request_layout_key.clear();
    this->active_tile_request_batch = 0;
    this->tiles_pending.clear();
    this->tiles_invalidated_while_pending.clear();
    this->tile_failures.clear();
    this->tile_deletions_pending.clear();

    if (this->transport.create)
        this->interface_map = this->transport.create(this);

    if (!this->interface_map)
        return;

    connect(this->interface_map, &InterfaceServerMap::signalTileDataReceived,
            this, &MapTileRepository::tileDataReceived);
    connect(this->interface_map, &InterfaceServerMap::signalTileFailed,
            this, &MapTileRepository::tileFailed);
    connect(this->interface_map, &InterfaceServerMap::signalMapTileUpstreamActivity,
            this, &MapTileRepository::signalUpstreamActivityChanged);
    connect(this->interface_map, &InterfaceServerMap::signalUpstreamControlError,
            this, &MapTileRepository::signalUpstreamControlError);
    connect(this->interface_map, &InterfaceServerMap::signalTilesDeleted,
            this, [this](quint64 request_id)
    {
        finishTileDeletion(request_id);
    });
    connect(this->interface_map, &InterfaceServerMap::signalTileDeletionFailed,
            this, [this](quint64 request_id, const QString &error)
    {
        finishTileDeletion(request_id, error);
    });
}

void MapTileRepository::tileDataReceived(const QString &key, const QByteArray &data)
{
    this->tiles_in_flight.remove(key);
    this->background_tiles_in_flight.remove(key);
    scheduleTileRequestDispatch();

    if (!this->tiles_pending.contains(key))
        return;

    if (data.isEmpty())
    {
        tileFailed(key);
        return;
    }

    const quint64 generation = this->tile_generation;
#ifdef Q_OS_WASM
    // The browser's native decoders on their own threads, pixels written
    // straight into the RGBA8888 image (map_tile_browser_decoder.h). What the
    // browser cannot decode - and the first tiles, until its decode worker
    // runs - goes through Qt below.
    if (MapTileBrowserDecoder::decode(data, this, [this, key, data, generation](const QImage &image)
        {
            if (image.isNull())
                decodeTileWithQt(key, data, generation);
            else
                finishTileDecode(key, generation, image);
        }))
    {
        return;
    }
#endif
    decodeTileWithQt(key, data, generation);
}

void MapTileRepository::decodeTileWithQt(const QString &key, const QByteArray &data,
                                         quint64 generation)
{
    MapTileRepository *repository = this;
    this->tile_decode_pool.start(QRunnable::create([repository, key, data, generation]
    {
        QImage image;
        image.loadFromData(data);
        // The renderer's texture format (bgfx RGBA8), here on the decode
        // thread instead of per upload on the GUI thread.
        if (!image.isNull() && image.format() != QImage::Format_RGBA8888)
            image = image.convertToFormat(QImage::Format_RGBA8888);

        QMetaObject::invokeMethod(repository, [repository, key, generation, image]
        {
            repository->finishTileDecode(key, generation, image);
        }, Qt::QueuedConnection);
    }));
}

void MapTileRepository::finishTileDecode(
    const QString &key, quint64 generation, const QImage &image)
{
    const MapRenderDiagnostics::Scope timing(
        MapRenderDiagnostics::Counter::TileDecodeCompletion);
    if (generation != this->tile_generation || !this->tiles_pending.contains(key))
        return;

    this->tiles_pending.remove(key);

    if (this->tiles_invalidated_while_pending.remove(key))
    {
        this->tile_failures.remove(key);
        emit signalTileRetryReady(key);
        return;
    }

    if (image.isNull())
    {
        tileFailed(key);
        return;
    }

    const qint64 cost_bytes = image.sizeInBytes();
    const qint64 cost_kib_rounded = (cost_bytes + 1023) / 1024;
    const int cost_kib = qMax(
        1, int(qMin<qint64>(cost_kib_rounded, TileCacheMaximumCostKiB)));

    if (!this->cache.insert(key, new QImage(image), cost_kib))
    {
        tileFailed(key);
        return;
    }

    this->tile_failures.remove(key);
    emit signalTileAvailable(key);
}

void MapTileRepository::tileFailed(const QString &key)
{
    this->tile_requests_queued.remove(key);
    this->tiles_in_flight.remove(key);
    this->background_tiles_in_flight.remove(key);
    this->tiles_pending.remove(key);
    scheduleTileRequestDispatch();

    if (this->tiles_invalidated_while_pending.remove(key))
    {
        this->tile_failures.remove(key);
        emit signalTileRetryReady(key);
        return;
    }

    TileFailure failure = this->tile_failures.value(key);
    failure.count = qMin(failure.count + 1, 6);

    const qint64 delay_multiplier = qint64(1) << (failure.count - 1);
    const qint64 retry_delay = qMin(
        TileRetryInitialDelayMs * delay_multiplier, TileRetryMaximumDelayMs);
    failure.retry_after_msecs = QDateTime::currentMSecsSinceEpoch() + retry_delay;

    this->tile_failures.insert(key, failure);
    const qint64 retry_after_msecs = failure.retry_after_msecs;
    QTimer::singleShot(int(retry_delay), this, [this, key, retry_after_msecs]
    {
        const QHash<QString, TileFailure>::const_iterator current_failure =
            this->tile_failures.constFind(key);
        if (current_failure == this->tile_failures.constEnd() ||
            current_failure->retry_after_msecs != retry_after_msecs)
        {
            return;
        }

        emit signalTileRetryReady(key);
    });
}
