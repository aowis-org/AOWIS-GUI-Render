#ifndef MAP_TERRAIN_REPOSITORY_H
#define MAP_TERRAIN_REPOSITORY_H

#include <QCache>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#ifndef __EMSCRIPTEN__
#include <QThreadPool>
#endif

#include "map/data/map_server_transport.h"
#include "services/interface_server_map.h"
#include "map/data/map_terrain_dataset.h"
#include "map/data/map_terrain_tile.h"
#include "map/data/map_terrain_tile_source.h"

#include <QVector>

#include <optional>

// One zoom level's XYZ tile range of a terrain cache deletion, with the
// map-tile cache deletion's semantics: rows are clamped to the zoom level,
// columns wrap around the antimeridian (x may lie outside 0..2^zoom-1).
struct MapTerrainCacheDeletionRange
{
    int zoom = 0;
    int tile_x_min = 0;
    int tile_x_max = -1;
    int tile_y_min = 0;
    int tile_y_max = -1;
};

// Also the globe terrain pipeline's MapTerrainTileSource; the map surfaces
// forward its tile signals to the pipeline.
class MapTerrainRepository : public QObject, public MapTerrainTileSource
{
    Q_OBJECT

public:
    // transport: how DEM tiles and the dataset catalog reach this repository
    // (created at once, owned by the repository).
    explicit MapTerrainRepository(const MapServerTransport &transport, QObject *parent = nullptr);
    ~MapTerrainRepository() override;

    const MapTerrainTile *tile(const QString &key) const override;
    const MapTerrainTile *tile(const QString &dataset, int zoom, quint32 x, quint32 y) const override;
    void requestTile(const QString &dataset, int zoom, quint32 x, quint32 y) override;
    // The dataset the map draws: the terrain service's default dataset, as
    // described by its catalog (GET /terrain/v1/datasets). nullptr until the
    // catalog arrived, or when it does not describe its default dataset:
    // then no relief is drawn rather than guessing a provider.
    const MapTerrainDatasetInfo *activeDataset() const override;
    void requestUpstreamActivity();
    void cancelUpstreamDownloads();
    // Map Editor "Terrain Cache": deletes the cached normalized tiles of
    // every range on the terrain service and here, and with source_range
    // also the provider-native source data covering that range's area, so
    // the area's terrain is rebuilt from freshly downloaded data. One
    // signalTerrainCacheDeletionFinished() when every request answered.
    // Renderers drop the deleted tiles (signalTerrainTilesDeleted()) and
    // request them again.
    void deleteCache(const QString &dataset,
                     const QVector<MapTerrainCacheDeletionRange> &tile_ranges,
                     const std::optional<MapTerrainCacheDeletionRange> &source_range);

signals:
    void signalTerrainTileAvailable(const QString &key);
    void signalTerrainTileRetryReady(const QString &key);
    void signalUpstreamActivityChanged(const MapUpstreamActivity &activity);
    void signalUpstreamControlError(const QString &error);
    void signalTerrainTileFailed(const QString &key, const QString &error);
    // activeDataset() changed: its catalog arrived, or the map server mode
    // changed and the catalog is requested again.
    void signalTerrainDatasetChanged();
    // Cached tiles were dropped by a cache deletion: whatever was built from
    // them is rebuilt as the tiles arrive again (requestTile()).
    void signalTerrainTilesDeleted();
    // A deleteCache() batch finished. The counts are what the terrain
    // service reported (-1: not reported); error is empty on success.
    void signalTerrainCacheDeletionFinished(int deleted_tiles, int deleted_source_files,
                                            const QString &error);

private:
    struct TerrainFailure
    {
        int count = 0;
        qint64 retry_after_msecs = 0;
    };

    struct PendingTerrainRequest
    {
        QString dataset;
        MapTerrainTileAddress address;
        quint64 generation = 0;
    };

    void initServerMapInterface();
    void requestDatasetCatalog();
    void datasetCatalogReceived(const QByteArray &data);
    void datasetCatalogFailed(const QString &error);
    void terrainDataReceived(const QString &key, const QByteArray &data);
    void terrainFailed(const QString &key, const QString &error);
    void finishTerrainDecode(const QString &key, quint64 generation,
                             const std::optional<MapTerrainTile> &tile,
                             const QString &error);

    struct TerrainDeletionRequest
    {
        quint64 batch_id = 0;
        MapTerrainCacheDeletionScope scope = MapTerrainCacheDeletionScope::NormalizedTiles;
        MapTerrainCacheDeletionRange range;
    };

    struct TerrainDeletionBatch
    {
        QString dataset;
        int outstanding = 0;
        int deleted_tiles = 0;
        int deleted_source_files = 0;
        bool tile_count_known = true;
        bool source_count_known = true;
        bool source_requested = false;
        QString error;
        // With source data: the tile deletions wait for it (sent by
        // finishTerrainDeletionRequest()), so no tile is regenerated from
        // the old source data in between.
        QVector<TerrainDeletionRequest> deferred_tile_requests;
    };

    // Drops cached, failed and in-flight tiles of a range (late answers of
    // requests sent before the deletion are ignored); true when any cached
    // tile was dropped.
    bool invalidateTiles(const QString &dataset, const MapTerrainCacheDeletionRange &range);
    void terrainCacheDeleted(quint64 request_id, int deleted_count);
    void terrainCacheDeletionFailed(quint64 request_id, const QString &error);
    void finishTerrainDeletionRequest(quint64 request_id, int deleted_count, const QString &error);
    void sendTerrainDeletionRequest(const QString &dataset, const TerrainDeletionRequest &request);
    // Ends every open batch with error (map server mode changed).
    void abortTerrainDeletions(const QString &error);

    MapServerTransport transport;
    InterfaceServerMap *interface_map = nullptr;
    QSet<QString> terrain_pending;
    QHash<QString, PendingTerrainRequest> terrain_requests_pending;
    QHash<QString, TerrainFailure> terrain_failures;
    quint64 terrain_generation = 0;
    std::optional<MapTerrainDatasetInfo> active_dataset;
    int dataset_catalog_failure_count = 0;
    QHash<quint64, TerrainDeletionBatch> terrain_deletion_batches;
    QHash<quint64, TerrainDeletionRequest> terrain_deletion_requests;
    quint64 next_terrain_deletion_batch_id = 1;
    quint64 next_terrain_deletion_request_id = 1;
#ifndef __EMSCRIPTEN__
    QThreadPool terrain_decode_pool;
#endif
    QCache<QString, MapTerrainTile> cache;
};

#endif // MAP_TERRAIN_REPOSITORY_H
