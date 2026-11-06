#ifndef MAP_GLOBE_CAMERA_H
#define MAP_GLOBE_CAMERA_H

#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_picking.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QVector3D>

class MapModel;

struct MapGlobeCameraBasis
{
    QVector3D right;
    QVector3D up;
    QVector3D eye;
};

// Backend-neutral canonical Globe camera. This class owns all camera math
// that is independent of a graphics API: orbit state, sticky ECEF render
// origin, camera basis, near/far planes and the canonical perspective/view
// matrix. A concrete GPU backend is responsible only for applying its own
// clip-space correction to viewProjectionMatrix().
class MapGlobeCamera
{
public:
    MapGlobeCamera();

    void setViewportSize(const QSize &viewport_size);
    void syncFromMapModel(const MapModel &map_model);

    void updateRenderOrigin();
    GeoWgs84Ellipsoid::EcefPositionD renderOriginEcef() const;

    QMatrix4x4 viewProjectionMatrix(
        MapGlobeCameraBasis *camera_basis = nullptr) const;
    // Re-evaluate the same physical camera relative to a retained geometry
    // origin. Terrain continues to use viewProjectionMatrix() with the
    // frequently rebased camera origin.
    QMatrix4x4 viewProjectionMatrixForOrigin(
        const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef,
        MapGlobeCameraBasis *camera_basis = nullptr) const;
    bool screenRay(
        const QPointF &screen_position,
        MapGlobeScreenRay *ray) const;
    double orbitDistanceM() const;

private:
    QSize viewport_size;
    double target_lon_deg = 0.0;
    double target_lat_deg = 0.0;
    double yaw_deg = 0.0;
    double pitch_deg = 55.0;
    double distance_m = 0.0;
    double vertical_offset_m = 0.0;
    double camera_collision_lift_m = 0.0;

    GeoWgs84Ellipsoid::EcefPositionD render_origin_ecef;
    bool render_origin_valid = false;
};

#endif // MAP_GLOBE_CAMERA_H
