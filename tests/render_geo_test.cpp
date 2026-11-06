// Geodesy regressions for AOWIS::RenderGeo: WGS84 / ECEF conversions,
// Web Mercator tile math and the project-local metric projection, all
// against independent closed-form references. Checks do not depend on
// assert(), so they also run in Release (NDEBUG) builds.

#include "geo/geo_metric_projection.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <cmath>
#include <cstdio>

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
    std::fprintf(stderr, "FAIL: %s (actual=%.12f expected=%.12f tolerance=%.3g)\n",
                 message, actual, expected, tolerance);
    ++g_failures;
}

constexpr double Pi = 3.14159265358979323846;

// Closed-form geodetic -> ECEF, independent of GeographicLib.
GeoWgs84Ellipsoid::EcefPositionD referenceEcef(double lon_deg, double lat_deg, double height_m)
{
    const double a = GeoWgs84Ellipsoid::EquatorialRadiusM;
    const double f = 1.0 / GeoWgs84Ellipsoid::FlatteningInverse;
    const double e2 = f * (2.0 - f);
    const double lon = lon_deg * Pi / 180.0;
    const double lat = lat_deg * Pi / 180.0;
    const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
    GeoWgs84Ellipsoid::EcefPositionD result;
    result.x = (n + height_m) * std::cos(lat) * std::cos(lon);
    result.y = (n + height_m) * std::cos(lat) * std::sin(lon);
    result.z = (n * (1.0 - e2) + height_m) * std::sin(lat);
    return result;
}

void testEcef()
{
    const double samples[][3] = {
        {0.0, 0.0, 0.0},
        {8.6821, 50.1109, 112.0},
        {-122.4194, 37.7749, -30.0},
        {168.6626, -45.0312, 2500.0},
        {179.9999, 89.5, 10.0},
    };
    for (const double *sample : samples)
    {
        const GeoWgs84Ellipsoid::EcefPositionD actual =
            GeoWgs84Ellipsoid::geodeticToEcefD(sample[0], sample[1], sample[2]);
        const GeoWgs84Ellipsoid::EcefPositionD expected =
            referenceEcef(sample[0], sample[1], sample[2]);
        expectNear(actual.x, expected.x, 1e-6, "geodeticToEcefD x matches the closed form");
        expectNear(actual.y, expected.y, 1e-6, "geodeticToEcefD y matches the closed form");
        expectNear(actual.z, expected.z, 1e-6, "geodeticToEcefD z matches the closed form");

        double lon_deg = 0.0;
        double lat_deg = 0.0;
        double height_m = 0.0;
        expectTrue(GeoWgs84Ellipsoid::ecefToGeodetic(actual, &lon_deg, &lat_deg, &height_m),
                   "ecefToGeodetic succeeds away from the centre");
        expectNear(lon_deg, sample[0], 1e-9, "double ECEF round trip keeps the longitude");
        expectNear(lat_deg, sample[1], 1e-9, "double ECEF round trip keeps the latitude");
        expectNear(height_m, sample[2], 1e-6, "double ECEF round trip keeps the height");
    }

    const GeoWgs84Ellipsoid::EcefPositionD pole = GeoWgs84Ellipsoid::geodeticToEcefD(0.0, 90.0, 0.0);
    expectNear(pole.z, GeoWgs84Ellipsoid::PolarRadiusM, 1e-6, "the pole lies at the polar radius");

    const GeoWgs84Ellipsoid::LocalFrame frame =
        GeoWgs84Ellipsoid::localFrameAtGeodetic(0.0, 0.0, 0.0);
    expectNear(frame.up.x(), 1.0, 1e-6, "up at (0, 0) points along +x");
    expectNear(frame.east.y(), 1.0, 1e-6, "east at (0, 0) points along +y");
    expectNear(frame.north.z(), 1.0, 1e-6, "north at (0, 0) points along +z");

    double lon_deg = 0.0;
    double lat_deg = 0.0;
    expectTrue(!GeoWgs84Ellipsoid::ecefToGeodetic(GeoWgs84Ellipsoid::EcefPositionD{}, &lon_deg, &lat_deg),
               "the Earth's centre has no geodetic position");
}

void testWebMercator()
{
    // Zoom 0: the whole world is one 256 px tile.
    const QPointF origin = GeoWebMercator::lonLatToWorldPixel(0.0, 0.0, 0);
    expectNear(origin.x(), 128.0, 1e-9, "(0, 0) is the centre of the zoom-0 tile (x)");
    expectNear(origin.y(), 128.0, 1e-9, "(0, 0) is the centre of the zoom-0 tile (y)");

    const QPointF frankfurt = GeoWebMercator::lonLatToWorldPixel(8.6821, 50.1109, 14);
    const CoordinateWGS84 back = GeoWebMercator::worldPixelToLonLat(frankfurt.x(), frankfurt.y(), 14);
    expectNear(back.longitude_deg, 8.6821, 1e-9, "world pixel round trip keeps the longitude");
    expectNear(back.latitude_deg, 50.1109, 1e-9, "world pixel round trip keeps the latitude");

    expectNear(GeoWebMercator::tileYToLat(0.0, 0), GeoWebMercator::MaximumLatitude, 1e-6,
               "the top tile edge is the Web Mercator latitude limit");
    expectTrue(GeoWebMercator::wrapTileX(-1, 3) == 7 && GeoWebMercator::wrapTileX(8, 3) == 0,
               "tile x wraps across the antimeridian");
    expectNear(GeoWebMercator::metersPerPixel(0.0, 0),
               2.0 * Pi * GeoWgs84Ellipsoid::EquatorialRadiusM / 256.0, 1e-6,
               "zoom-0 resolution at the equator is the circumference over one tile");
}

void testMetricProjection()
{
    // One degree of longitude on the WGS84 equator: a * pi / 180.
    CoordinateWGS84 a;
    CoordinateWGS84 b;
    b.longitude_deg = 1.0;
    expectNear(GeoMetricProjection::distanceMeters(a, b),
               GeoWgs84Ellipsoid::EquatorialRadiusM * Pi / 180.0, 1e-6,
               "geodesic distance of one equatorial degree");
    expectNear(GeoMetricProjection::azimuthDegrees(a, b), 90.0, 1e-9, "due east has azimuth 90");

    GeoMetricProjection projection;
    expectTrue(!projection.hasOrigin(), "a new projection has no origin");
    CoordinateWGS84 origin;
    origin.longitude_deg = 8.6821;
    origin.latitude_deg = 50.1109;
    projection.setOrigin(origin);
    expectTrue(projection.hasOrigin() && projection.originUtm().zone == 32,
               "Frankfurt lies in UTM zone 32");

    const CoordinateLocal at_origin = projection.wgs84ToLocal(origin);
    expectNear(at_origin.x_m, 0.0, 1e-6, "the origin is local (0, 0) (x)");
    expectNear(at_origin.y_m, 0.0, 1e-6, "the origin is local (0, 0) (y)");

    CoordinateLocal offset;
    offset.x_m = 1250.0;
    offset.y_m = -830.0;
    const CoordinateWGS84 moved = projection.localToWgs84(offset);
    const CoordinateLocal again = projection.wgs84ToLocal(moved);
    expectNear(again.x_m, offset.x_m, 1e-6, "local round trip keeps x");
    expectNear(again.y_m, offset.y_m, 1e-6, "local round trip keeps y");
}
}

int main()
{
    testEcef();
    testWebMercator();
    testMetricProjection();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d of %d geo checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: %d geo checks\n", g_checks);
    return 0;
}
