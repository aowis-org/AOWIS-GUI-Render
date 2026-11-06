// Terrain tile / dataset-catalog decoding for AOWIS::RenderTerrainData.
// The tiles are written here byte by byte from the documented v1 layout
// (64-byte little-endian header, dataset id, 65 x 65 uint16 samples, CRC-32
// of the payload), independent of the decoder. Checks do not depend on
// assert(), so they also run in Release (NDEBUG) builds.

#include "map/data/map_terrain_dataset.h"
#include "map/data/map_terrain_tile.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
int g_failures = 0;
int g_checks = 0;

void expectTrue(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

void expectNear(double actual, double expected, double tolerance, const char *message)
{
    ++g_checks;
    if (std::isfinite(actual) && std::abs(actual - expected) <= tolerance)
        return;
    std::fprintf(stderr, "FAIL: %s (actual=%.9f expected=%.9f)\n", message, actual, expected);
    ++g_failures;
}

void putUInt16(QByteArray &data, int offset, std::uint16_t value)
{
    data[offset] = char(value & 0xffu);
    data[offset + 1] = char((value >> 8u) & 0xffu);
}

void putUInt32(QByteArray &data, int offset, std::uint32_t value)
{
    for (int byte = 0; byte < 4; ++byte)
        data[offset + byte] = char((value >> (8u * unsigned(byte))) & 0xffu);
}

void putFloat32(QByteArray &data, int offset, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putUInt32(data, offset, bits);
}

// Standard reflected CRC-32 (polynomial 0xEDB88320), as zlib.
std::uint32_t crc32(const QByteArray &data)
{
    std::uint32_t crc = 0xffffffffu;
    for (qsizetype index = 0; index < data.size(); ++index)
    {
        crc ^= std::uint32_t(std::uint8_t(data.at(index)));
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
    }
    return ~crc;
}

// Sample code = row * 7 + column, except no-data in the first row's first
// sample; elevation = -100 m + code * 0.5 m.
QByteArray syntheticTile()
{
    const QByteArray dataset("copernicus-glo30");
    const int header_bytes = 64 + int(dataset.size());
    const int payload_bytes = MapTerrainTileSampleCount * 2;

    QByteArray payload(payload_bytes, char(0));
    for (int row = 0; row < MapTerrainTileGridSize; ++row)
    {
        for (int column = 0; column < MapTerrainTileGridSize; ++column)
        {
            const int index = row * MapTerrainTileGridSize + column;
            const std::uint16_t code = index == 0
                ? MapTerrainTileNoDataCode
                : std::uint16_t(row * 7 + column);
            putUInt16(payload, index * 2, code);
        }
    }

    QByteArray data(header_bytes, char(0));
    std::memcpy(data.data(), "AOWTRN\0\0", 8);
    putUInt16(data, 8, MapTerrainTileFormatVersion);
    putUInt16(data, 10, std::uint16_t(header_bytes));
    putUInt32(data, 12, 0u);       // flags
    data[16] = char(1);            // Web Mercator XYZ
    data[17] = char(1);            // uint16 linear
    data[18] = char(3);            // EGM2008
    putUInt16(data, 20, MapTerrainTileGridSize);
    putUInt16(data, 22, MapTerrainTileGridSize);
    data[24] = char(12);           // zoom
    putUInt32(data, 28, 2140u);    // x
    putUInt32(data, 32, 1390u);    // y
    putFloat32(data, 36, -100.0f); // minimum elevation
    putFloat32(data, 40, 0.5f);    // elevation scale
    putFloat32(data, 44, 30.0f);   // nominal resolution
    putUInt16(data, 48, std::uint16_t(dataset.size()));
    putUInt32(data, 52, std::uint32_t(MapTerrainTileSampleCount));
    putUInt32(data, 56, std::uint32_t(payload_bytes));
    putUInt32(data, 60, crc32(payload));
    std::memcpy(data.data() + 64, dataset.constData(), size_t(dataset.size()));
    data += payload;
    return data;
}

void testTileDecoding()
{
    const QByteArray data = syntheticTile();
    QString error;
    const std::optional<MapTerrainTile> tile = decodeMapTerrainTile(data, &error);
    expectTrue(tile.has_value(), "a well-formed v1 tile decodes");
    if (!tile.has_value())
    {
        std::fprintf(stderr, "  decoder: %s\n", qPrintable(error));
        return;
    }
    expectTrue(tile->address.zoom == 12 && tile->address.x == 2140u && tile->address.y == 1390u,
               "the tile address comes from the header");
    expectTrue(tile->dataset == QStringLiteral("copernicus-glo30"), "the dataset id follows the header");
    expectTrue(tile->vertical_datum == MapTerrainVerticalDatum::Egm2008, "datum code 3 is EGM2008");
    expectNear(tile->nominal_resolution_m, 30.0, 1e-6, "nominal resolution");
    expectTrue(tile->elevations_m.size() == MapTerrainTileSampleCount, "65 x 65 samples");
    expectTrue(std::isnan(tile->elevations_m.at(0)), "the no-data code decodes to NaN");
    // Row 2, column 5: code 19 -> -100 + 9.5.
    expectNear(tile->elevations_m.at(2 * MapTerrainTileGridSize + 5), -90.5, 1e-6,
               "samples are row-major, north to south, west to east");
    // Smallest finite code: row 0, column 1 (1); largest: row 64, column 64 (512).
    expectNear(tile->minimum_elevation_m, -99.5, 1e-6, "minimum ignores no-data samples");
    expectNear(tile->maximum_elevation_m, 156.0, 1e-6, "maximum of the decoded samples");

    QByteArray corrupt = data;
    corrupt[corrupt.size() - 1] = char(corrupt.at(corrupt.size() - 1) ^ 0x01);
    expectTrue(!decodeMapTerrainTile(corrupt).has_value(), "a payload CRC mismatch is rejected");
    expectTrue(!decodeMapTerrainTile(data.left(63)).has_value(), "a truncated header is rejected");
    expectTrue(!decodeMapTerrainTile(data.left(data.size() - 2)).has_value(),
               "a truncated payload is rejected");
    QByteArray other_version = data;
    putUInt16(other_version, 8, 2);
    expectTrue(!decodeMapTerrainTile(other_version).has_value(), "an unknown format version is rejected");
}

void testTileKeys()
{
    MapTerrainTileAddress address;
    address.zoom = 12;
    address.x = 2140u;
    address.y = 1390u;
    expectTrue(mapTerrainTileKey(QStringLiteral("copernicus-glo30"), address)
                   == QStringLiteral("terrain/v1/copernicus-glo30/12/2140/1390"),
               "tile cache key");
    expectTrue(mapTerrainTileEndpoint(QStringLiteral("copernicus-glo30"), address)
                   == QStringLiteral("/terrain/v1/copernicus-glo30/12/2140/1390.aowterrain"),
               "tile endpoint");
    expectTrue(!isValidMapTerrainDatasetId(QStringLiteral("Copernicus"))
                   && !isValidMapTerrainDatasetId(QStringLiteral("-glo30"))
                   && isValidMapTerrainDatasetId(QStringLiteral("glo30.v2_a")),
               "dataset ids are lower-case ASCII starting with a letter or digit");
    MapTerrainTileAddress outside = address;
    outside.x = 4096u;
    expectTrue(!isValidMapTerrainTileAddress(outside) && mapTerrainTileKey(QStringLiteral("a"), outside).isEmpty(),
               "an address outside its zoom level has no key");
}

void testDatasetCatalog()
{
    const QByteArray json = QByteArrayLiteral(
        "{\"default_dataset\":\"copernicus-glo30\",\"datasets\":["
        "{\"dataset\":\"copernicus-glo30\",\"provider\":\"copernicus\","
        "\"nominal_resolution_m\":30,\"vertical_datum\":\"egm2008\",\"surface_model\":\"dsm\","
        "\"minimum_zoom\":8,\"maximum_zoom\":14,\"tile_grid_size\":65},"
        "{\"dataset\":\"other-grid\",\"provider\":\"x\",\"nominal_resolution_m\":1,"
        "\"vertical_datum\":\"egm2008\",\"surface_model\":\"dtm\",\"minimum_zoom\":10,"
        "\"maximum_zoom\":19,\"tile_grid_size\":257}]}");
    const std::optional<MapTerrainDatasetCatalog> catalog = decodeMapTerrainDatasetCatalog(json);
    expectTrue(catalog.has_value() && catalog->datasets.size() == 1,
               "the catalog keeps the 65 x 65 dataset and drops another grid");
    if (!catalog.has_value())
        return;
    const MapTerrainDatasetInfo *info = catalog->find(QStringLiteral("copernicus-glo30"));
    expectTrue(info != nullptr && info->minimum_zoom == 8 && info->maximum_zoom == 14
                   && info->vertical_datum == MapTerrainVerticalDatum::Egm2008,
               "the catalog entry carries range and datum");
    expectTrue(!decodeMapTerrainDatasetCatalog(QByteArrayLiteral("[]")).has_value(),
               "a malformed catalog is rejected");
}
}

int main()
{
    testTileDecoding();
    testTileKeys();
    testDatasetCatalog();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d of %d terrain data checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: %d terrain data checks\n", g_checks);
    return 0;
}
