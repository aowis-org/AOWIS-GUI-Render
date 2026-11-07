// The map server REST client without a server: the provider catalog lists
// its built-in providers, refuses refresh() and fallback changes until the
// host configured a server and a fallback store, and the REST transport is
// constructed from the host's configuration (no request is sent).
#include "map/core/map_provider_catalog.h"
#include "services/interface_server_map_rest.h"

#include <QCoreApplication>

#include <cstdio>

namespace
{
int g_checks = 0;
int g_failures = 0;

void expectTrue(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

void testCatalogWithoutServer()
{
    MapProviderCatalog catalog;
    expectTrue(catalog.providers().size() == 4, "four built-in providers before the server answers");
    expectTrue(!catalog.serverLoaded(), "nothing loaded without a server");
    expectTrue(catalog.firstEnabledProviderId() == MapProviderCatalog::builtinId(1),
               "the first built-in provider is enabled");
    expectTrue(catalog.builtinProviderId(MapProvider(2)) == MapProviderCatalog::builtinId(2),
               "built-in provider ids are stable");

    int errors = 0;
    QObject::connect(&catalog, &MapProviderCatalog::errorOccurred,
                     [&errors](const QString &) { ++errors; });
    catalog.refresh();
    expectTrue(errors == 1, "refresh() without a server reports an error, sends nothing");

    // OpenStreetMap (max 19) may fall back to CyclOSM (max 20).
    const QString osm = MapProviderCatalog::builtinId(2);
    const QString cyclosm = MapProviderCatalog::builtinId(4);
    expectTrue(!catalog.setFallback(osm, cyclosm), "no fallback store, no fallback change");

    QString saved_provider;
    QString saved_fallback;
    catalog.setFallbackStore({}, [&saved_provider, &saved_fallback](const QString &provider_id,
                                                                   const QString &fallback_id)
    {
        saved_provider = provider_id;
        saved_fallback = fallback_id;
        return true;
    });
    expectTrue(catalog.setFallback(osm, cyclosm), "a valid fallback is stored");
    expectTrue(saved_provider == osm && saved_fallback == cyclosm, "the host store receives it");
    expectTrue(catalog.fallback(osm) == cyclosm, "the catalog uses it at once");
    expectTrue(catalog.sourceForZoom(osm, 20) == cyclosm, "zoom 20 comes from the fallback");
    expectTrue(!catalog.setFallback(cyclosm, osm), "a fallback must extend the zoom range");

    MapServerRestConfiguration server;
    expectTrue(!server.configured(), "an empty base URL is not a server");
    server.base_url = QStringLiteral("http://192.168.2.10:8080");
    catalog.setServer(server);
    expectTrue(catalog.server().configured(), "the configured server is kept");
}

void testTransportConstruction()
{
    MapServerRestConfiguration server;
    server.base_url = QStringLiteral("http://127.0.0.1:9");
    server.api_key = QByteArrayLiteral("key");
    InterfaceServerMapREST transport(server);
    InterfaceServerMap *base = &transport;
    expectTrue(base != nullptr, "the REST transport is an InterfaceServerMap");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    testCatalogWithoutServer();
    testTransportConstruction();
    std::printf("render_map_rest: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
