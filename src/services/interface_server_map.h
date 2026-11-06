#ifndef INTERFACE_SERVER_MAP_H
#define INTERFACE_SERVER_MAP_H

#include <QByteArray>
#include <QObject>
#include <QString>

struct MapUpstreamActivity
{
    int active = 0;
    int queued = 0;
};

// What a terrain cache deletion removes (the map server's DELETE
// /terrain/v1/cache/... and /terrain/v1/source/...).
enum class MapTerrainCacheDeletionScope
{
    // The normalized .aowterrain tiles of the XYZ range.
    NormalizedTiles,
    // The provider-native source data (downloaded DEM rasters) covering the
    // range's area.
    SourceData
};

class InterfaceServerMap : public QObject
{
    Q_OBJECT

public:
    explicit InterfaceServerMap(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual void requestTile(const QString &endpoint, const QString &key, int x, int y) = 0;
    virtual void requestTerrainTile(const QString &endpoint, const QString &key)
    {
        Q_UNUSED(endpoint)
        emit signalTerrainTileFailed(
            key, QStringLiteral("Terrain transport is not available in this map-server mode"));
    }
    // The terrain service's dataset catalog (GET /terrain/v1/datasets): which
    // dataset to draw and its zoom range. Answered by
    // signalTerrainDatasetsReceived() with the catalog JSON, or
    // signalTerrainDatasetsFailed().
    virtual void requestTerrainDatasets()
    {
        emit signalTerrainDatasetsFailed(
            QStringLiteral("Terrain dataset catalog is not available in this map-server mode"));
    }
    virtual void requestTerrainElevation(const QString &endpoint)
    {
        Q_UNUSED(endpoint)
        emit signalTerrainElevationFailed(
            QStringLiteral("Terrain elevation transport is not available in this map-server mode"));
    }
    virtual void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                             int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) = 0;
    // Deletes cached terrain of one zoom level's XYZ range (the map-tile
    // deleteTiles() range semantics). Answered by signalTerrainCacheDeleted()
    // with the number of deleted files, or signalTerrainCacheDeletionFailed().
    virtual void deleteTerrainCache(quint64 request_id, MapTerrainCacheDeletionScope scope,
                                    const QString &dataset, int zoom,
                                    int tile_x_min, int tile_x_max,
                                    int tile_y_min, int tile_y_max)
    {
        Q_UNUSED(scope)
        Q_UNUSED(dataset)
        Q_UNUSED(zoom)
        Q_UNUSED(tile_x_min)
        Q_UNUSED(tile_x_max)
        Q_UNUSED(tile_y_min)
        Q_UNUSED(tile_y_max)
        emit signalTerrainCacheDeletionFailed(
            request_id,
            QStringLiteral("Terrain cache deletion is not available in this map-server mode"));
    }
    virtual void requestMapTileUpstreamActivity() = 0;
    virtual void requestTerrainUpstreamActivity() = 0;
    virtual void cancelMapTileUpstreamDownloads() = 0;
    virtual void cancelTerrainUpstreamDownloads() = 0;

signals:
    void signalTileDataReceived(const QString &key, const QByteArray &data);
    void signalTileFailed(const QString &key);
    void signalTerrainTileDataReceived(const QString &key, const QByteArray &data);
    void signalTerrainTileFailed(const QString &key, const QString &error);
    void signalTerrainDatasetsReceived(const QByteArray &data);
    void signalTerrainDatasetsFailed(const QString &error);
    void signalTerrainElevationDataReceived(const QByteArray &data);
    void signalTerrainElevationFailed(const QString &error);
    void signalTilesDeleted(quint64 request_id);
    void signalTileDeletionFailed(quint64 request_id, const QString &error);
    // deleted_count: files the map server removed, -1 when it did not say.
    void signalTerrainCacheDeleted(quint64 request_id, int deleted_count);
    void signalTerrainCacheDeletionFailed(quint64 request_id, const QString &error);
    void signalMapTileUpstreamActivity(const MapUpstreamActivity &activity);
    void signalTerrainUpstreamActivity(const MapUpstreamActivity &activity);
    void signalUpstreamControlError(const QString &error);
};

#endif // INTERFACE_SERVER_MAP_H
