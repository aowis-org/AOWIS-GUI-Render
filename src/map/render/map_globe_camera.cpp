#include "map/render/map_globe_camera.h"

#include "map/core/map_model.h"

#include <QtMath>

#include <cmath>

namespace
{
// A camera-origin rebase affects Globe terrain resources, not the separately
// anchored retained network geometry. The minimum keeps camera-relative
// terrain coordinates comfortably sub-centimetre, while
// updateRenderOrigin() expands it at distant zooms according to the current
// meters-per-pixel.
constexpr double GlobeRenderOriginMinimumRebaseThresholdM = 50000.0;

// Maximum tolerated float32 coordinate quantisation attributable to origin
// drift, expressed in logical screen pixels.
constexpr double GlobeRenderOriginPrecisionBudgetPx = 0.05;
constexpr double Float32FractionScale = 8388608.0; // 2^23

// Keep relief above the geometric horizon from being clipped without
// wasting depth precision on the far side of the planet.
constexpr double GlobeHorizonMarginM = 50000.0;

void globeNearFarPlanesM(
    double distance_m, double pitch_deg, double target_height_m,
    double camera_collision_lift_m,
    double *near_plane_m, double *far_plane_m)
{
    // The eye's height above the sphere through sea level: the target lies
    // target_height_m above it, the eye distance_m from the target at pitch
    // above the target's horizon, so its distance from the centre is
    // |target + distance * direction| (law of cosines). The flat
    // approximation target_height + distance * sin(pitch) left out the
    // curvature: zoomed out and tilted (5000 km at 10 deg: 868 instead of
    // 2369 km) the far plane fell short of the horizon and cut off the far
    // side of the globe.
    const double pitch_rad = qDegreesToRadians(pitch_deg);
    const double radius_m = GeoWgs84Ellipsoid::EquatorialRadiusM;
    const double target_radius_m = radius_m + target_height_m;
    const double eye_radius_m = std::sqrt(target_radius_m * target_radius_m + distance_m * distance_m
                                          + 2.0 * target_radius_m * distance_m * std::sin(pitch_rad));
    const double eye_altitude_m = qMax(
        1.0, eye_radius_m - radius_m + qMax(0.0, camera_collision_lift_m));
    const double horizon_distance_m = std::sqrt(
        eye_altitude_m
        * (2.0 * GeoWgs84Ellipsoid::EquatorialRadiusM + eye_altitude_m));

    *near_plane_m = qMax(0.1, distance_m * 0.001);
    *far_plane_m = qMax(
        *near_plane_m * 10.0,
        qMax(horizon_distance_m, distance_m) + GlobeHorizonMarginM);
}
}

MapGlobeCamera::MapGlobeCamera() = default;

void MapGlobeCamera::setViewportSize(const QSize &viewport_size)
{
    this->viewport_size = viewport_size;
}

void MapGlobeCamera::syncFromMapModel(const MapModel &map_model)
{
    const MapCameraViewState view = map_model.cameraViewState();
    this->target_lon_deg = view.center_lon_deg;
    this->target_lat_deg = view.center_lat_deg;
    this->yaw_deg = view.globe_yaw_deg;
    this->pitch_deg = view.globe_pitch_deg;
    this->distance_m = view.globe_distance_m;
    this->vertical_offset_m = view.globe_vertical_offset_m;
    this->camera_collision_lift_m = view.globe_collision_lift_m;
}

void MapGlobeCamera::updateRenderOrigin()
{
    const GeoWgs84Ellipsoid::EcefPositionD candidate =
        GeoWgs84Ellipsoid::geodeticToEcefD(
            this->target_lon_deg, this->target_lat_deg,
            this->vertical_offset_m);

    if (!this->render_origin_valid)
    {
        this->render_origin_ecef = candidate;
        this->render_origin_valid = true;
        return;
    }

    const double delta_x = candidate.x - this->render_origin_ecef.x;
    const double delta_y = candidate.y - this->render_origin_ecef.y;
    const double delta_z = candidate.z - this->render_origin_ecef.z;
    const double drift_squared_m =
        delta_x * delta_x + delta_y * delta_y + delta_z * delta_z;

    // Precision requirements are screen-relative. At close zoom the 50 km
    // floor applies; farther out, one pixel covers more metres and the origin
    // can remain fixed across proportionally larger target motion.
    const double distance_m = orbitDistanceM();
    const double viewport_height_px =
        double(qMax(1, this->viewport_size.height()));
    const double meters_per_pixel =
        2.0 * distance_m
        * std::tan(qDegreesToRadians(MapModel::GlobeFieldOfViewDeg * 0.5))
        / viewport_height_px;
    const double precision_scaled_threshold_m =
        meters_per_pixel * GlobeRenderOriginPrecisionBudgetPx
        * Float32FractionScale;
    const double rebase_threshold_m = qMax(
        GlobeRenderOriginMinimumRebaseThresholdM,
        precision_scaled_threshold_m);
    const double threshold_squared_m =
        rebase_threshold_m * rebase_threshold_m;
    if (drift_squared_m > threshold_squared_m)
        this->render_origin_ecef = candidate;
}

GeoWgs84Ellipsoid::EcefPositionD MapGlobeCamera::renderOriginEcef() const
{
    return this->render_origin_ecef;
}

QMatrix4x4 MapGlobeCamera::viewProjectionMatrix(
    MapGlobeCameraBasis *camera_basis) const
{
    return viewProjectionMatrixForOrigin(this->render_origin_ecef, camera_basis);
}

QMatrix4x4 MapGlobeCamera::viewProjectionMatrixForOrigin(
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef,
    MapGlobeCameraBasis *camera_basis) const
{
    const int viewport_width = qMax(1, this->viewport_size.width());
    const int viewport_height = qMax(1, this->viewport_size.height());
    const double bounded_pitch_deg = qBound(
        MapModel::MinViewGlobePitchDeg, this->pitch_deg,
        MapModel::MaxViewGlobePitchDeg);
    const double bounded_distance_m = orbitDistanceM();

    const GeoWgs84Ellipsoid::OrbitCameraBasisRelative basis =
        GeoWgs84Ellipsoid::orbitCameraBasisRelativeToOrigin(
            this->target_lon_deg, this->target_lat_deg,
            this->yaw_deg, bounded_pitch_deg, bounded_distance_m,
            this->vertical_offset_m, origin_ecef,
            this->camera_collision_lift_m);
    if (camera_basis != nullptr)
    {
        camera_basis->right = basis.right;
        camera_basis->up = basis.up;
        camera_basis->eye = basis.eye;
    }

    double near_plane_m = 0.0;
    double far_plane_m = 0.0;
    globeNearFarPlanesM(
        bounded_distance_m, bounded_pitch_deg, this->vertical_offset_m,
        this->camera_collision_lift_m,
        &near_plane_m, &far_plane_m);

    QMatrix4x4 projection;
    projection.perspective(
        float(MapModel::GlobeFieldOfViewDeg),
        float(viewport_width) / float(viewport_height),
        float(near_plane_m), float(far_plane_m));

    QMatrix4x4 view;
    view.lookAt(basis.eye, basis.target, basis.up);

    QMatrix4x4 result = projection;
    result *= view;
    return result;
}

bool MapGlobeCamera::screenRay(
    const QPointF &screen_position,
    MapGlobeScreenRay *ray) const
{
    MapGlobeScreenRayParameters parameters;
    parameters.target_lon_deg = this->target_lon_deg;
    parameters.target_lat_deg = this->target_lat_deg;
    parameters.yaw_deg = this->yaw_deg;
    parameters.pitch_deg = qBound(
        MapModel::MinViewGlobePitchDeg, this->pitch_deg,
        MapModel::MaxViewGlobePitchDeg);
    parameters.distance_m = orbitDistanceM();
    parameters.target_height_m = this->vertical_offset_m;
    parameters.collision_lift_m = this->camera_collision_lift_m;
    parameters.field_of_view_deg = MapModel::GlobeFieldOfViewDeg;

    return mapGlobeBuildScreenRay(
        screen_position, this->viewport_size, parameters, ray);
}

double MapGlobeCamera::orbitDistanceM() const
{
    return qMax(MapModel::MinViewGlobeDistanceM, this->distance_m);
}
