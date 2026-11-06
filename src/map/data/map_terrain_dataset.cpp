#include "map/data/map_terrain_dataset.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace
{
void setError(QString *error_message, const QString &message)
{
    if (error_message != nullptr)
        *error_message = message;
}

// QJsonValue::toInt() also accepts 8.5 (truncated): require an integer.
std::optional<int> integerValue(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number)
        return std::nullopt;
    return int(number);
}
}

const MapTerrainDatasetInfo *MapTerrainDatasetCatalog::find(const QString &dataset) const
{
    for (const MapTerrainDatasetInfo &info : this->datasets)
    {
        if (info.dataset == dataset)
            return &info;
    }
    return nullptr;
}

std::optional<MapTerrainVerticalDatum> mapTerrainVerticalDatumFromId(const QString &id)
{
    if (id == QStringLiteral("unknown"))
        return MapTerrainVerticalDatum::Unknown;
    if (id == QStringLiteral("wgs84-ellipsoid"))
        return MapTerrainVerticalDatum::Wgs84Ellipsoid;
    if (id == QStringLiteral("egm96"))
        return MapTerrainVerticalDatum::Egm96;
    if (id == QStringLiteral("egm2008"))
        return MapTerrainVerticalDatum::Egm2008;
    if (id == QStringLiteral("local"))
        return MapTerrainVerticalDatum::Local;
    return std::nullopt;
}

std::optional<MapTerrainDatasetCatalog> decodeMapTerrainDatasetCatalog(
    const QByteArray &json, QString *error_message)
{
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        setError(error_message, QStringLiteral("Terrain dataset catalog is not a JSON object"));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    MapTerrainDatasetCatalog catalog;
    catalog.default_dataset = root.value(QStringLiteral("default_dataset")).toString();
    if (!isValidMapTerrainDatasetId(catalog.default_dataset))
    {
        setError(error_message,
                 QStringLiteral("Terrain dataset catalog has an invalid default dataset: %1")
                     .arg(catalog.default_dataset));
        return std::nullopt;
    }

    const QJsonValue entries_value = root.value(QStringLiteral("datasets"));
    if (!entries_value.isArray())
    {
        setError(error_message, QStringLiteral("Terrain dataset catalog has no dataset list"));
        return std::nullopt;
    }

    for (const QJsonValue &entry_value : entries_value.toArray())
    {
        const QJsonObject entry = entry_value.toObject();
        MapTerrainDatasetInfo info;
        info.dataset = entry.value(QStringLiteral("dataset")).toString();
        info.provider = entry.value(QStringLiteral("provider")).toString();
        info.nominal_resolution_m =
            entry.value(QStringLiteral("nominal_resolution_m")).toDouble(-1.0);
        info.surface_model = entry.value(QStringLiteral("surface_model")).toString();
        const std::optional<MapTerrainVerticalDatum> datum = mapTerrainVerticalDatumFromId(
            entry.value(QStringLiteral("vertical_datum")).toString());
        const std::optional<int> minimum_zoom =
            integerValue(entry.value(QStringLiteral("minimum_zoom")));
        const std::optional<int> maximum_zoom =
            integerValue(entry.value(QStringLiteral("maximum_zoom")));
        const std::optional<int> grid_size =
            integerValue(entry.value(QStringLiteral("tile_grid_size")));

        // The renderer reads only v1 65x65 tiles; another grid needs another
        // decoder, so such a dataset is skipped rather than misread.
        if (!isValidMapTerrainDatasetId(info.dataset) || !datum.has_value()
            || !std::isfinite(info.nominal_resolution_m) || info.nominal_resolution_m < 0.0
            || !minimum_zoom.has_value() || !maximum_zoom.has_value()
            || minimum_zoom.value() < 0 || maximum_zoom.value() > MapTerrainTileMaximumZoom
            || minimum_zoom.value() > maximum_zoom.value()
            || grid_size.value_or(-1) != MapTerrainTileGridSize)
        {
            setError(error_message,
                     QStringLiteral("Ignoring invalid terrain dataset catalog entry: %1")
                         .arg(info.dataset));
            continue;
        }

        info.vertical_datum = datum.value();
        info.minimum_zoom = minimum_zoom.value();
        info.maximum_zoom = maximum_zoom.value();
        catalog.datasets.append(info);
    }

    return catalog;
}
