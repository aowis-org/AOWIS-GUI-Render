#ifndef MAP_GLOBE_PICKING_H
#define MAP_GLOBE_PICKING_H

#include "geo/geo_wgs84_ellipsoid.h"

#include <QPointF>
#include <QSize>
#include <QVector3D>

struct MapGlobeScreenRayParameters
{
    double target_lon_deg = 0.0;
    double target_lat_deg = 0.0;
    double yaw_deg = 0.0;
    double pitch_deg = 0.0;
    double distance_m = 0.0;
    double target_height_m = 0.0;
    double collision_lift_m = 0.0;
    double field_of_view_deg = 45.0;
};

struct MapGlobeScreenRay
{
    GeoWgs84Ellipsoid::EcefPositionD origin_ecef;
    QVector3D direction;
};

// Builds a Globe camera ray from canonical Qt screen coordinates
// (top-left origin, +Y downward). No renderer/backend clip-space convention
// participates in this calculation.
bool mapGlobeBuildScreenRay(
    const QPointF &screen_position,
    const QSize &viewport_size,
    const MapGlobeScreenRayParameters &parameters,
    MapGlobeScreenRay *ray);

bool mapGlobeRayTriangleIntersectionDistance(
    const QVector3D &ray_origin,
    const QVector3D &ray_direction,
    const QVector3D &a,
    const QVector3D &b,
    const QVector3D &c,
    double *distance_m);

bool mapGlobeRaySphereIntersectionDistance(
    const QVector3D &ray_origin,
    const QVector3D &ray_direction,
    const QVector3D &sphere_center,
    double sphere_radius_m,
    double *distance_m);

// Replaces nearest_distance_m only when candidate_distance_m is a finite,
// non-negative hit that is closer than the current value.
bool mapGlobeUpdateNearestHitDistance(
    double candidate_distance_m,
    double *nearest_distance_m);

#endif // MAP_GLOBE_PICKING_H
