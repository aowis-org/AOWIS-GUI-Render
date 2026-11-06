#ifndef MAP_TERRAIN_TILE_H
#define MAP_TERRAIN_TILE_H

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

inline constexpr quint16 MapTerrainTileFormatVersion = 1;
inline constexpr int MapTerrainTileGridSize = 65;
inline constexpr int MapTerrainTileCellCount = MapTerrainTileGridSize - 1;
inline constexpr int MapTerrainTileSampleCount = MapTerrainTileGridSize * MapTerrainTileGridSize;
inline constexpr quint16 MapTerrainTileNoDataCode = 65535;
inline constexpr quint16 MapTerrainTileMaximumElevationCode = 65534;
inline constexpr int MapTerrainTileMaximumZoom = 30;
inline constexpr qsizetype MapTerrainTileMaximumDatasetIdBytes = 128;

enum class MapTerrainVerticalDatum
{
    Unknown,
    Wgs84Ellipsoid,
    Egm96,
    Egm2008,
    Local
};

struct MapTerrainTileAddress
{
    int zoom = 0;
    quint32 x = 0;
    quint32 y = 0;
};

struct MapTerrainTile
{
    MapTerrainTileAddress address;
    QString dataset;
    double nominal_resolution_m = 0.0;
    MapTerrainVerticalDatum vertical_datum = MapTerrainVerticalDatum::Unknown;
    double minimum_elevation_m = 0.0;
    double maximum_elevation_m = 0.0;

    // Row-major, north-to-south then west-to-east. NaN represents no-data.
    QVector<float> elevations_m;
};

bool isValidMapTerrainDatasetId(const QString &dataset);
bool isValidMapTerrainTileAddress(const MapTerrainTileAddress &address);
QString mapTerrainTileKey(const QString &dataset, const MapTerrainTileAddress &address);
QString mapTerrainTileEndpoint(const QString &dataset, const MapTerrainTileAddress &address);
std::optional<MapTerrainTile> decodeMapTerrainTile(const QByteArray &data,
                                                   QString *error_message = nullptr);

#endif // MAP_TERRAIN_TILE_H
