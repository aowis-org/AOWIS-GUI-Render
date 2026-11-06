#ifndef MAP_TERRAIN_DATASET_H
#define MAP_TERRAIN_DATASET_H

#include "map/data/map_terrain_tile.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

// One terrain dataset as described by the terrain service
// (GET /terrain/v1/datasets, Aowis::Map::TerrainDatasetInfo).
struct MapTerrainDatasetInfo
{
    QString dataset;
    QString provider;
    double nominal_resolution_m = 0.0;
    MapTerrainVerticalDatum vertical_datum = MapTerrainVerticalDatum::Unknown;
    // "dsm", "dtm" or "unknown".
    QString surface_model;
    // Normalized zoom range the provider generates; maximum_zoom is its
    // native detail (the renderer never requests finer terrain tiles).
    int minimum_zoom = 0;
    int maximum_zoom = 0;
};

struct MapTerrainDatasetCatalog
{
    QString default_dataset;
    QVector<MapTerrainDatasetInfo> datasets;

    const MapTerrainDatasetInfo *find(const QString &dataset) const;
};

std::optional<MapTerrainVerticalDatum> mapTerrainVerticalDatumFromId(const QString &id);

// Parses and validates the catalog document. Invalid entries are dropped
// (with a reason in error_message); a malformed document or an invalid
// default dataset identifier fails entirely.
std::optional<MapTerrainDatasetCatalog> decodeMapTerrainDatasetCatalog(
    const QByteArray &json, QString *error_message = nullptr);

#endif // MAP_TERRAIN_DATASET_H
