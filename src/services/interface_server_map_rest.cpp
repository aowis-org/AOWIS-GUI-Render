#include "services/interface_server_map_rest.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
bool isUpstreamCancellationError(const QString &error)
{
    return error.contains(QStringLiteral("upstream download canceled"), Qt::CaseInsensitive);
}
}

InterfaceServerMapREST::InterfaceServerMapREST(const MapServerRestConfiguration &server,
                                               QObject *parent)
    : InterfaceServerMap(parent)
{
    initRestConnection(server);
}

void InterfaceServerMapREST::initRestConnection(const MapServerRestConfiguration &server)
{
    this->rest = new RESTClient(server.base_url, server.api_key, server.delete_api_key, this,
                                server.proxy);

    connect(this->rest, &RESTClient::requestFinishedTile, this,
            [this](const QByteArray &data, const QString &key)
    {
        this->rest_pending.remove(key);

        if (data.isEmpty())
        {
            qWarning() << "Tile response is empty:" << key;
            emit signalTileFailed(key);
            return;
        }

        emit signalTileDataReceived(key, data);
    });

    connect(this->rest, &RESTClient::requestTileError, this,
            [this](const QString &key, const QString &error)
    {
        this->rest_pending.remove(key);
        if (!isUpstreamCancellationError(error))
            qWarning() << "Tile request failed:" << key << error;
        emit signalTileFailed(key);
    });

    connect(this->rest, &RESTClient::requestFinishedTerrainTile, this,
            [this](const QByteArray &data, const QString &key)
    {
        this->terrain_pending.remove(key);

        if (data.isEmpty())
        {
            const QString error = QStringLiteral("Terrain response is empty");
            qWarning() << error << key;
            emit signalTerrainTileFailed(key, error);
            return;
        }

        emit signalTerrainTileDataReceived(key, data);
    });

    connect(this->rest, &RESTClient::requestTerrainTileError, this,
            [this](const QString &key, const QString &error)
    {
        this->terrain_pending.remove(key);
        if (!isUpstreamCancellationError(error))
            qWarning() << "Terrain tile request failed:" << key << error;
        emit signalTerrainTileFailed(key, error);
    });

    connect(this->rest, &RESTClient::requestFinished, this,
            [this](const QByteArray &data)
    {
        if (!this->terrain_elevation_pending)
            return;

        this->terrain_elevation_pending = false;
        emit signalTerrainElevationDataReceived(data);
    });

    connect(this->rest, &RESTClient::requestError, this,
            [this](const QString &error)
    {
        if (!this->terrain_elevation_pending)
            return;

        this->terrain_elevation_pending = false;
        emit signalTerrainElevationFailed(error);
    });

    connect(this->rest, &RESTClient::requestFinishedControl, this,
            [this](const QString &request_key, const QByteArray &data)
    {
        if (request_key == QStringLiteral("terrain-datasets"))
        {
            this->terrain_datasets_pending = false;
            emit signalTerrainDatasetsReceived(data);
            return;
        }

        if (request_key == QStringLiteral("map-upstream-activity"))
            this->map_upstream_activity_pending = false;
        else if (request_key == QStringLiteral("terrain-upstream-activity"))
            this->terrain_upstream_activity_pending = false;

        if (request_key != QStringLiteral("map-upstream-activity") &&
            request_key != QStringLiteral("terrain-upstream-activity"))
        {
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (!document.isObject())
        {
            emit signalUpstreamControlError(QStringLiteral("Map server returned invalid upstream activity JSON"));
            return;
        }

        const QJsonObject root = document.object();
        const QString section_name = request_key == QStringLiteral("map-upstream-activity")
            ? QStringLiteral("map_tiles")
            : QStringLiteral("terrain");
        const QJsonObject section = root.value(section_name).toObject();
        MapUpstreamActivity activity;
        activity.active = qMax(0, section.value(QStringLiteral("active")).toInt());
        activity.queued = qMax(0, section.value(QStringLiteral("queued")).toInt());

        if (request_key == QStringLiteral("map-upstream-activity"))
            emit signalMapTileUpstreamActivity(activity);
        else
            emit signalTerrainUpstreamActivity(activity);
    });

    connect(this->rest, &RESTClient::requestControlError, this,
            [this](const QString &request_key, const QString &error)
    {
        if (request_key == QStringLiteral("terrain-datasets"))
        {
            this->terrain_datasets_pending = false;
            emit signalTerrainDatasetsFailed(error);
            return;
        }

        if (request_key == QStringLiteral("map-upstream-activity"))
            this->map_upstream_activity_pending = false;
        else if (request_key == QStringLiteral("terrain-upstream-activity"))
            this->terrain_upstream_activity_pending = false;
        emit signalUpstreamControlError(error);
    });

    connect(this->rest, &RESTClient::requestFinishedDeleteData, this,
            [this](quint64 rest_id, const QByteArray &data)
    {
        const QHash<quint64, quint64>::iterator terrain =
            this->terrain_deletion_request_ids.find(rest_id);
        if (terrain == this->terrain_deletion_request_ids.end())
            return;

        const quint64 request_id = terrain.value();
        this->terrain_deletion_request_ids.erase(terrain);
        // The body is the deleted file count.
        bool count_valid = false;
        const int deleted_count = QString::fromUtf8(data).trimmed().toInt(&count_valid);
        emit signalTerrainCacheDeleted(request_id, count_valid ? deleted_count : -1);
    });

    connect(this->rest, &RESTClient::requestFinishedDelete, this,
            [this](quint64 request_id)
    {
        // Terrain ids were answered (and removed) by requestFinishedDeleteData.
        if (request_id >= (quint64(1) << 63))
            return;
        emit signalTilesDeleted(request_id);
    });

    connect(this->rest, &RESTClient::requestDeleteError, this,
            [this](quint64 request_id, const QString &error)
    {
        const QHash<quint64, quint64>::iterator terrain =
            this->terrain_deletion_request_ids.find(request_id);
        if (terrain != this->terrain_deletion_request_ids.end())
        {
            const quint64 terrain_request_id = terrain.value();
            this->terrain_deletion_request_ids.erase(terrain);
            emit signalTerrainCacheDeletionFailed(terrain_request_id, error);
            return;
        }
        emit signalTileDeletionFailed(request_id, error);
    });
}

void InterfaceServerMapREST::requestTile(const QString &endpoint, const QString &key, int x, int y)
{
    Q_UNUSED(x)
    Q_UNUSED(y)

    if (this->rest_pending.contains(key))
        return;

    this->rest_pending.insert(key);
    this->rest->getTile(endpoint, key);
}

void InterfaceServerMapREST::requestTerrainTile(const QString &endpoint, const QString &key)
{
    if (this->terrain_pending.contains(key))
        return;

    this->terrain_pending.insert(key);
    this->rest->getTerrainTile(endpoint, key);
}

void InterfaceServerMapREST::requestTerrainDatasets()
{
    if (this->terrain_datasets_pending)
        return;
    this->terrain_datasets_pending = true;
    this->rest->getControl(QStringLiteral("/terrain/v1/datasets"),
                           QStringLiteral("terrain-datasets"));
}

void InterfaceServerMapREST::requestTerrainElevation(const QString &endpoint)
{
    if (this->terrain_elevation_pending)
    {
        emit signalTerrainElevationFailed(
            QStringLiteral("A terrain elevation request is already in progress"));
        return;
    }

    this->terrain_elevation_pending = true;
    this->rest->get(endpoint);
}

void InterfaceServerMapREST::deleteTiles(quint64 request_id, const QString &provider, int zoom,
                                         int tile_x_min, int tile_x_max,
                                         int tile_y_min, int tile_y_max)
{
    const QString endpoint = QString("/cache/%1/%2/%3/%4/%5/%6")
        .arg(provider)
        .arg(zoom)
        .arg(tile_x_min)
        .arg(tile_x_max)
        .arg(tile_y_min)
        .arg(tile_y_max);
    this->rest->deleteResource(endpoint, request_id);
}

void InterfaceServerMapREST::deleteTerrainCache(quint64 request_id,
                                                MapTerrainCacheDeletionScope scope,
                                                const QString &dataset, int zoom,
                                                int tile_x_min, int tile_x_max,
                                                int tile_y_min, int tile_y_max)
{
    const QString endpoint = QStringLiteral("/terrain/v1/%1/%2/%3/%4/%5/%6/%7")
        .arg(scope == MapTerrainCacheDeletionScope::SourceData
                 ? QStringLiteral("source") : QStringLiteral("cache"))
        .arg(dataset)
        .arg(zoom)
        .arg(tile_x_min)
        .arg(tile_x_max)
        .arg(tile_y_min)
        .arg(tile_y_max);
    const quint64 rest_id = this->next_terrain_deletion_rest_id++;
    this->terrain_deletion_request_ids.insert(rest_id, request_id);
    this->rest->deleteResource(endpoint, rest_id);
}

void InterfaceServerMapREST::requestMapTileUpstreamActivity()
{
    if (this->map_upstream_activity_pending)
        return;
    this->map_upstream_activity_pending = true;
    this->rest->getControl(QStringLiteral("/upstream/v1/activity"),
                           QStringLiteral("map-upstream-activity"));
}

void InterfaceServerMapREST::requestTerrainUpstreamActivity()
{
    if (this->terrain_upstream_activity_pending)
        return;
    this->terrain_upstream_activity_pending = true;
    this->rest->getControl(QStringLiteral("/upstream/v1/activity"),
                           QStringLiteral("terrain-upstream-activity"));
}

void InterfaceServerMapREST::cancelMapTileUpstreamDownloads()
{
    this->rest->deleteControl(QStringLiteral("/upstream/v1/map-tiles"),
                              QStringLiteral("cancel-map-upstream"));
}

void InterfaceServerMapREST::cancelTerrainUpstreamDownloads()
{
    this->rest->deleteControl(QStringLiteral("/upstream/v1/terrain"),
                              QStringLiteral("cancel-terrain-upstream"));
}
