#include "geo/geo_wgs84_ellipsoid.h"

#include <GeographicLib/Geocentric.hpp>
#include <GeographicLib/Geodesic.hpp>

#include <QtMath>

#include <cmath>
#include <vector>

QVector3D GeoWgs84Ellipsoid::geodeticToEcef(double lon_deg, double lat_deg, double height_m)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    GeographicLib::Geocentric::WGS84().Forward(lat_deg, lon_deg, height_m, x, y, z);
    return QVector3D(float(x), float(y), float(z));
}

GeoWgs84Ellipsoid::EcefPositionD GeoWgs84Ellipsoid::geodeticToEcefD(
    double lon_deg, double lat_deg, double height_m)
{
    EcefPositionD result;
    GeographicLib::Geocentric::WGS84().Forward(
        lat_deg, lon_deg, height_m, result.x, result.y, result.z);
    return result;
}

GeoWgs84Ellipsoid::LocalFrame GeoWgs84Ellipsoid::localFrameAtGeodetic(
    double lon_deg, double lat_deg, double height_m)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::vector<double> rotation(9, 0.0);
    GeographicLib::Geocentric::WGS84().Forward(
        lat_deg, lon_deg, height_m, x, y, z, rotation);

    LocalFrame frame;
    frame.position = QVector3D(float(x), float(y), float(z));

    // GeographicLib::Geocentric::Forward documents the rotation matrix M as
    // row-major with ecef = M * [east, north, up]^T, i.e. column j of M is
    // the ECEF direction of local unit axis j (0 = east, 1 = north, 2 = up).
    frame.east = QVector3D(float(rotation[0]), float(rotation[3]), float(rotation[6]));
    frame.north = QVector3D(float(rotation[1]), float(rotation[4]), float(rotation[7]));
    frame.up = QVector3D(float(rotation[2]), float(rotation[5]), float(rotation[8]));
    return frame;
}

bool GeoWgs84Ellipsoid::ecefToGeodetic(
    const QVector3D &ecef, double *lon_deg, double *lat_deg, double *height_m)
{
    if (lon_deg == nullptr || lat_deg == nullptr)
        return false;
    if (ecef.lengthSquared() <= 1e-6f)
        return false;

    double lat = 0.0;
    double lon = 0.0;
    double height = 0.0;
    GeographicLib::Geocentric::WGS84().Reverse(
        double(ecef.x()), double(ecef.y()), double(ecef.z()), lat, lon, height);

    *lon_deg = lon;
    *lat_deg = lat;
    if (height_m != nullptr)
        *height_m = height;
    return true;
}

bool GeoWgs84Ellipsoid::ecefToGeodetic(
    const EcefPositionD &ecef, double *lon_deg, double *lat_deg, double *height_m)
{
    if (lon_deg == nullptr || lat_deg == nullptr)
        return false;
    const double length_squared = ecef.x * ecef.x + ecef.y * ecef.y + ecef.z * ecef.z;
    if (length_squared <= 1e-12)
        return false;

    double lat = 0.0;
    double lon = 0.0;
    double height = 0.0;
    GeographicLib::Geocentric::WGS84().Reverse(
        ecef.x, ecef.y, ecef.z, lat, lon, height);

    *lon_deg = lon;
    *lat_deg = lat;
    if (height_m != nullptr)
        *height_m = height;
    return true;
}

bool GeoWgs84Ellipsoid::offsetGeodetic(
    double lon_deg, double lat_deg, double east_m, double north_m,
    double *offset_lon_deg, double *offset_lat_deg)
{
    if (offset_lon_deg == nullptr || offset_lat_deg == nullptr
        || !std::isfinite(lon_deg) || !std::isfinite(lat_deg)
        || !std::isfinite(east_m) || !std::isfinite(north_m))
    {
        return false;
    }

    const double distance_m = std::hypot(east_m, north_m);
    if (distance_m <= 1e-9)
    {
        *offset_lon_deg = lon_deg;
        *offset_lat_deg = lat_deg;
        return true;
    }

    const double azimuth_deg = qRadiansToDegrees(std::atan2(east_m, north_m));
    double result_lat_deg = 0.0;
    double result_lon_deg = 0.0;
    GeographicLib::Geodesic::WGS84().Direct(
        lat_deg, lon_deg, azimuth_deg, distance_m,
        result_lat_deg, result_lon_deg);
    *offset_lon_deg = result_lon_deg;
    *offset_lat_deg = result_lat_deg;
    return std::isfinite(result_lon_deg) && std::isfinite(result_lat_deg);
}

GeoWgs84Ellipsoid::EcefPositionD GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
    double target_lon_deg, double target_lat_deg,
    double yaw_deg, double pitch_deg, double distance_m,
    double target_height_m, double camera_collision_lift_m)
{
    double target_x = 0.0;
    double target_y = 0.0;
    double target_z = 0.0;
    std::vector<double> rotation(9, 0.0);
    GeographicLib::Geocentric::WGS84().Forward(
        target_lat_deg, target_lon_deg, target_height_m,
        target_x, target_y, target_z, rotation);

    const double pitch_rad = qDegreesToRadians(pitch_deg);
    const double yaw_rad = qDegreesToRadians(yaw_deg);
    const double distance = qMax(0.0, distance_m);
    const double horizontal_distance = distance * std::cos(pitch_rad);
    const double vertical_offset = distance * std::sin(pitch_rad)
        + qMax(0.0, camera_collision_lift_m);
    const double horizontal_east = std::sin(yaw_rad);
    const double horizontal_north = -std::cos(yaw_rad);

    EcefPositionD eye;
    eye.x = target_x
        + rotation[2] * vertical_offset
        + (rotation[0] * horizontal_east + rotation[1] * horizontal_north)
            * horizontal_distance;
    eye.y = target_y
        + rotation[5] * vertical_offset
        + (rotation[3] * horizontal_east + rotation[4] * horizontal_north)
            * horizontal_distance;
    eye.z = target_z
        + rotation[8] * vertical_offset
        + (rotation[6] * horizontal_east + rotation[7] * horizontal_north)
            * horizontal_distance;
    return eye;
}

GeoWgs84Ellipsoid::OrbitCameraBasis GeoWgs84Ellipsoid::orbitCameraBasis(
    double target_lon_deg, double target_lat_deg,
    double yaw_deg, double pitch_deg, double distance_m,
    double target_height_m, double camera_collision_lift_m)
{
    const LocalFrame frame = localFrameAtGeodetic(
        target_lon_deg, target_lat_deg, target_height_m);
    const double pitch_rad = qDegreesToRadians(pitch_deg);
    const double yaw_rad = qDegreesToRadians(yaw_deg);
    const double distance = qMax(0.0, distance_m);
    const double horizontal_distance = distance * std::cos(pitch_rad);

    // Use the established AOWIS orbit convention in the local ENU frame.
    // Yaw 0 places the
    // eye south of the focus while screen-up points north. Expressed in ENU
    // that is:
    //   eye horizontal = +east*sin(yaw) - north*cos(yaw)
    //   screen right   = +east*cos(yaw) + north*sin(yaw)
    //
    // The old globe basis used +north at yaw 0, which orbited from the
    // opposite side of the focus. Tilting away from nadir therefore made the
    // globe rise toward the upper edge of the screen instead of following the
    // established map-camera orientation.
    const QVector3D horizontal_direction =
        frame.east * float(std::sin(yaw_rad)) - frame.north * float(std::cos(yaw_rad));
    const QVector3D right =
        frame.east * float(std::cos(yaw_rad)) + frame.north * float(std::sin(yaw_rad));

    OrbitCameraBasis basis;
    basis.target = frame.position;
    basis.eye = frame.position
        + frame.up * float(
            distance * std::sin(pitch_rad) + qMax(0.0, camera_collision_lift_m))
        + horizontal_direction * float(horizontal_distance);
    basis.forward = (basis.target - basis.eye).normalized();
    basis.right = right;
    // "right" is constructed purely from yaw, in the target's tangent
    // plane, so it is always perpendicular to "forward" (which only ever
    // combines "up" and "horizontal_direction", both of which "right" is
    // perpendicular to by construction) -- including at pitch 90 degrees,
    // where a naive up-vector-based lookAt() would degenerate. Using
    // cross(right, forward) here (rather than cross(forward, right)) is
    // deliberate: lookAt() derives its screen-right axis as cross(forward,
    // up_hint), and cross(forward, cross(right, forward)) reduces to
    // +right (by the vector triple product identity, since right and
    // forward are perpendicular and forward is unit length), which keeps
    // geographic east on screen-right. The other cross order would
    // silently mirror the globe left/right.
    basis.up = QVector3D::crossProduct(right, basis.forward).normalized();
    return basis;
}

GeoWgs84Ellipsoid::OrbitCameraBasisRelative GeoWgs84Ellipsoid::orbitCameraBasisRelativeToOrigin(
    double target_lon_deg, double target_lat_deg,
    double yaw_deg, double pitch_deg, double distance_m,
    double target_height_m, const EcefPositionD &origin_ecef,
    double camera_collision_lift_m)
{
    // Same GeographicLib call localFrameAtGeodetic() makes, but the
    // target's own ECEF position is kept in double (target_x/y/z) instead
    // of being narrowed to QVector3D immediately -- see the EcefPositionD
    // comment in the header for why. east/north/up are unit *directions*,
    // never large in magnitude, so narrowing those to float here (as
    // localFrameAtGeodetic() also does) loses nothing.
    double target_x = 0.0;
    double target_y = 0.0;
    double target_z = 0.0;
    std::vector<double> rotation(9, 0.0);
    GeographicLib::Geocentric::WGS84().Forward(
        target_lat_deg, target_lon_deg, target_height_m,
        target_x, target_y, target_z, rotation);

    // Copy-initialization ("= QVector3D(...)"), not direct-initialization
    // ("QVector3D east(...)"): with direct-init here, each "float(rotation[N])"
    // argument is itself grammatically a valid parameter declaration (type
    // "float", parenthesized declarator "rotation[N]"), so the compiler is
    // required to parse the whole line as a function *declaration* named
    // "east" rather than a variable definition -- the classic "most vexing
    // parse". Copy-init has no such ambiguity.
    const QVector3D east = QVector3D(float(rotation[0]), float(rotation[3]), float(rotation[6]));
    const QVector3D north = QVector3D(float(rotation[1]), float(rotation[4]), float(rotation[7]));
    const QVector3D up_direction =
        QVector3D(float(rotation[2]), float(rotation[5]), float(rotation[8]));

    const double pitch_rad = qDegreesToRadians(pitch_deg);
    const double yaw_rad = qDegreesToRadians(yaw_deg);
    const double distance = qMax(0.0, distance_m);
    const double horizontal_distance = distance * std::cos(pitch_rad);
    const double vertical_offset = distance * std::sin(pitch_rad)
        + qMax(0.0, camera_collision_lift_m);

    // Same ENU combination orbitCameraBasis() above uses for its eye
    // offset and "right" axis -- see its comment for the yaw/pitch
    // convention and the reasoning behind the cross(right, forward) order.
    // Only difference here: the offset is accumulated against target_x/y/z
    // in double (eye_x/y/z below) instead of against a float32 QVector3D,
    // so the offset -- which can be as large as the current orbit
    // distance, up to ~1.18e7 m at maximum Globe zoom-out -- never has to
    // round against Earth-radius-scale (~6.378e6 m) values before it needs
    // to.
    const QVector3D horizontal_direction =
        east * float(std::sin(yaw_rad)) - north * float(std::cos(yaw_rad));
    const QVector3D right =
        east * float(std::cos(yaw_rad)) + north * float(std::sin(yaw_rad));

    const double eye_x = target_x
        + double(up_direction.x()) * vertical_offset
        + double(horizontal_direction.x()) * horizontal_distance;
    const double eye_y = target_y
        + double(up_direction.y()) * vertical_offset
        + double(horizontal_direction.y()) * horizontal_distance;
    const double eye_z = target_z
        + double(up_direction.z()) * vertical_offset
        + double(horizontal_direction.z()) * horizontal_distance;

    // The only precision-critical step: target and eye are subtracted
    // against origin_ecef here, in double, while both sides of each
    // subtraction still carry their full ECEF-scale precision. The
    // *results* are small (bounded by how far the camera actually is from
    // the render origin -- typically the current orbit distance or less),
    // so narrowing them to float32 now, for the QVector3D members below,
    // no longer throws away anything that matters.
    OrbitCameraBasisRelative basis;
    basis.target = QVector3D(
        float(target_x - origin_ecef.x),
        float(target_y - origin_ecef.y),
        float(target_z - origin_ecef.z));
    basis.eye = QVector3D(
        float(eye_x - origin_ecef.x),
        float(eye_y - origin_ecef.y),
        float(eye_z - origin_ecef.z));
    basis.forward = (basis.target - basis.eye).normalized();
    basis.right = right;
    // See orbitCameraBasis()'s comment above -- same triple-product
    // reasoning applies unchanged, since it only depends on "right" and
    // "forward" being perpendicular and forward being unit length, neither
    // of which is affected by eye/target now being origin-relative.
    basis.up = QVector3D::crossProduct(right, basis.forward).normalized();
    return basis;
}

bool GeoWgs84Ellipsoid::rayIntersection(
    const QVector3D &origin, const QVector3D &direction, QVector3D *intersection)
{
    if (intersection == nullptr)
        return false;

    // Scale ECEF space by (1/a, 1/a, 1/b) so the WGS84 ellipsoid becomes a
    // unit sphere, solve the standard ray/sphere quadratic there, then use
    // the resulting ray parameter "t" against the *original* (unscaled) ray
    // -- t is the same parametrization in both spaces since the scaling is
    // linear and applied identically to both origin and direction.
    const float inverse_a = float(1.0 / EquatorialRadiusM);
    const float inverse_b = float(1.0 / PolarRadiusM);
    const QVector3D scale(inverse_a, inverse_a, inverse_b);

    const QVector3D scaled_origin(
        origin.x() * scale.x(), origin.y() * scale.y(), origin.z() * scale.z());
    const QVector3D scaled_direction(
        direction.x() * scale.x(), direction.y() * scale.y(), direction.z() * scale.z());

    const double a_coefficient = double(QVector3D::dotProduct(scaled_direction, scaled_direction));
    if (a_coefficient <= 1e-20)
        return false;

    const double b_coefficient =
        2.0 * double(QVector3D::dotProduct(scaled_origin, scaled_direction));
    const double c_coefficient =
        double(QVector3D::dotProduct(scaled_origin, scaled_origin)) - 1.0;
    const double discriminant = b_coefficient * b_coefficient - 4.0 * a_coefficient * c_coefficient;
    if (discriminant < 0.0)
        return false;

    const double sqrt_discriminant = std::sqrt(discriminant);
    const double t_near = (-b_coefficient - sqrt_discriminant) / (2.0 * a_coefficient);
    const double t_far = (-b_coefficient + sqrt_discriminant) / (2.0 * a_coefficient);
    // Nearest intersection in front of the ray origin -- t_near first (the
    // ray entering the ellipsoid), falling back to t_far for the
    // origin-inside-the-ellipsoid case. That should not happen for an
    // orbit camera, which always sits outside the globe, but is handled
    // defensively rather than assumed away.
    const double t = t_near >= 0.0 ? t_near : t_far;
    if (t < 0.0)
        return false;

    *intersection = origin + direction * float(t);
    return true;
}

bool GeoWgs84Ellipsoid::rayIntersection(
    const EcefPositionD &origin, const QVector3D &direction,
    EcefPositionD *intersection, double *distance_m)
{
    if (intersection == nullptr || distance_m == nullptr
        || direction.lengthSquared() <= 1e-12f)
    {
        return false;
    }

    QVector3D normalized_direction = direction;
    normalized_direction.normalize();

    const double inverse_a = 1.0 / EquatorialRadiusM;
    const double inverse_b = 1.0 / PolarRadiusM;
    const double scaled_origin_x = origin.x * inverse_a;
    const double scaled_origin_y = origin.y * inverse_a;
    const double scaled_origin_z = origin.z * inverse_b;
    const double scaled_direction_x =
        double(normalized_direction.x()) * inverse_a;
    const double scaled_direction_y =
        double(normalized_direction.y()) * inverse_a;
    const double scaled_direction_z =
        double(normalized_direction.z()) * inverse_b;

    const double a_coefficient =
        scaled_direction_x * scaled_direction_x
        + scaled_direction_y * scaled_direction_y
        + scaled_direction_z * scaled_direction_z;
    if (a_coefficient <= 1e-30)
        return false;

    const double b_coefficient = 2.0 * (
        scaled_origin_x * scaled_direction_x
        + scaled_origin_y * scaled_direction_y
        + scaled_origin_z * scaled_direction_z);
    const double c_coefficient =
        scaled_origin_x * scaled_origin_x
        + scaled_origin_y * scaled_origin_y
        + scaled_origin_z * scaled_origin_z - 1.0;
    const double discriminant = b_coefficient * b_coefficient
        - 4.0 * a_coefficient * c_coefficient;
    if (discriminant < 0.0)
        return false;

    const double sqrt_discriminant = std::sqrt(discriminant);
    const double t_near = (-b_coefficient - sqrt_discriminant)
        / (2.0 * a_coefficient);
    const double t_far = (-b_coefficient + sqrt_discriminant)
        / (2.0 * a_coefficient);
    const double distance = t_near >= 0.0 ? t_near : t_far;
    if (!(distance >= 0.0) || !std::isfinite(distance))
        return false;

    intersection->x = origin.x
        + double(normalized_direction.x()) * distance;
    intersection->y = origin.y
        + double(normalized_direction.y()) * distance;
    intersection->z = origin.z
        + double(normalized_direction.z()) * distance;
    *distance_m = distance;
    return true;
}

bool GeoWgs84Ellipsoid::screenRay(
    const OrbitCameraBasis &basis, const QPointF &screen_position,
    const QSize &viewport, double vertical_fov_deg,
    QVector3D *eye, QVector3D *direction)
{
    if (eye == nullptr || direction == nullptr || !viewport.isValid()
        || !std::isfinite(screen_position.x()) || !std::isfinite(screen_position.y()))
    {
        return false;
    }

    const double viewport_width = double(qMax(1, viewport.width()));
    const double viewport_height = double(qMax(1, viewport.height()));
    const double aspect = viewport_width / viewport_height;
    const double tan_half_fov = std::tan(qDegreesToRadians(vertical_fov_deg * 0.5));
    const double ndc_x = 2.0 * screen_position.x() / viewport_width - 1.0;
    const double ndc_y = 1.0 - 2.0 * screen_position.y() / viewport_height;

    QVector3D ray_direction = basis.forward
        + basis.right * float(ndc_x * tan_half_fov * aspect)
        + basis.up * float(ndc_y * tan_half_fov);
    if (ray_direction.lengthSquared() <= 1e-12f)
        return false;

    ray_direction.normalize();
    *eye = basis.eye;
    *direction = ray_direction;
    return true;
}
