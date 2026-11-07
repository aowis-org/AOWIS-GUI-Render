#ifndef INTERFACE_SERVER_MAP_REST_H
#define INTERFACE_SERVER_MAP_REST_H

#include <QHash>
#include <QSet>

#include "services/interface_server_map.h"
#include "services/map_server_rest_configuration.h"
#include "services/rest_client.h"

// The map/terrain transport over aowis-server-map's REST API
// (/map/..., /terrain/v1/..., /upstream/v1/...).
class InterfaceServerMapREST : public InterfaceServerMap
{
    Q_OBJECT

public:
    explicit InterfaceServerMapREST(const MapServerRestConfiguration &server,
                                    QObject *parent = nullptr);

    void requestTile(const QString &endpoint, const QString &key, int x, int y) override;
    void requestTerrainTile(const QString &endpoint, const QString &key) override;
    void requestTerrainDatasets() override;
    void requestTerrainElevation(const QString &endpoint) override;
    void deleteTiles(quint64 request_id, const QString &provider, int zoom,
                     int tile_x_min, int tile_x_max, int tile_y_min, int tile_y_max) override;
    void deleteTerrainCache(quint64 request_id, MapTerrainCacheDeletionScope scope,
                            const QString &dataset, int zoom,
                            int tile_x_min, int tile_x_max,
                            int tile_y_min, int tile_y_max) override;
    void requestMapTileUpstreamActivity() override;
    void requestTerrainUpstreamActivity() override;
    void cancelMapTileUpstreamDownloads() override;
    void cancelTerrainUpstreamDownloads() override;

private:
    void initRestConnection(const MapServerRestConfiguration &server);

    RESTClient *rest = nullptr;
    QSet<QString> rest_pending;
    QSet<QString> terrain_pending;
    bool terrain_elevation_pending = false;
    bool terrain_datasets_pending = false;
    bool map_upstream_activity_pending = false;
    bool terrain_upstream_activity_pending = false;
    // Terrain cache DELETEs share RESTClient's delete path with the map-tile
    // ones: their REST ids live in the upper half of the id space (map-tile
    // ids count up from 1), mapped back to the caller's request ids.
    QHash<quint64, quint64> terrain_deletion_request_ids;
    quint64 next_terrain_deletion_rest_id = quint64(1) << 63;
};

#endif // INTERFACE_SERVER_MAP_REST_H
