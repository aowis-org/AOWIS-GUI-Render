#include "map/render/map_globe_picking.h"

#include <QtMath>

#include <cmath>

bool mapGlobeBuildScreenRay(
    const QPointF &screen_position,
    const QSize &viewport_size,
    const MapGlobeScreenRayParameters &parameters,
    MapGlobeScreenRay *ray)
{
    if (ray == nullptr || !viewport_size.isValid()
        || !std::isfinite(screen_position.x())
        || !std::isfinite(screen_position.y())
        || !std::isfinite(parameters.field_of_view_deg)
        || !(parameters.field_of_view_deg > 0.0)
        || !(parameters.field_of_view_deg < 180.0))
    {
        return false;
    }

    const GeoWgs84Ellipsoid::EcefPositionD local_origin =
        GeoWgs84Ellipsoid::geodeticToEcefD(
            parameters.target_lon_deg,
            parameters.target_lat_deg,
            parameters.target_height_m);
    const GeoWgs84Ellipsoid::OrbitCameraBasisRelative basis =
        GeoWgs84Ellipsoid::orbitCameraBasisRelativeToOrigin(
            parameters.target_lon_deg,
            parameters.target_lat_deg,
            parameters.yaw_deg,
            parameters.pitch_deg,
            parameters.distance_m,
            parameters.target_height_m,
            local_origin,
            parameters.collision_lift_m);

    const double viewport_width = double(qMax(1, viewport_size.width()));
    const double viewport_height = double(qMax(1, viewport_size.height()));
    const double aspect = viewport_width / viewport_height;
    const double tan_half_fov = std::tan(
        qDegreesToRadians(parameters.field_of_view_deg * 0.5));
    const double ndc_x = 2.0 * screen_position.x() / viewport_width - 1.0;
    const double ndc_y = 1.0 - 2.0 * screen_position.y() / viewport_height;

    QVector3D direction = basis.forward
        + basis.right * float(ndc_x * tan_half_fov * aspect)
        + basis.up * float(ndc_y * tan_half_fov);
    if (direction.lengthSquared() <= 1e-12f)
        return false;
    direction.normalize();

    ray->origin_ecef = GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
        parameters.target_lon_deg,
        parameters.target_lat_deg,
        parameters.yaw_deg,
        parameters.pitch_deg,
        parameters.distance_m,
        parameters.target_height_m,
        parameters.collision_lift_m);
    ray->direction = direction;
    return true;
}

bool mapGlobeRayTriangleIntersectionDistance(
    const QVector3D &ray_origin,
    const QVector3D &ray_direction,
    const QVector3D &a,
    const QVector3D &b,
    const QVector3D &c,
    double *distance_m)
{
    if (distance_m == nullptr)
        return false;

    const QVector3D edge1 = b - a;
    const QVector3D edge2 = c - a;
    const QVector3D cross = QVector3D::crossProduct(ray_direction, edge2);
    const double determinant = double(QVector3D::dotProduct(edge1, cross));
    if (std::abs(determinant) <= 1e-10)
        return false;

    const double inverse_determinant = 1.0 / determinant;
    const QVector3D from_a = ray_origin - a;
    const double u = double(QVector3D::dotProduct(from_a, cross))
        * inverse_determinant;
    if (u < 0.0 || u > 1.0)
        return false;

    const QVector3D q = QVector3D::crossProduct(from_a, edge1);
    const double v = double(QVector3D::dotProduct(ray_direction, q))
        * inverse_determinant;
    if (v < 0.0 || u + v > 1.0)
        return false;

    const double hit_distance_m = double(QVector3D::dotProduct(edge2, q))
        * inverse_determinant;
    if (hit_distance_m < 0.0 || !std::isfinite(hit_distance_m))
        return false;

    *distance_m = hit_distance_m;
    return true;
}

bool mapGlobeRaySphereIntersectionDistance(
    const QVector3D &ray_origin,
    const QVector3D &ray_direction,
    const QVector3D &sphere_center,
    double sphere_radius_m,
    double *distance_m)
{
    if (distance_m == nullptr || !(sphere_radius_m >= 0.0)
        || !std::isfinite(sphere_radius_m))
    {
        return false;
    }

    const QVector3D offset = ray_origin - sphere_center;
    const double a = double(QVector3D::dotProduct(ray_direction, ray_direction));
    if (!(a > 1e-20) || !std::isfinite(a))
        return false;

    const double b = 2.0 * double(QVector3D::dotProduct(offset, ray_direction));
    const double c = double(QVector3D::dotProduct(offset, offset))
        - sphere_radius_m * sphere_radius_m;
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0 || !std::isfinite(discriminant))
        return false;

    const double root = std::sqrt(discriminant);
    const double inverse_two_a = 0.5 / a;
    const double near_distance_m = (-b - root) * inverse_two_a;
    const double far_distance_m = (-b + root) * inverse_two_a;
    const double hit_distance_m = near_distance_m >= 0.0
        ? near_distance_m
        : far_distance_m;
    if (hit_distance_m < 0.0 || !std::isfinite(hit_distance_m))
        return false;

    *distance_m = hit_distance_m;
    return true;
}

bool mapGlobeUpdateNearestHitDistance(
    double candidate_distance_m,
    double *nearest_distance_m)
{
    if (nearest_distance_m == nullptr
        || candidate_distance_m < 0.0
        || !std::isfinite(candidate_distance_m)
        || candidate_distance_m >= *nearest_distance_m)
    {
        return false;
    }

    *nearest_distance_m = candidate_distance_m;
    return true;
}
