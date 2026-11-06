#ifndef GEO_METRIC_PROJECTION_H
#define GEO_METRIC_PROJECTION_H

#include <QObject>

#include <aowis/model/gis.h>

class GeoMetricProjection : public QObject
{
    Q_OBJECT
    
public:
    explicit GeoMetricProjection(QObject *parent = nullptr);
    
    void setOrigin(const CoordinateWGS84 &origin);
    bool hasOrigin() const;
    
    CoordinateWGS84 originWgs84() const;
    CoordinateUTM originUtm() const;
    
    static CoordinateUTM wgs84ToUtm(const CoordinateWGS84 &coordinate);
    static CoordinateWGS84 utmToWgs84(const CoordinateUTM &coordinate);
    
    CoordinateLocal wgs84ToLocal(const CoordinateWGS84 &coordinate) const;
    CoordinateWGS84 localToWgs84(const CoordinateLocal &coordinate) const;
    
    static double distanceMeters(const CoordinateWGS84 &a,
                                 const CoordinateWGS84 &b);
    
    static double azimuthDegrees(const CoordinateWGS84 &a,
                                 const CoordinateWGS84 &b);
    
signals:
    void originChanged();
    
private:
    CoordinateUTM wgs84ToOriginZoneUtm(const CoordinateWGS84 &coordinate) const;
    
private:
    bool m_hasOrigin = false;
    
    CoordinateWGS84 m_originWgs84;
    CoordinateUTM m_originUtm;
};

#endif // GEO_METRIC_PROJECTION_H
