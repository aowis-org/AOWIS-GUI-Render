#include "map/core/map_provider_catalog.h"
#include "map/core/map_provider_fallback_resolution.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

namespace
{
const QString ArcGisId = QStringLiteral("a0d790c2-f231-55fd-b95e-23c9bd6cb3fc");
const QString OsmId = QStringLiteral("0381bc44-67e9-5af9-aeb0-023ccba36658");
const QString TopoId = QStringLiteral("178a60e6-178a-507b-af86-f45427150986");
const QString CycloId = QStringLiteral("49bda1b9-15aa-55e6-8933-2172c785e775");

QNetworkRequest providerRequest(const MapServerRestConfiguration &server)
{
    QString base = server.base_url.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QNetworkRequest request(QUrl(base + QStringLiteral("/map/v1/providers")));
    request.setTransferTimeout(20000);
    if (!server.api_key.isEmpty())
        request.setRawHeader("X-API-Key", server.api_key);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return request;
}
}

MapProviderCatalog::MapProviderCatalog(QObject *parent) : QObject(parent)
{
    this->entries = {
        {ArcGisId, QStringLiteral("ArcGIS World Imagery"), {}, 0, 17},
        {OsmId, QStringLiteral("OpenStreetMap"), {}, 0, 19},
        {TopoId, QStringLiteral("OpenTopoMap"), {}, 0, 17},
        {CycloId, QStringLiteral("CyclOSM"), {}, 0, 20}
    };
}

void MapProviderCatalog::setServer(const MapServerRestConfiguration &server)
{
    this->map_server = server;
}

void MapProviderCatalog::setFallbackStore(const QHash<QString, QString> &fallbacks,
                                          FallbackSave save)
{
    this->fallback_cache = fallbacks;
    this->fallback_save = std::move(save);
    emit fallbackChanged();
}

const QVector<MapProviderEntry> &MapProviderCatalog::providers() const
{
    return this->entries;
}

bool MapProviderCatalog::serverLoaded() const
{
    return this->server_loaded;
}

const MapProviderEntry *MapProviderCatalog::provider(const QString &id) const
{
    for (const MapProviderEntry &entry : this->entries)
        if (entry.id == id) return &entry;
    return nullptr;
}

QString MapProviderCatalog::builtinId(int provider_enum)
{
    switch (provider_enum)
    {
    case 1: return ArcGisId;
    case 2: return OsmId;
    case 3: return TopoId;
    case 4: return CycloId;
    default: return {};
    }
}

bool MapProviderCatalog::isProviderEnabled(const QString &provider_id) const
{
    const MapProviderEntry *entry = provider(provider_id);
    return entry != nullptr && entry->enabled;
}

QString MapProviderCatalog::firstEnabledProviderId() const
{
    for (const MapProviderEntry &candidate : this->entries)
    {
        if (candidate.enabled)
            return candidate.id;
    }
    return QString();
}

bool MapProviderCatalog::sourceZoomRange(const QString &source, int *minimum_zoom,
                                         int *maximum_zoom) const
{
    const MapProviderEntry *entry = provider(source);
    if (entry == nullptr || minimum_zoom == nullptr || maximum_zoom == nullptr)
        return false;
    *minimum_zoom = entry->minimum_zoom;
    *maximum_zoom = entry->maximum_zoom;
    return true;
}

QString MapProviderCatalog::builtinProviderId(MapProvider provider) const
{
    return builtinId(int(provider));
}

QString MapProviderCatalog::fallback(const QString &provider_id) const
{
    // Called on every map frame: never touch disk here.
    return this->fallback_cache.value(provider_id);
}

bool MapProviderCatalog::setFallback(const QString &provider_id, const QString &fallback_id)
{
    const MapProviderEntry *source = provider(provider_id);
    const MapProviderEntry *target = provider(fallback_id);
    if (source == nullptr || (!fallback_id.isEmpty() &&
        (target == nullptr || !target->enabled || fallback_id == provider_id ||
         target->maximum_zoom <= source->maximum_zoom ||
         target->minimum_zoom > source->maximum_zoom + 1)))
        return false;
    if (!this->fallback_save)
        return false;
    if (this->fallback_cache.value(provider_id) == fallback_id)
        return true;
    if (!this->fallback_save(provider_id, fallback_id))
        return false;
    if (fallback_id.isEmpty())
        this->fallback_cache.remove(provider_id);
    else
        this->fallback_cache.insert(provider_id, fallback_id);
    emit fallbackChanged();
    return true;
}

QString MapProviderCatalog::sourceForZoom(const QString &selected_id, int zoom) const
{
    return MapProviderFallbackResolution::sourceForZoom(
        this->entries, this->fallback_cache, selected_id, zoom);
}

int MapProviderCatalog::maximumSourceZoom(const QString &selected_id) const
{
    return MapProviderFallbackResolution::maximumSourceZoom(
        this->entries, this->fallback_cache, selected_id);
}

void MapProviderCatalog::refresh()
{
    if (!this->map_server.configured())
    {
        emit errorOccurred(QStringLiteral("No map server configured"));
        return;
    }
    QNetworkReply *reply = this->network_manager.get(providerRequest(this->map_server));
    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred(reply->errorString());
            reply->deleteLater();
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonArray array = document.object().value(QStringLiteral("providers")).toArray();
        if (!document.isObject() || !document.object().value(QStringLiteral("providers")).isArray())
        {
            emit errorOccurred(QStringLiteral("Invalid provider catalog response"));
            reply->deleteLater();
            return;
        }
        QVector<MapProviderEntry> providers;
        for (const QJsonValue &value : array)
        {
            const QJsonObject object = value.toObject();
            MapProviderEntry entry;
            entry.id = object.value(QStringLiteral("id")).toString();
            entry.name = object.value(QStringLiteral("name")).toString();
            entry.url = object.value(QStringLiteral("url")).toString();
            entry.minimum_zoom = object.value(QStringLiteral("minimum_zoom")).toInt();
            entry.maximum_zoom = object.value(QStringLiteral("maximum_zoom")).toInt(19);
            entry.enabled = object.value(QStringLiteral("enabled")).toBool(true);
            entry.remote_fetch_enabled = object.value(QStringLiteral("remote_fetch_enabled")).toBool(true);
            entry.coverage = object.value(QStringLiteral("coverage")).toString();
            if (!entry.id.isEmpty()) providers.append(entry);
        }
        this->entries = providers;
        this->server_loaded = true;
        emit catalogChanged();
        reply->deleteLater();
    });
}

void MapProviderCatalog::saveProviders(const QVector<MapProviderEntry> &providers)
{
    QJsonArray array;
    for (const MapProviderEntry &entry : providers)
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), entry.id);
        object.insert(QStringLiteral("name"), entry.name);
        object.insert(QStringLiteral("url"), entry.url);
        object.insert(QStringLiteral("minimum_zoom"), entry.minimum_zoom);
        object.insert(QStringLiteral("maximum_zoom"), entry.maximum_zoom);
        object.insert(QStringLiteral("enabled"), entry.enabled);
        object.insert(QStringLiteral("remote_fetch_enabled"), entry.remote_fetch_enabled);
        object.insert(QStringLiteral("coverage"), entry.coverage);
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("providers"), array);
    QNetworkReply *reply = this->network_manager.put(providerRequest(this->map_server),
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        const QByteArray response = reply->readAll();
        if (reply->error() == QNetworkReply::NoError)
        {
            emit providersSaved();
            refresh();
        }
        else
            emit errorOccurred(QStringLiteral("Saving providers failed: %1 (%2)")
                .arg(reply->errorString(), QString::fromUtf8(response.left(400))));
        reply->deleteLater();
    });
}
