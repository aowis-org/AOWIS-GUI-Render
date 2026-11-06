#ifndef GEO_WEB_MERCATOR_H
#define GEO_WEB_MERCATOR_H

#include <QPointF>

#include <aowis/model/gis.h>

class GeoWebMercator
{
public:
    static constexpr int TileSize = 256;
    static constexpr double MaximumLatitude = 85.05112878;

    GeoWebMercator() = delete;

    static double normalizeLongitude(double lon);
    static int wrapTileX(int x, int zoom);
    static double nearestWrappedTileX(double x, double referenceX, int zoom);
    static double nearestWrappedWorldPixelX(double x, double referenceX, int zoom);
    static double zoomScale(int zoom, int referenceZoom);
    static double metersPerPixel(double latitude, int zoom);

    static double lonToTileX(double lon, int zoom);
    static double latToTileY(double lat, int zoom);
    static double tileXToLon(double x, int zoom);
    static double tileYToLat(double y, int zoom);

    static QPointF lonLatToWorldPixel(double lon, double lat, int zoom);
    static CoordinateWGS84 worldPixelToLonLat(double pixelX, double pixelY, int zoom);
};

#endif // GEO_WEB_MERCATOR_H
