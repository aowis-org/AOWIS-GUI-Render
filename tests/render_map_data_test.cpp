// Map data repositories (AOWIS::RenderMapData) with a host-supplied fake
// transport: the terrain repository asks for the dataset catalog first and
// decodes a DEM tile, the tile repository honours the transport's request
// limits and decodes an imagery tile to RGBA8888, and neither requests
// anything without a transport. Checks do not depend on assert(), so they
// also run in Release (NDEBUG) builds.

#include "map/data/map_server_transport.h"
#include "map/data/map_terrain_repository.h"
#include "map/data/map_tile_repository.h"
#include "services/interface_server_map.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>

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

// Runs the event loop until done() or the timeout.
bool waitFor(const std::function<bool()> &done, int timeout_ms = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout_ms)
    {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return done();
}

// Records requests; answers only when the test tells it to.
class FakeTransport : public InterfaceServerMap
{
public:
    explicit FakeTransport(QObject *parent)
        : InterfaceServerMap(parent)
    {
    }

    QStringList tile_keys;
    QStringList tile_endpoints;
    QStringList terrain_keys;
    QStringList terrain_endpoints;
    int catalog_requests = 0;

    void requestTile(const QString &endpoint, const QString &key, int, int) override
    {
        this->tile_endpoints.append(endpoint);
        this->tile_keys.append(key);
    }

    void requestTerrainTile(const QString &endpoint, const QString &key) override
    {
        this->terrain_endpoints.append(endpoint);
        this->terrain_keys.append(key);
    }

    void requestTerrainDatasets() override
    {
        ++this->catalog_requests;
    }

    void deleteTiles(quint64, const QString &, int, int, int, int, int) override
    {
    }

    void requestMapTileUpstreamActivity() override
    {
    }

    void requestTerrainUpstreamActivity() override
    {
    }

    void cancelMapTileUpstreamDownloads() override
    {
    }

    void cancelTerrainUpstreamDownloads() override
    {
    }

    void answerCatalog(const QByteArray &json)
    {
        emit signalTerrainDatasetsReceived(json);
    }

    void answerTerrainTile(const QString &key, const QByteArray &data)
    {
        emit signalTerrainTileDataReceived(key, data);
    }

    void answerTile(const QString &key, const QByteArray &data)
    {
        emit signalTileDataReceived(key, data);
    }
};

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

// A flat v1 DEM tile (all samples 250 m) of copernicus-glo30 at z/x/y.
QByteArray demTile(int zoom, std::uint32_t x, std::uint32_t y)
{
    const QByteArray dataset("copernicus-glo30");
    const int header_bytes = 64 + int(dataset.size());
    const int payload_bytes = MapTerrainTileSampleCount * 2;
    QByteArray payload(payload_bytes, char(0));
    for (int index = 0; index < MapTerrainTileSampleCount; ++index)
        putUInt16(payload, index * 2, 700); // -100 m + 700 * 0.5 m

    QByteArray data(header_bytes, char(0));
    std::memcpy(data.data(), "AOWTRN\0\0", 8);
    putUInt16(data, 8, MapTerrainTileFormatVersion);
    putUInt16(data, 10, std::uint16_t(header_bytes));
    data[16] = char(1);
    data[17] = char(1);
    data[18] = char(3);
    putUInt16(data, 20, MapTerrainTileGridSize);
    putUInt16(data, 22, MapTerrainTileGridSize);
    data[24] = char(zoom);
    putUInt32(data, 28, x);
    putUInt32(data, 32, y);
    putFloat32(data, 36, -100.0f);
    putFloat32(data, 40, 0.5f);
    putFloat32(data, 44, 30.0f);
    putUInt16(data, 48, std::uint16_t(dataset.size()));
    putUInt32(data, 52, std::uint32_t(MapTerrainTileSampleCount));
    putUInt32(data, 56, std::uint32_t(payload_bytes));
    putUInt32(data, 60, crc32(payload));
    std::memcpy(data.data() + 64, dataset.constData(), size_t(dataset.size()));
    data += payload;
    return data;
}

QByteArray pngTile()
{
    QImage image(256, 256, QImage::Format_ARGB32);
    image.fill(QColor(10, 120, 200));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void testTerrainRepository()
{
    FakeTransport *fake = nullptr;
    MapServerTransport transport;
    transport.create = [&fake](QObject *parent) -> InterfaceServerMap *
    {
        fake = new FakeTransport(parent);
        return fake;
    };
    MapTerrainRepository repository(transport);
    expectTrue(fake != nullptr && fake->catalog_requests == 1,
               "the terrain repository creates its transport and asks for the catalog first");
    if (fake == nullptr)
        return;
    expectTrue(repository.activeDataset() == nullptr, "no dataset before the catalog answer");

    fake->answerCatalog(QByteArrayLiteral(
        "{\"default_dataset\":\"copernicus-glo30\",\"datasets\":[{\"dataset\":\"copernicus-glo30\","
        "\"provider\":\"copernicus\",\"nominal_resolution_m\":30,\"vertical_datum\":\"egm2008\","
        "\"surface_model\":\"dsm\",\"minimum_zoom\":8,\"maximum_zoom\":14,\"tile_grid_size\":65}]}"));
    expectTrue(repository.activeDataset() != nullptr && repository.activeDataset()->maximum_zoom == 14,
               "the catalog answer selects the default dataset");

    QString available_key;
    QObject::connect(&repository, &MapTerrainRepository::signalTerrainTileAvailable,
                     [&available_key](const QString &key) { available_key = key; });
    repository.requestTile(QStringLiteral("copernicus-glo30"), 12, 2140u, 1390u);
    expectTrue(fake->terrain_endpoints.size() == 1
                   && fake->terrain_endpoints.first()
                          == QStringLiteral("/terrain/v1/copernicus-glo30/12/2140/1390.aowterrain"),
               "a DEM request goes to the transport with the service endpoint");
    if (fake->terrain_keys.isEmpty())
        return;
    const QString key = fake->terrain_keys.first();
    fake->answerTerrainTile(key, demTile(12, 2140u, 1390u));
    expectTrue(waitFor([&available_key, &key]() { return available_key == key; }),
               "the answered tile is decoded and announced");
    const MapTerrainTile *tile = repository.tile(key);
    expectTrue(tile != nullptr && tile->elevations_m.size() == MapTerrainTileSampleCount
                   && tile->elevations_m.at(0) == 250.0f,
               "the decoded tile is cached");
}

void testTileRepository()
{
    FakeTransport *fake = nullptr;
    MapServerTransport transport;
    transport.create = [&fake](QObject *parent) -> InterfaceServerMap *
    {
        fake = new FakeTransport(parent);
        return fake;
    };
    transport.tile_requests_in_flight = 3;
    MapTileRepository repository(transport);
    if (fake == nullptr)
    {
        expectTrue(false, "the tile repository creates its transport");
        return;
    }

    const quint64 batch = repository.beginTileRequestBatch(&repository, QStringLiteral("test"));
    for (int x = 0; x < 10; ++x)
    {
        repository.requestTile(QStringLiteral("/src/5/%1/7.png").arg(x),
                               QStringLiteral("src/5/%1/7").arg(x), x, 7, x, batch);
    }
    waitFor([&fake]() { return fake->tile_keys.size() >= 3; }, 1000);
    // Give the dispatcher a few more turns: it must stay at the limit.
    waitFor([]() { return false; }, 50);
    expectTrue(fake->tile_keys.size() == 3, "the transport's in-flight limit holds");

    QString available_key;
    QObject::connect(&repository, &MapTileRepository::signalTileAvailable,
                     [&available_key](const QString &key) { available_key = key; });
    const QString key = fake->tile_keys.first();
    fake->answerTile(key, pngTile());
    expectTrue(waitFor([&available_key, &key]() { return available_key == key; }),
               "the answered imagery tile is decoded and announced");
    const QImage *image = repository.tile(key);
    expectTrue(image != nullptr && image->format() == QImage::Format_RGBA8888
                   && image->pixelColor(10, 10) == QColor(10, 120, 200),
               "the cached tile is RGBA8888 with the sent pixels");
    waitFor([&fake]() { return fake->tile_keys.size() >= 4; }, 1000);
    expectTrue(fake->tile_keys.size() == 4, "a finished request frees one in-flight slot");
}

void testWithoutTransport()
{
    MapServerTransport none;
    MapTerrainRepository terrain(none);
    terrain.requestTile(QStringLiteral("copernicus-glo30"), 12, 2140u, 1390u);
    expectTrue(terrain.activeDataset() == nullptr && terrain.tile(QStringLiteral("x")) == nullptr,
               "without a transport the terrain repository stays empty");

    MapTileRepository tiles(none);
    tiles.requestTile(QStringLiteral("/src/1/0/0.png"), QStringLiteral("src/1/0/0"), 0, 0, 0,
                      tiles.beginTileRequestBatch(&tiles, QStringLiteral("test")));
    waitFor([]() { return false; }, 20);
    expectTrue(tiles.tile(QStringLiteral("src/1/0/0")) == nullptr,
               "without a transport the tile repository requests nothing");
}
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    testTerrainRepository();
    testTileRepository();
    testWithoutTransport();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d of %d map data checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: %d map data checks\n", g_checks);
    return 0;
}
