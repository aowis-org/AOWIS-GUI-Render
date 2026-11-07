#ifndef MAP_PROVIDER_CATALOG_H
#define MAP_PROVIDER_CATALOG_H

#include "map/core/map_imagery_provider_directory.h"
#include "services/map_server_rest_configuration.h"

#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QString>
#include <QVector>

#include <functional>

struct MapProviderEntry
{
    QString id;
    QString name;
    QString url;
    int minimum_zoom = 0;
    int maximum_zoom = 19;
    bool enabled = true;
    bool remote_fetch_enabled = true;
    QString coverage;
};

// The map server owns provider definitions (GET/PUT /map/v1/providers); the
// host stores each user's fallback choices. It is also the MapModel's view of
// the providers: the host passes it to the model and forwards
// catalogChanged() / fallbackChanged() to MapModel::providerCatalogChanged() /
// providerFallbackChanged(). Until refresh() succeeds the four built-in
// providers are listed.
class MapProviderCatalog : public QObject, public MapImageryProviderDirectory
{
    Q_OBJECT
public:
    explicit MapProviderCatalog(QObject *parent = nullptr);
    // The map server this catalog mirrors; used by the next refresh() /
    // saveProviders().
    void setServer(const MapServerRestConfiguration &server);
    const MapServerRestConfiguration &server() const { return this->map_server; }
    // The user's saved fallback choices and how a changed one is stored
    // (provider id, fallback id; empty fallback id: none). Without a store
    // setFallback() refuses changes.
    using FallbackSave = std::function<bool(const QString &provider_id, const QString &fallback_id)>;
    void setFallbackStore(const QHash<QString, QString> &fallbacks, FallbackSave save);
    const QVector<MapProviderEntry> &providers() const;
    bool serverLoaded() const;
    const MapProviderEntry *provider(const QString &id) const;
    QString fallback(const QString &provider_id) const;
    bool setFallback(const QString &provider_id, const QString &fallback_id);
    QString sourceForZoom(const QString &selected_id, int zoom) const override;
    int maximumSourceZoom(const QString &selected_id) const override;
    bool isProviderEnabled(const QString &provider_id) const override;
    QString firstEnabledProviderId() const override;
    bool sourceZoomRange(const QString &source, int *minimum_zoom,
                         int *maximum_zoom) const override;
    QString builtinProviderId(MapProvider provider) const override;
    void refresh();
    void saveProviders(const QVector<MapProviderEntry> &entries);
    static QString builtinId(int provider_enum);
signals:
    void catalogChanged();
    void fallbackChanged();
    void errorOccurred(const QString &message);
    void providersSaved();
private:
    QVector<MapProviderEntry> entries;
    QHash<QString, QString> fallback_cache;
    bool server_loaded = false;
    MapServerRestConfiguration map_server;
    FallbackSave fallback_save;
    QNetworkAccessManager network_manager;
};

#endif
