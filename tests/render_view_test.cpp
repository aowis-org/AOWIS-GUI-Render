// MapModel (AOWIS::RenderView) without a GUI: camera round trips, the
// injected imagery provider directory and orbit sensitivity. Checks do not
// depend on assert(), so they also run in Release (NDEBUG) builds.

#include "map/core/map_imagery_provider_directory.h"
#include "map/core/map_model.h"

#include <cmath>
#include <cstdio>
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

void expectNear(double actual, double expected, double tolerance, const char *message)
{
    ++g_checks;
    if (std::isfinite(actual) && std::abs(actual - expected) <= tolerance)
        return;
    std::fprintf(stderr, "FAIL: %s (actual=%.12f expected=%.12f)\n", message, actual, expected);
    ++g_failures;
}

// Two providers: "builtin-osm" (built-in OpenStreetMap, z0-19) and
// "custom-a" (z3-15, falls back to "builtin-osm" above 15).
class FakeDirectory : public MapImageryProviderDirectory
{
public:
    bool custom_enabled = true;

    bool isProviderEnabled(const QString &provider_id) const override
    {
        return provider_id == QStringLiteral("builtin-osm")
            || (provider_id == QStringLiteral("custom-a") && this->custom_enabled);
    }

    QString firstEnabledProviderId() const override
    {
        return QStringLiteral("builtin-osm");
    }

    QString sourceForZoom(const QString &provider_id, int zoom) const override
    {
        if (provider_id == QStringLiteral("custom-a"))
            return zoom <= 15 ? QStringLiteral("custom-a") : QStringLiteral("builtin-osm");
        if (provider_id == QStringLiteral("builtin-osm"))
            return QStringLiteral("builtin-osm");
        return QString();
    }

    int maximumSourceZoom(const QString &provider_id) const override
    {
        return isProviderEnabled(provider_id) ? 19 : 0;
    }

    bool sourceZoomRange(const QString &source, int *minimum_zoom, int *maximum_zoom) const override
    {
        if (source == QStringLiteral("custom-a"))
        {
            *minimum_zoom = 3;
            *maximum_zoom = 15;
            return true;
        }
        if (source == QStringLiteral("builtin-osm"))
        {
            *minimum_zoom = 0;
            *maximum_zoom = 19;
            return true;
        }
        return false;
    }

    QString builtinProviderId(MapProvider provider) const override
    {
        return provider == MapProvider::OpenStreetMap ? QStringLiteral("builtin-osm") : QString();
    }
};

void testCameraRoundTrip()
{
    MapModel model;
    const QSize viewport(1280, 720);
    model.setView2dContinuousZoom(14.25, viewport);
    model.setCenter(8.6821, 50.1109, viewport);
    expectNear(model.view2dContinuousZoom(), 14.25, 1e-12, "continuous zoom is kept");
    expectNear(model.centerLon(), 8.6821, 1e-9, "centre longitude");
    expectNear(model.centerLat(), 50.1109, 1e-9, "centre latitude");

    const CoordinateWGS84 centre = model.wgs84FromScreen(QPoint(640, 360), viewport);
    expectNear(centre.longitude_deg, 8.6821, 1e-9, "the viewport centre is the map centre (lon)");
    expectNear(centre.latitude_deg, 50.1109, 1e-9, "the viewport centre is the map centre (lat)");

    CoordinateWGS84 point;
    point.longitude_deg = 8.70;
    point.latitude_deg = 50.10;
    const QPointF screen = model.screenFromWgs84(point, viewport);
    const CoordinateWGS84 back = model.wgs84FromScreen(
        QPoint(int(std::lround(screen.x())), int(std::lround(screen.y()))), viewport);
    // One pixel at zoom 14.25 and 50 deg N is about 4 m, i.e. < 1e-4 deg.
    expectNear(back.longitude_deg, point.longitude_deg, 1e-4, "screen round trip (lon)");
    expectNear(back.latitude_deg, point.latitude_deg, 1e-4, "screen round trip (lat)");

    expectTrue(model.cameraViewState().mode == MapViewMode::TwoD, "a new model starts in 2D");
}

void testWithoutDirectory()
{
    MapModel model;
    expectTrue(model.tileSourcePath(10).isEmpty(), "no directory: no imagery source");
    const QString before = model.providerId();
    model.setProviderId(QStringLiteral("builtin-osm"));
    expectTrue(model.providerId() == before, "no directory: provider selection is refused");
    expectTrue(model.imageryMaximumZoom() == MapModel::MaxZoom, "no directory: no zoom limit");
}

void testProviderDirectory()
{
    FakeDirectory directory;
    MapModel model(&directory);

    model.setProvider(MapProvider::OpenStreetMap);
    expectTrue(model.providerId() == QStringLiteral("builtin-osm")
                   && model.provider() == MapProvider::OpenStreetMap,
               "a built-in provider selects its id and enum");

    model.setProviderId(QStringLiteral("custom-a"));
    expectTrue(model.providerId() == QStringLiteral("custom-a") && model.provider() == MapProvider::Custom,
               "an enabled custom provider is selected as Custom");
    expectTrue(model.tileSourcePath(12) == QStringLiteral("custom-a")
                   && model.tileSourcePath(17) == QStringLiteral("builtin-osm"),
               "sources follow the fallback chain per zoom");
    expectTrue(model.tileCacheKey(-1, 5, 3) == QStringLiteral("custom-a/3/7/5"),
               "cache key: source/zoom/x/y with x wrapped");
    expectTrue(model.tileEndpoint(2, 5, 17) == QStringLiteral("/builtin-osm/17/2/5.png"),
               "endpoint uses the fallback source");
    expectTrue(model.imageryTileZoom(1) == 3 && model.imageryTileZoom(12) == 12,
               "imagery level is clamped to the source's zoom range");

    model.setProviderId(QStringLiteral("unknown"));
    expectTrue(model.providerId() == QStringLiteral("custom-a"), "an unknown provider is refused");

    directory.custom_enabled = false;
    model.providerCatalogChanged();
    expectTrue(model.providerId() == QStringLiteral("builtin-osm"),
               "a disabled selection falls back to the first enabled provider");
}

void testOrbitSensitivity()
{
    MapModel model;
    model.setViewGlobeYawDeg(0.0);
    model.orbitViewGlobeByPointerDelta(QPoint(10, 0), false);
    expectNear(model.viewGlobeYawDeg(), 10.0 * MapModel::ViewGlobeOrbitYawDegreesPerPixel, 1e-9,
               "default orbit sensitivity is 1");

    model.setViewGlobeYawDeg(0.0);
    double sensitivity = 2.0;
    const std::function<double()> source = [&sensitivity]() { return sensitivity; };
    model.setOrbitSensitivitySource(source);
    model.orbitViewGlobeByPointerDelta(QPoint(10, 0), false);
    expectNear(model.viewGlobeYawDeg(), 20.0 * MapModel::ViewGlobeOrbitYawDegreesPerPixel, 1e-9,
               "the injected sensitivity scales the orbit");

    model.setViewGlobeYawDeg(0.0);
    sensitivity = 0.5;
    model.orbitViewGlobeByPointerDelta(QPoint(10, 0), false);
    expectNear(model.viewGlobeYawDeg(), 5.0 * MapModel::ViewGlobeOrbitYawDegreesPerPixel, 1e-9,
               "a changed setting applies at the next drag");
}
}

// Free look turns from straight down to straight up; an orbit keeps its
// 0..90 degree tilt lock.
void testGlobeFreeLook()
{
    MapModel model;
    model.setViewMode(MapViewMode::Globe, QSize(800, 600));
    model.setViewGlobeDistanceM(5000.0);
    model.setViewGlobePitchDeg(30.0);
    expectTrue(model.viewGlobeCameraMode() == MapGlobeCameraMode::Orbit, "the globe starts as an orbit");
    model.setViewGlobePitchDeg(-20.0);
    expectNear(model.viewGlobePitchDeg(), MapModel::MinViewGlobePitchDeg, 1e-9,
               "an orbit cannot tilt above its target's horizon");

    model.setViewGlobePitchDeg(30.0);
    model.setViewGlobeLookDepressionDeg(-60.0);
    expectTrue(model.viewGlobeCameraMode() == MapGlobeCameraMode::FreeLook, "looking switches to free look");
    expectNear(model.viewGlobeLookDepressionDeg(), -60.0, 1e-4, "free look looks 60 degrees above the horizon");
    expectTrue(model.viewGlobePitchDeg() < 0.0, "looking up puts the target above the eye");

    model.setViewGlobeLookDepressionDeg(-120.0);
    expectTrue(model.viewGlobeLookDepressionDeg() < -89.0
                   && model.viewGlobeLookDepressionDeg() >= -MapModel::MaxViewGlobeLookAngleDeg - 1e-4,
               "straight up is free look's limit");
    model.setViewGlobeLookDepressionDeg(120.0);
    expectTrue(model.viewGlobeLookDepressionDeg() > 89.0
                   && model.viewGlobeLookDepressionDeg() <= MapModel::MaxViewGlobeLookAngleDeg + 1e-4,
               "straight down is free look's other limit");

    model.setViewGlobeLookDepressionDeg(-45.0);
    model.setViewGlobeCameraMode(MapGlobeCameraMode::Orbit);
    expectTrue(model.viewGlobePitchDeg() >= MapModel::MinViewGlobePitchDeg,
               "returning to the orbit restores its tilt lock");

    model.setViewGlobeLookDepressionDeg(-30.0);
    model.setViewMode(MapViewMode::TwoD, QSize(800, 600));
    expectTrue(model.viewGlobeCameraMode() == MapGlobeCameraMode::Orbit, "a view mode switch ends free look");
}

int main()
{
    testCameraRoundTrip();
    testWithoutDirectory();
    testProviderDirectory();
    testOrbitSensitivity();
    testGlobeFreeLook();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d of %d view checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: %d view checks\n", g_checks);
    return 0;
}
