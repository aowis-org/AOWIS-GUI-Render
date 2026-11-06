#include "geo/geo_web_mercator.h"

#include <algorithm>
#include <cmath>

#include <QtMath>

namespace
{
constexpr double WebMercatorEarthRadiusMeters = 6378137.0;
}

double GeoWebMercator::normalizeLongitude(double lon)
{
    double normalized = std::fmod(lon + 180.0, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;

    return normalized - 180.0;
}

int GeoWebMercator::wrapTileX(int x, int zoom)
{
    const int tile_count = 1 << zoom;
    int wrapped_x = x % tile_count;
    if (wrapped_x < 0)
        wrapped_x += tile_count;

    return wrapped_x;
}

double GeoWebMercator::nearestWrappedTileX(double x, double referenceX, int zoom)
{
    const double tile_count = double(1 << zoom);
    return x + std::round((referenceX - x) / tile_count) * tile_count;
}

double GeoWebMercator::nearestWrappedWorldPixelX(
    double x, double referenceX, int zoom)
{
    const double tile_x = x / TileSize;
    const double reference_tile_x = referenceX / TileSize;
    return nearestWrappedTileX(tile_x, reference_tile_x, zoom) * TileSize;
}

double GeoWebMercator::zoomScale(int zoom, int referenceZoom)
{
    return std::ldexp(1.0, zoom - referenceZoom);
}

double GeoWebMercator::metersPerPixel(double latitude, int zoom)
{
    const double latitude_clamped = std::clamp(
        latitude, -MaximumLatitude, MaximumLatitude);
    const double circumference = 2.0 * M_PI * WebMercatorEarthRadiusMeters;
    return circumference * std::cos(qDegreesToRadians(latitude_clamped)) /
        (TileSize * zoomScale(zoom, 0));
}

double GeoWebMercator::lonToTileX(double lon, int zoom)
{
    return (lon + 180.0) / 360.0 * double(1 << zoom);
}

double GeoWebMercator::latToTileY(double lat, int zoom)
{
    const double lat_clamped = std::clamp(lat, -MaximumLatitude, MaximumLatitude);
    const double rad = qDegreesToRadians(lat_clamped);

    return (1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / M_PI)
           / 2.0 * double(1 << zoom);
}

double GeoWebMercator::tileXToLon(double x, int zoom)
{
    return x / double(1 << zoom) * 360.0 - 180.0;
}

double GeoWebMercator::tileYToLat(double y, int zoom)
{
    const double n = M_PI - 2.0 * M_PI * y / double(1 << zoom);
    return qRadiansToDegrees(std::atan(std::sinh(n)));
}

QPointF GeoWebMercator::lonLatToWorldPixel(double lon, double lat, int zoom)
{
    return QPointF(
        lonToTileX(lon, zoom) * TileSize,
        latToTileY(lat, zoom) * TileSize
    );
}

CoordinateWGS84 GeoWebMercator::worldPixelToLonLat(double pixelX, double pixelY, int zoom)
{
    CoordinateWGS84 wgs;
    wgs.latitude_deg = tileYToLat(pixelY / TileSize, zoom);
    wgs.longitude_deg = normalizeLongitude(tileXToLon(pixelX / TileSize, zoom));
    return wgs;
}
