#include "geo/geo_metric_projection.h"

#include <GeographicLib/UTMUPS.hpp>
#include <GeographicLib/Geodesic.hpp>

#include <stdexcept>

GeoMetricProjection::GeoMetricProjection(QObject *parent)
    : QObject{parent}
{
    
}

void GeoMetricProjection::setOrigin(const CoordinateWGS84 &origin)
{
    this->m_originWgs84 = origin;
    this->m_originUtm = wgs84ToUtm(origin);
    this->m_hasOrigin = true;
    
    emit originChanged();
}

bool GeoMetricProjection::hasOrigin() const
{
    return this->m_hasOrigin;
}

CoordinateWGS84 GeoMetricProjection::originWgs84() const
{
    return this->m_originWgs84;
}

CoordinateUTM GeoMetricProjection::originUtm() const
{
    return this->m_originUtm;
}

CoordinateUTM GeoMetricProjection::wgs84ToUtm(const CoordinateWGS84 &coordinate)
{
    int zone = GeographicLib::UTMUPS::INVALID;
    bool northp = true;
    double easting = 0.0;
    double northing = 0.0;
    
    GeographicLib::UTMUPS::Forward(
        coordinate.latitude_deg,
        coordinate.longitude_deg,
        zone,
        northp,
        easting,
        northing
        );
    
    CoordinateUTM result;
    result.easting_m = easting;
    result.northing_m = northing;
    result.zone = zone;
    result.hemisphere_northern = northp;
    
    return result;
}

CoordinateWGS84 GeoMetricProjection::utmToWgs84(const CoordinateUTM &coordinate)
{
    double lat = 0.0;
    double lon = 0.0;
    
    GeographicLib::UTMUPS::Reverse(
        coordinate.zone,
        coordinate.hemisphere_northern,
        coordinate.easting_m,
        coordinate.northing_m,
        lat,
        lon
        );
    
    CoordinateWGS84 result;
    result.latitude_deg = lat;
    result.longitude_deg = lon;
    
    return result;
}

CoordinateLocal GeoMetricProjection::wgs84ToLocal(const CoordinateWGS84 &coordinate) const
{
    if (!this->m_hasOrigin)
    {
        throw std::logic_error("GeoMetricProjection origin has not been set.");
    }
    
    const CoordinateUTM utm = wgs84ToOriginZoneUtm(coordinate);
    
    CoordinateLocal result;
    result.x_m = utm.easting_m - this->m_originUtm.easting_m;
    result.y_m = utm.northing_m - this->m_originUtm.northing_m;
    
    return result;
}

CoordinateWGS84 GeoMetricProjection::localToWgs84(const CoordinateLocal &coordinate) const
{
    if (!this->m_hasOrigin)
    {
        throw std::logic_error("GeoMetricProjection origin has not been set.");
    }
    
    CoordinateUTM utm;
    utm.easting_m = this->m_originUtm.easting_m + coordinate.x_m;
    utm.northing_m = this->m_originUtm.northing_m + coordinate.y_m;
    utm.zone = this->m_originUtm.zone;
    utm.hemisphere_northern = this->m_originUtm.hemisphere_northern;
    
    return utmToWgs84(utm);
}

CoordinateUTM GeoMetricProjection::wgs84ToOriginZoneUtm(const CoordinateWGS84 &coordinate) const
{
    if (!this->m_hasOrigin)
    {
        throw std::logic_error("GeoMetricProjection origin has not been set.");
    }
    
    int zone = GeographicLib::UTMUPS::INVALID;
    bool northp = true;
    double easting = 0.0;
    double northing = 0.0;
    
    GeographicLib::UTMUPS::Forward(
        coordinate.latitude_deg,
        coordinate.longitude_deg,
        zone,
        northp,
        easting,
        northing,
        this->m_originUtm.zone
        );
    
    if (northp != this->m_originUtm.hemisphere_northern)
    {
        throw std::runtime_error(
            "Coordinate is in a different UTM hemisphere than the local origin."
            );
    }
    
    CoordinateUTM result;
    result.easting_m = easting;
    result.northing_m = northing;
    result.zone = zone;
    result.hemisphere_northern = northp;
    
    return result;
}

double GeoMetricProjection::distanceMeters(const CoordinateWGS84 &a,
                                           const CoordinateWGS84 &b)
{
    double distance = 0.0;
    
    GeographicLib::Geodesic::WGS84().Inverse(
        a.latitude_deg,
        a.longitude_deg,
        b.latitude_deg,
        b.longitude_deg,
        distance
        );
    
    return distance;
}

double GeoMetricProjection::azimuthDegrees(const CoordinateWGS84 &a,
                                           const CoordinateWGS84 &b)
{
    double distance = 0.0;
    double azimuth1 = 0.0;
    double azimuth2 = 0.0;
    
    GeographicLib::Geodesic::WGS84().Inverse(
        a.latitude_deg,
        a.longitude_deg,
        b.latitude_deg,
        b.longitude_deg,
        distance,
        azimuth1,
        azimuth2
        );
    
    return azimuth1;
}
