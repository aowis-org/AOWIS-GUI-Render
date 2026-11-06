#include "map/data/map_terrain_tile.h"

#include <QtEndian>

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr int FixedHeaderBytes = 64;
constexpr quint32 SupportedFlags = 0;
constexpr quint8 WebMercatorXyzProjection = 1;
constexpr quint8 UInt16LinearEncoding = 1;
constexpr int PayloadBytes = MapTerrainTileSampleCount * int(sizeof(quint16));
constexpr char TerrainTileMagic[8] = {'A', 'O', 'W', 'T', 'R', 'N', '\0', '\0'};

void setError(QString *error_message, const QString &message)
{
    if (error_message != nullptr)
        *error_message = message;
}

quint16 readUInt16(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 readUInt32(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(data.constData() + offset));
}

float readFloat32(const QByteArray &data, qsizetype offset)
{
    const quint32 bits = readUInt32(data, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xffffffffu;
    for (char byte : data)
    {
        crc ^= quint32(uchar(byte));
        for (int bit = 0; bit < 8; ++bit)
        {
            const quint32 mask = quint32(0) - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

std::optional<MapTerrainVerticalDatum> verticalDatumFromCode(quint8 code)
{
    switch (code)
    {
    case 0:
        return MapTerrainVerticalDatum::Unknown;
    case 1:
        return MapTerrainVerticalDatum::Wgs84Ellipsoid;
    case 2:
        return MapTerrainVerticalDatum::Egm96;
    case 3:
        return MapTerrainVerticalDatum::Egm2008;
    case 4:
        return MapTerrainVerticalDatum::Local;
    default:
        return std::nullopt;
    }
}

bool finiteFloat(float value)
{
    return std::isfinite(double(value));
}
}

bool isValidMapTerrainDatasetId(const QString &dataset)
{
    // Lower-case ASCII letters, digits, '-', '_' and '.', so one UTF-8
    // byte per character (also no surrounding white space and nothing upper
    // case). Checked per character without temporary strings: the globe
    // builds tile keys thousands of times per frame.
    if (dataset.isEmpty() || dataset.size() > MapTerrainTileMaximumDatasetIdBytes)
        return false;

    for (const QChar character : dataset)
    {
        const char16_t value = character.unicode();
        const bool lower_letter = value >= u'a' && value <= u'z';
        const bool digit = value >= u'0' && value <= u'9';
        const bool punctuation = value == u'-' || value == u'_' || value == u'.';
        if (!lower_letter && !digit && !punctuation)
            return false;
    }

    const char16_t first = dataset.at(0).unicode();
    return (first >= u'a' && first <= u'z') || (first >= u'0' && first <= u'9');
}

bool isValidMapTerrainTileAddress(const MapTerrainTileAddress &address)
{
    if (address.zoom < 0 || address.zoom > MapTerrainTileMaximumZoom)
        return false;

    const quint32 tile_count = quint32(1) << address.zoom;
    return address.x < tile_count && address.y < tile_count;
}

QString mapTerrainTileKey(const QString &dataset, const MapTerrainTileAddress &address)
{
    if (!isValidMapTerrainDatasetId(dataset) || !isValidMapTerrainTileAddress(address))
        return QString();

    // Appended directly ("terrain/v1/<dataset>/<z>/<x>/<y>"): a chain of
    // QString::arg() parses the pattern once per argument.
    QString key;
    key.reserve(24 + dataset.size());
    key += QStringLiteral("terrain/v");
    key += QString::number(MapTerrainTileFormatVersion);
    key += QLatin1Char('/');
    key += dataset;
    key += QLatin1Char('/');
    key += QString::number(address.zoom);
    key += QLatin1Char('/');
    key += QString::number(address.x);
    key += QLatin1Char('/');
    key += QString::number(address.y);
    return key;
}

QString mapTerrainTileEndpoint(const QString &dataset, const MapTerrainTileAddress &address)
{
    const QString key = mapTerrainTileKey(dataset, address);
    if (key.isEmpty())
        return QString();

    return QStringLiteral("/%1.aowterrain").arg(key);
}

std::optional<MapTerrainTile> decodeMapTerrainTile(const QByteArray &data, QString *error_message)
{
    if (error_message != nullptr)
        error_message->clear();

    if (data.size() < FixedHeaderBytes)
    {
        setError(error_message, QStringLiteral("Terrain tile is shorter than the fixed header"));
        return std::nullopt;
    }
    if (std::memcmp(data.constData(), TerrainTileMagic, sizeof(TerrainTileMagic)) != 0)
    {
        setError(error_message, QStringLiteral("Terrain tile magic does not match"));
        return std::nullopt;
    }

    const quint16 version = readUInt16(data, 8);
    const quint16 header_bytes = readUInt16(data, 10);
    const quint32 flags = readUInt32(data, 12);
    const quint8 projection = quint8(uchar(data.at(16)));
    const quint8 encoding = quint8(uchar(data.at(17)));
    const quint8 datum_code = quint8(uchar(data.at(18)));
    const quint8 reserved_19 = quint8(uchar(data.at(19)));
    const quint16 grid_width = readUInt16(data, 20);
    const quint16 grid_height = readUInt16(data, 22);
    const int zoom = int(uchar(data.at(24)));
    const bool reserved_25_27_zero =
        data.at(25) == char(0) && data.at(26) == char(0) && data.at(27) == char(0);
    const quint32 tile_x = readUInt32(data, 28);
    const quint32 tile_y = readUInt32(data, 32);
    const float minimum_elevation_m = readFloat32(data, 36);
    const float elevation_scale_m = readFloat32(data, 40);
    const float nominal_resolution_m = readFloat32(data, 44);
    const quint16 dataset_bytes = readUInt16(data, 48);
    const quint16 reserved_50 = readUInt16(data, 50);
    const quint32 sample_count = readUInt32(data, 52);
    const quint32 payload_bytes = readUInt32(data, 56);
    const quint32 expected_crc32 = readUInt32(data, 60);

    if (version != MapTerrainTileFormatVersion)
    {
        setError(error_message,
                 QStringLiteral("Unsupported terrain tile version: %1").arg(version));
        return std::nullopt;
    }
    if (flags != SupportedFlags)
    {
        setError(error_message, QStringLiteral("Terrain tile uses unsupported flags"));
        return std::nullopt;
    }
    if (reserved_19 != 0 || !reserved_25_27_zero || reserved_50 != 0)
    {
        setError(error_message, QStringLiteral("Terrain tile reserved header fields are not zero"));
        return std::nullopt;
    }
    if (projection != WebMercatorXyzProjection || encoding != UInt16LinearEncoding)
    {
        setError(error_message,
                 QStringLiteral("Terrain tile uses an unsupported projection or sample encoding"));
        return std::nullopt;
    }
    if (grid_width != MapTerrainTileGridSize || grid_height != MapTerrainTileGridSize ||
        sample_count != MapTerrainTileSampleCount || payload_bytes != PayloadBytes)
    {
        setError(error_message, QStringLiteral("Terrain tile grid or payload dimensions are invalid"));
        return std::nullopt;
    }
    if (dataset_bytes > MapTerrainTileMaximumDatasetIdBytes ||
        header_bytes != FixedHeaderBytes + dataset_bytes || header_bytes > data.size())
    {
        setError(error_message, QStringLiteral("Terrain tile header length is invalid"));
        return std::nullopt;
    }
    if (qsizetype(header_bytes) + qsizetype(payload_bytes) != data.size())
    {
        setError(error_message, QStringLiteral("Terrain tile file length does not match its header"));
        return std::nullopt;
    }
    if (!finiteFloat(minimum_elevation_m) || !finiteFloat(elevation_scale_m) ||
        elevation_scale_m < 0.0f || !finiteFloat(nominal_resolution_m) ||
        nominal_resolution_m < 0.0f)
    {
        setError(error_message, QStringLiteral("Terrain tile elevation metadata is invalid"));
        return std::nullopt;
    }

    const std::optional<MapTerrainVerticalDatum> vertical_datum = verticalDatumFromCode(datum_code);
    if (!vertical_datum.has_value())
    {
        setError(error_message, QStringLiteral("Terrain tile vertical datum code is invalid"));
        return std::nullopt;
    }

    MapTerrainTile tile;
    tile.address.zoom = zoom;
    tile.address.x = tile_x;
    tile.address.y = tile_y;
    tile.dataset = QString::fromUtf8(data.constData() + FixedHeaderBytes, dataset_bytes);
    tile.nominal_resolution_m = double(nominal_resolution_m);
    tile.vertical_datum = vertical_datum.value();

    if (!isValidMapTerrainTileAddress(tile.address) || !isValidMapTerrainDatasetId(tile.dataset) ||
        tile.dataset.toUtf8().size() != dataset_bytes)
    {
        setError(error_message, QStringLiteral("Terrain tile address or dataset metadata is invalid"));
        return std::nullopt;
    }

    const QByteArray payload = data.mid(header_bytes, payload_bytes);
    if (crc32(payload) != expected_crc32)
    {
        setError(error_message, QStringLiteral("Terrain tile payload CRC32 check failed"));
        return std::nullopt;
    }

    tile.elevations_m.resize(MapTerrainTileSampleCount);
    bool has_finite_sample = false;
    double decoded_minimum_elevation_m = std::numeric_limits<double>::infinity();
    double decoded_maximum_elevation_m = -std::numeric_limits<double>::infinity();
    for (int index = 0; index < MapTerrainTileSampleCount; ++index)
    {
        const quint16 code = readUInt16(payload, qsizetype(index) * qsizetype(sizeof(quint16)));
        if (code == MapTerrainTileNoDataCode)
        {
            tile.elevations_m[index] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }

        const float elevation_m = minimum_elevation_m + float(code) * elevation_scale_m;
        tile.elevations_m[index] = elevation_m;
        decoded_minimum_elevation_m = qMin(
            decoded_minimum_elevation_m, double(elevation_m));
        decoded_maximum_elevation_m = qMax(
            decoded_maximum_elevation_m, double(elevation_m));
        has_finite_sample = true;
    }

    if (!has_finite_sample)
    {
        setError(error_message, QStringLiteral("Terrain tile payload contains only no-data samples"));
        return std::nullopt;
    }

    tile.minimum_elevation_m = decoded_minimum_elevation_m;
    tile.maximum_elevation_m = decoded_maximum_elevation_m;

    return tile;
}
