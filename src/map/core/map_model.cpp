#include "map/core/map_model.h"
#include "map/core/map_imagery_provider_directory.h"
#include "map/render/map_render_diagnostics.h"

#include "map/render/map_render_cache_math.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_picking.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QtMath>

namespace
{
constexpr double CoordinateComparisonEpsilon = 1e-12;
double normalizedYawDegrees(double yaw_deg)
{
    double normalized = std::fmod(yaw_deg, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    return normalized;
}

double ecefLength(const GeoWgs84Ellipsoid::EcefPositionD &vector)
{
    return std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

// A turn about a unit axis through the Earth's centre.
struct GlobeRotation
{
    GeoWgs84Ellipsoid::EcefPositionD axis;
    double angle_rad = 0.0;
};

// The shortest turn carrying the direction of from onto that of to: about
// from x to, by the angle between them.
GlobeRotation globeRotationBetween(
    const GeoWgs84Ellipsoid::EcefPositionD &from, const GeoWgs84Ellipsoid::EcefPositionD &to)
{
    GlobeRotation rotation;
    GeoWgs84Ellipsoid::EcefPositionD axis;
    axis.x = from.y * to.z - from.z * to.y;
    axis.y = from.z * to.x - from.x * to.z;
    axis.z = from.x * to.y - from.y * to.x;
    const double axis_length = ecefLength(axis);
    if (!(axis_length > 0.0))
        return rotation;
    rotation.axis.x = axis.x / axis_length;
    rotation.axis.y = axis.y / axis_length;
    rotation.axis.z = axis.z / axis_length;
    rotation.angle_rad = std::atan2(axis_length, from.x * to.x + from.y * to.y + from.z * to.z);
    return rotation;
}

// Rodrigues' rotation of vector about the rotation's unit axis.
GeoWgs84Ellipsoid::EcefPositionD rotatedEcef(
    const GlobeRotation &rotation, const GeoWgs84Ellipsoid::EcefPositionD &vector)
{
    const GeoWgs84Ellipsoid::EcefPositionD &axis = rotation.axis;
    const double cosine = std::cos(rotation.angle_rad);
    const double sine = std::sin(rotation.angle_rad);
    const double along = (axis.x * vector.x + axis.y * vector.y + axis.z * vector.z) * (1.0 - cosine);
    GeoWgs84Ellipsoid::EcefPositionD rotated;
    rotated.x = vector.x * cosine + (axis.y * vector.z - axis.z * vector.y) * sine + axis.x * along;
    rotated.y = vector.y * cosine + (axis.z * vector.x - axis.x * vector.z) * sine + axis.y * along;
    rotated.z = vector.z * cosine + (axis.x * vector.y - axis.y * vector.x) * sine + axis.z * along;
    return rotated;
}

bool coordinatesEqual(double first, double second)
{
    return std::abs(first - second) <= CoordinateComparisonEpsilon;
}
}

MapModel::MapModel(const MapImageryProviderDirectory *provider_directory, QObject *parent)
    : QObject(parent),
      m_provider_directory(provider_directory)
{
}

void MapModel::setOrbitSensitivitySource(const std::function<double()> &source)
{
    this->m_orbit_sensitivity_source = source;
}

void MapModel::providerCatalogChanged()
{
    if (this->m_provider_directory != nullptr
        && !this->m_provider_directory->isProviderEnabled(this->m_provider_id))
    {
        const QString first_enabled = this->m_provider_directory->firstEnabledProviderId();
        if (!first_enabled.isEmpty())
        {
            // setProviderId() emits providerIdChanged() / providerChanged().
            setProviderId(first_enabled);
            return;
        }
    }
    emit providerChanged(this->m_provider);
}

void MapModel::providerFallbackChanged()
{
    emit providerChanged(this->m_provider);
}

MapCameraViewState MapModel::cameraViewState() const
{
    MapCameraViewState state;
    state.mode = this->m_view_mode;
    state.center_lon_deg = this->m_centerLon;
    state.center_lat_deg = this->m_centerLat;
    state.continuous_2d_zoom = view2dContinuousZoom();
    state.globe_yaw_deg = this->m_view_globe_yaw_deg;
    state.globe_pitch_deg = this->m_view_globe_pitch_deg;
    state.globe_distance_m = this->m_view_globe_distance_m;
    state.globe_vertical_offset_m = this->m_view_globe_vertical_offset_m;
    state.globe_pose_revision = this->m_pose_revision;
    state.globe_collision_lift_m = this->m_view_globe_camera_collision_lift_m;
    return state;
}

double MapModel::view2dContinuousZoom() const
{
    return this->m_view_2d_continuous_zoom;
}

double MapModel::centerLon() const
{
    return this->m_centerLon;
}

double MapModel::centerLat() const
{
    return this->m_centerLat;
}

MapProvider MapModel::provider() const
{
    return this->m_provider;
}

QString MapModel::providerId() const
{
    return this->m_provider_id;
}

int MapModel::imageryMaximumZoom() const
{
    if (this->m_provider_directory == nullptr)
        return MaxZoom;
    return qBound(MinZoom, this->m_provider_directory->maximumSourceZoom(this->m_provider_id), MaxZoom);
}

int MapModel::imageryTileZoom(int requested_zoom) const
{
    const QString source = tileSourcePath(requested_zoom);
    int minimum_zoom = 0;
    int maximum_zoom = 0;
    if (this->m_provider_directory == nullptr
        || !this->m_provider_directory->sourceZoomRange(source, &minimum_zoom, &maximum_zoom))
    {
        return MinZoom;
    }
    return qBound(minimum_zoom, requested_zoom, maximum_zoom);
}

MapViewMode MapModel::viewMode() const
{
    return this->m_view_mode;
}

double MapModel::view3dNetworkGroundOffsetM() const
{
    return this->m_view_3d_network_ground_offset_m;
}

double MapModel::view3dVerticalExaggeration() const
{
    return this->m_view_3d_vertical_exaggeration;
}

MapView3dNavigationState MapModel::view3dNavigationState() const
{
    return this->m_view_3d_navigation_state;
}

double MapModel::viewGlobeYawDeg() const
{
    return this->m_view_globe_yaw_deg;
}

double MapModel::viewGlobePitchDeg() const
{
    return this->m_view_globe_pitch_deg;
}

double MapModel::viewGlobeDistanceM() const
{
    return this->m_view_globe_distance_m;
}

double MapModel::viewGlobeVerticalOffsetM() const
{
    return this->m_view_globe_vertical_offset_m;
}

double MapModel::viewGlobeCameraCollisionLiftM() const
{
    return this->m_view_globe_camera_collision_lift_m;
}

double MapModel::viewGlobeDistanceMForZoomLevel(
    double zoom_level, double latitude_deg, int viewport_height_px)
{
    const double latitude_clamped = qBound(
        -GlobeZoomFormulaMaxLatitudeDeg, latitude_deg, GlobeZoomFormulaMaxLatitudeDeg);
    const double circumference_m = 2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM;
    const double meters_per_pixel = circumference_m * std::cos(qDegreesToRadians(latitude_clamped))
        / (double(TileSize) * std::exp2(zoom_level));
    const double tan_half_fov = std::tan(qDegreesToRadians(GlobeFieldOfViewDeg / 2.0));
    const double safe_viewport_height = double(qMax(1, viewport_height_px));
    return safe_viewport_height * meters_per_pixel / (2.0 * tan_half_fov);
}

double MapModel::viewGlobeZoomLevelForDistanceM(
    double distance_m, double latitude_deg, int viewport_height_px)
{
    const double latitude_clamped = qBound(
        -GlobeZoomFormulaMaxLatitudeDeg, latitude_deg, GlobeZoomFormulaMaxLatitudeDeg);
    const double circumference_m = 2.0 * M_PI * GeoWgs84Ellipsoid::EquatorialRadiusM;
    const double tan_half_fov = std::tan(qDegreesToRadians(GlobeFieldOfViewDeg / 2.0));
    const double safe_viewport_height = double(qMax(1, viewport_height_px));
    const double meters_per_pixel_target =
        qMax(1.0, distance_m) * 2.0 * tan_half_fov / safe_viewport_height;
    return std::log2(
        circumference_m * std::cos(qDegreesToRadians(latitude_clamped))
        / (double(TileSize) * meters_per_pixel_target));
}

double MapModel::viewGlobeZoomLevel(const QSize &viewport) const
{
    return viewGlobeZoomLevelForDistanceM(
        this->m_view_globe_distance_m, this->m_centerLat,
        viewport.isValid() ? viewport.height() : GlobeZoomReferenceViewportHeightPx);
}

QPointF MapModel::centerMercator() const
{
    return QPointF(GeoWebMercator::lonToTileX(this->m_centerLon, 0),
                   GeoWebMercator::latToTileY(this->m_centerLat, 0));
}

double MapModel::view2dWorldSizePx() const
{
    return double(TileSize) * std::exp2(this->m_view_2d_continuous_zoom);
}

QString MapModel::tileCachePrefix(int zoom) const
{
    const int bounded_zoom = std::clamp(zoom, MinZoom, MaxZoom);
    return tileSourcePath(bounded_zoom) + QLatin1Char('/') + QString::number(bounded_zoom) + QLatin1Char('/');
}

QString MapModel::tileCacheKey(int x, int y, int zoom) const
{
    // Appended directly: the globe builds a key per window tile on every
    // window rebuild, and QString::arg() parses its pattern per argument.
    const int wrapped_x = GeoWebMercator::wrapTileX(x, zoom);
    return tileCachePrefix(zoom) + QString::number(wrapped_x) + QLatin1Char('/') + QString::number(y);
}

QString MapModel::tileEndpoint(int x, int y, int zoom) const
{
    const int wrapped_x = GeoWebMercator::wrapTileX(x, zoom);

    return QString("/%1/%2/%3/%4.png")
        .arg(tileSourcePath(zoom))
        .arg(zoom)
        .arg(wrapped_x)
        .arg(y);
}

QString MapModel::providerPath() const
{
    return this->m_provider_id;
}

QString MapModel::tileSourcePath(int zoom) const
{
    if (this->m_provider_directory == nullptr)
        return QString();
    return this->m_provider_directory->sourceForZoom(this->m_provider_id, zoom);
}

void MapModel::setView(double lon, double lat, double continuous_zoom, const QSize &viewport)
{
    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const double old_zoom = this->m_view_2d_continuous_zoom;

    this->m_view_2d_continuous_zoom = qBound(double(MinZoom), continuous_zoom, double(MaxZoom));
    this->m_centerLon = GeoWebMercator::normalizeLongitude(lon);

    // The flat 2D view is backed by Web Mercator and therefore cannot
    // represent the polar caps above +-85.05112878 degrees. Globe view is
    // different: its target is geodetic WGS84 converted directly to ECEF,
    // so the full physical latitude range through the true poles is valid.
    //
    // clampCenter() already deliberately skips the Web Mercator extent
    // clamp in Globe mode, but setCenter() comes through setView() first.
    // Keeping the unconditional Mercator clamp here therefore made the
    // globe's quaternion/ECEF pan math *look* pole-limited even though the
    // rotation itself is gimbal-lock safe.
    const double maximum_center_latitude = this->m_view_mode == MapViewMode::Globe
        ? 90.0
        : GeoWebMercator::MaximumLatitude;
    this->m_centerLat = std::clamp(
        lat, -maximum_center_latitude, maximum_center_latitude);

    if (viewport.isValid())
        clampCenter(viewport);

    if (!coordinatesEqual(this->m_view_2d_continuous_zoom, old_zoom))
    {
        MapRenderDiagnostics::add(MapRenderDiagnostics::Counter::ZoomChange);
        emit view2dContinuousZoomChanged(this->m_view_2d_continuous_zoom);
    }
    if (!coordinatesEqual(this->m_centerLon, old_lon) || !coordinatesEqual(this->m_centerLat, old_lat))
        emitCenterChanged();
}

void MapModel::fitViewToBounds(
    const CoordinateWGS84 &minimum, const CoordinateWGS84 &maximum,
    const QSize &viewport, double elevation_minimum_m, double elevation_maximum_m)
{
    constexpr double FitMarginFraction = 0.10;
    constexpr double MinimumWorldExtent = 1.0;

    const QSize safe_viewport = viewport.isValid() ? viewport : QSize(1024, 768);
    const double available_width = qMax(
        1.0, double(safe_viewport.width()) * (1.0 - 2.0 * FitMarginFraction));
    const double available_height = qMax(
        1.0, double(safe_viewport.height()) * (1.0 - 2.0 * FitMarginFraction));

    const QPointF minimum_world = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(minimum.longitude_deg),
        minimum.latitude_deg,
        MapRenderCacheMath::ReferenceZoom);
    const QPointF maximum_world_raw = GeoWebMercator::lonLatToWorldPixel(
        GeoWebMercator::normalizeLongitude(maximum.longitude_deg),
        maximum.latitude_deg,
        MapRenderCacheMath::ReferenceZoom);
    const double maximum_world_x = GeoWebMercator::nearestWrappedWorldPixelX(
        maximum_world_raw.x(), minimum_world.x(), MapRenderCacheMath::ReferenceZoom);

    const double left_world = qMin(minimum_world.x(), maximum_world_x);
    const double right_world = qMax(minimum_world.x(), maximum_world_x);
    const double top_world = qMin(minimum_world.y(), maximum_world_raw.y());
    const double bottom_world = qMax(minimum_world.y(), maximum_world_raw.y());
    const double width_world = right_world - left_world;
    const double height_world = bottom_world - top_world;

    const QPointF center_world(
        (left_world + right_world) * 0.5,
        (top_world + bottom_world) * 0.5);
    const CoordinateWGS84 center = GeoWebMercator::worldPixelToLonLat(
        center_world.x(), center_world.y(), MapRenderCacheMath::ReferenceZoom);

    double fit_scale = std::numeric_limits<double>::infinity();
    if (width_world > 1e-9)
        fit_scale = qMin(fit_scale, available_width / width_world);
    if (height_world > 1e-9)
        fit_scale = qMin(fit_scale, available_height / height_world);
    if (!std::isfinite(fit_scale) || fit_scale <= 0.0)
        fit_scale = GeoWebMercator::zoomScale(MaxZoom, MapRenderCacheMath::ReferenceZoom);

    const double continuous_zoom = qBound(
        double(MinZoom),
        double(MapRenderCacheMath::ReferenceZoom) + std::log2(fit_scale),
        double(MaxZoom));
    setView(center.longitude_deg, center.latitude_deg, continuous_zoom, safe_viewport);

    if (this->m_view_mode == MapViewMode::Globe)
    {
        const double meters_per_world_pixel = GeoWebMercator::metersPerPixel(
            center.latitude_deg, MapRenderCacheMath::ReferenceZoom);
        if (!std::isfinite(meters_per_world_pixel) || meters_per_world_pixel <= 0.0)
            return;

        // Fit the same network bounds used by the 2D view into the
        // Globe perspective camera. A bounding sphere keeps the complete
        // network visible at any current yaw/pitch instead of only matching
        // the top-down Mercator zoom.
        const double horizontal_radius_m = 0.5 * meters_per_world_pixel * std::hypot(
            qMax(MinimumWorldExtent, width_world),
            qMax(MinimumWorldExtent, height_world));
        const double elevation_span_m =
            std::isfinite(elevation_minimum_m) && std::isfinite(elevation_maximum_m)
                ? std::abs(elevation_maximum_m - elevation_minimum_m)
                : 0.0;
        const double vertical_radius_m =
            elevation_span_m * this->m_view_3d_vertical_exaggeration;
        const double bounding_radius_m = qMax(
            meters_per_world_pixel * MinimumWorldExtent,
            std::hypot(horizontal_radius_m, vertical_radius_m));

        const double half_vertical_fov_rad =
            qDegreesToRadians(GlobeFieldOfViewDeg / 2.0);
        const double aspect = qMax(
            1e-6, double(safe_viewport.width()) / double(qMax(1, safe_viewport.height())));
        const double usable_vertical_tangent = std::tan(half_vertical_fov_rad)
            * available_height / double(qMax(1, safe_viewport.height()));
        const double usable_horizontal_tangent = std::tan(half_vertical_fov_rad)
            * aspect * available_width / double(qMax(1, safe_viewport.width()));
        const double limiting_half_angle = qMin(
            std::atan(usable_vertical_tangent),
            std::atan(usable_horizontal_tangent));
        const double safe_half_angle = qMax(qDegreesToRadians(1.0), limiting_half_angle);
        const double desired_distance_m =
            bounding_radius_m / std::sin(safe_half_angle);

        setViewGlobeDistanceM(desired_distance_m);
    }
}

void MapModel::setCenter(double lon, double lat, const QSize &viewport)
{
    setView(lon, lat, this->m_view_2d_continuous_zoom, viewport);
}

void MapModel::setView2dContinuousZoom(double continuous_zoom, const QSize &viewport)
{
    if (!std::isfinite(continuous_zoom))
        return;
    setView(this->m_centerLon, this->m_centerLat, continuous_zoom, viewport);
}

void MapModel::zoomByAt(double steps, const QPointF &anchorPos, const QSize &viewport)
{
    if (!viewport.isValid() || !std::isfinite(steps) || coordinatesEqual(steps, 0.0))
        return;

    const double old_continuous_zoom = this->m_view_2d_continuous_zoom;
    const double new_continuous_zoom = qBound(
        double(MinZoom), old_continuous_zoom + steps, double(MaxZoom));
    if (coordinatesEqual(new_continuous_zoom, old_continuous_zoom))
        return;

    // The map point under the anchor stays under it: anchor in normalised
    // Mercator at the old world size, new centre at the new world size.
    const QPointF center = centerMercator();
    const double anchor_offset_x_px = anchorPos.x() - viewport.width() / 2.0;
    const double anchor_offset_y_px = anchorPos.y() - viewport.height() / 2.0;
    const double old_world_size_px = view2dWorldSizePx();
    const double anchor_x = center.x() + anchor_offset_x_px / old_world_size_px;
    const double anchor_y = center.y() + anchor_offset_y_px / old_world_size_px;
    const double new_world_size_px = double(TileSize) * std::exp2(new_continuous_zoom);

    setView(GeoWebMercator::tileXToLon(anchor_x - anchor_offset_x_px / new_world_size_px, 0),
            GeoWebMercator::tileYToLat(anchor_y - anchor_offset_y_px / new_world_size_px, 0),
            new_continuous_zoom, viewport);
}

void MapModel::panByPixels(const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull())
        return;

    const QPointF center = centerMercator();
    const double world_size_px = view2dWorldSizePx();
    setCenter(
        GeoWebMercator::tileXToLon(center.x() - double(delta.x()) / world_size_px, 0),
        GeoWebMercator::tileYToLat(center.y() - double(delta.y()) / world_size_px, 0),
        viewport);
}

double MapModel::globeFieldOfViewDeg() const
{
    if (!this->m_eye_pose)
        return GlobeFieldOfViewDeg;
    const double tangent = std::tan(qDegreesToRadians(GlobeFieldOfViewDeg * 0.5));
    return 2.0 * qRadiansToDegrees(std::atan(tangent / this->m_eye_pose->magnification));
}

void MapModel::zoomGlobeEyeBy(double steps)
{
    if (!this->m_eye_pose || !std::isfinite(steps))
        return;
    MapGlobeEyePose pose = *this->m_eye_pose;
    const double scale = std::clamp(pose.magnification * std::exp2(steps), 0.5, 8.0);
    if (scale != pose.magnification)
    {
        pose.magnification = scale;
        setGlobeEyePose(pose);
    }
}

void MapModel::setGlobeEyePose(const std::optional<MapGlobeEyePose> &pose)
{
    this->m_eye_pose = pose;
    ++this->m_pose_revision;
    emit viewGlobeCameraChanged();
}

bool MapModel::globeScreenRay(
    const QPointF &screen_position, const QSize &viewport, MapGlobeScreenRay *ray) const
{
    // The rendered camera's ray (MapGlobeCamera::screenRay()), eye in double
    // precision: an Earth-scale float eye is quantised by about half a metre.
    MapGlobeScreenRayParameters parameters;
    parameters.eye_pose = this->m_eye_pose;
    parameters.target_lon_deg = this->m_centerLon;
    parameters.target_lat_deg = this->m_centerLat;
    parameters.yaw_deg = this->m_view_globe_yaw_deg;
    parameters.pitch_deg = qBound(
        MinViewGlobeFreeLookPitchDeg, this->m_view_globe_pitch_deg, MaxViewGlobePitchDeg);
    parameters.distance_m = qMax(MinViewGlobeDistanceM, this->m_view_globe_distance_m);
    parameters.target_height_m = this->m_view_globe_vertical_offset_m;
    parameters.collision_lift_m = this->m_view_globe_camera_collision_lift_m;
    parameters.field_of_view_deg = globeFieldOfViewDeg();
    return mapGlobeBuildScreenRay(screen_position, viewport, parameters, ray);
}

bool MapModel::globeCoordinateAtScreen(
    const QPoint &screen_position, const QSize &viewport, CoordinateWGS84 *coordinate) const
{
    MapGlobeScreenRay ray;
    GeoWgs84Ellipsoid::EcefPositionD point;
    double distance_m = 0.0;
    if (!globeScreenRay(QPointF(screen_position), viewport, &ray)
        || !GeoWgs84Ellipsoid::rayIntersection(ray.origin_ecef, ray.direction, &point, &distance_m))
    {
        return false;
    }

    double lon_deg = 0.0;
    double lat_deg = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(point, &lon_deg, &lat_deg))
        return false;

    coordinate->longitude_deg = lon_deg;
    coordinate->latitude_deg = lat_deg;
    return true;
}

bool MapModel::globeSpherePointAtScreen(
    const QPointF &screen_position, const QSize &viewport, double radius_m,
    GeoWgs84Ellipsoid::EcefPositionD *point) const
{
    MapGlobeScreenRay ray;
    if (!globeScreenRay(screen_position, viewport, &ray))
        return false;

    const GeoWgs84Ellipsoid::EcefPositionD &origin = ray.origin_ecef;
    const double direction_x = double(ray.direction.x());
    const double direction_y = double(ray.direction.y());
    const double direction_z = double(ray.direction.z());
    const double origin_along = origin.x * direction_x + origin.y * direction_y + origin.z * direction_z;
    const double origin_length_squared = origin.x * origin.x + origin.y * origin.y + origin.z * origin.z;
    const double discriminant = origin_along * origin_along - (origin_length_squared - radius_m * radius_m);
    double distance_m = -origin_along;
    if (discriminant >= 0.0)
    {
        // The nearer forward hit; the farther one when the eye is inside the
        // sphere (low in a valley below the crosshair's terrain).
        const double root = std::sqrt(discriminant);
        distance_m = -origin_along - root > 0.0 ? -origin_along - root : -origin_along + root;
    }
    if (!(distance_m > 0.0))
        return false;
    point->x = origin.x + direction_x * distance_m;
    point->y = origin.y + direction_y * distance_m;
    point->z = origin.z + direction_z * distance_m;
    if (discriminant < 0.0)
    {
        // A ray past the sphere (beyond the horizon): its closest approach,
        // lifted onto the sphere, so a drag over the globe's edge goes on
        // along the silhouette instead of stopping.
        const double scale = radius_m / ecefLength(*point);
        point->x *= scale;
        point->y *= scale;
        point->z *= scale;
    }
    return true;
}

void MapModel::panByPixelsGlobe(const QPoint &delta, const QSize &viewport, double crosshair_radius_m)
{
    if (delta.isNull() || !viewport.isValid())
        return;

    // The ground under the crosshair moves by delta pixels: the points at
    // the crosshair minus delta and at the crosshair, both on the sphere
    // through the drawn surface at the crosshair (centred on the Earth's
    // centre), are turned onto each other. The speed thus follows the
    // terrain at the crosshair (its height, how far away it is, how steeply
    // it is seen), not a fixed angle per pixel; and two rays a few pixels
    // apart never land on unrelated relief (a near ridge, a far valley).
    // Without a drawn surface the crosshair's ellipsoid point gives the
    // sphere.
    const QPointF crosshair(viewport.width() * 0.5, viewport.height() * 0.5);
    double radius_m = crosshair_radius_m;
    if (!(radius_m > 0.0))
    {
        MapGlobeScreenRay ray;
        GeoWgs84Ellipsoid::EcefPositionD crosshair_point;
        double distance_m = 0.0;
        if (!globeScreenRay(crosshair, viewport, &ray)
            || !GeoWgs84Ellipsoid::rayIntersection(ray.origin_ecef, ray.direction, &crosshair_point,
                                                   &distance_m))
        {
            return;
        }
        radius_m = ecefLength(crosshair_point);
    }

    GeoWgs84Ellipsoid::EcefPositionD previous_point;
    GeoWgs84Ellipsoid::EcefPositionD new_point;
    if (!globeSpherePointAtScreen(crosshair - QPointF(delta), viewport, radius_m, &previous_point)
        || !globeSpherePointAtScreen(crosshair, viewport, radius_m, &new_point))
    {
        return;
    }
    // The camera rig turns from the crosshair's point to the grabbed one:
    // the same as the globe turning the other way under a fixed camera.
    const GlobeRotation rotation = globeRotationBetween(new_point, previous_point);
    rotateGlobe(rotation.axis, rotation.angle_rad);
}

void MapModel::rotateGlobe(const GeoWgs84Ellipsoid::EcefPositionD &axis, double angle_rad)
{
    if (!(std::abs(angle_rad) > 0.0))
        return;
    GlobeRotation rotation;
    rotation.axis = axis;
    rotation.angle_rad = angle_rad;

    // The camera rig (target, screen-right direction) turned rigidly about
    // the Earth's centre, in double precision, then expressed as target
    // longitude/latitude and yaw again. A rigid turn has no preferred axis,
    // so it runs through and across the poles without a jump: the
    // longitude may swing by 180 degrees there, and the yaw taken from the
    // turned screen-right direction swings with it.
    const GeoWgs84Ellipsoid::EcefPositionD target =
        GeoWgs84Ellipsoid::geodeticToEcefD(this->m_centerLon, this->m_centerLat, 0.0);
    const GeoWgs84Ellipsoid::LocalFrame frame =
        GeoWgs84Ellipsoid::localFrameAtGeodetic(this->m_centerLon, this->m_centerLat, 0.0);
    const double yaw_rad = qDegreesToRadians(this->m_view_globe_yaw_deg);
    GeoWgs84Ellipsoid::EcefPositionD right;
    right.x = double(frame.east.x()) * std::cos(yaw_rad) + double(frame.north.x()) * std::sin(yaw_rad);
    right.y = double(frame.east.y()) * std::cos(yaw_rad) + double(frame.north.y()) * std::sin(yaw_rad);
    right.z = double(frame.east.z()) * std::cos(yaw_rad) + double(frame.north.z()) * std::sin(yaw_rad);

    double next_lon_deg = 0.0;
    double next_lat_deg = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(rotatedEcef(rotation, target), &next_lon_deg, &next_lat_deg))
        return;
    next_lon_deg = GeoWebMercator::normalizeLongitude(next_lon_deg);
    next_lat_deg = std::clamp(next_lat_deg, -90.0, 90.0);

    const GeoWgs84Ellipsoid::EcefPositionD turned_right = rotatedEcef(rotation, right);
    const GeoWgs84Ellipsoid::LocalFrame next_frame =
        GeoWgs84Ellipsoid::localFrameAtGeodetic(next_lon_deg, next_lat_deg, 0.0);
    const double right_east = turned_right.x * double(next_frame.east.x())
        + turned_right.y * double(next_frame.east.y()) + turned_right.z * double(next_frame.east.z());
    const double right_north = turned_right.x * double(next_frame.north.x())
        + turned_right.y * double(next_frame.north.y()) + turned_right.z * double(next_frame.north.z());
    const double rigid_yaw_deg = normalizedYawDegrees(qRadiansToDegrees(std::atan2(right_north, right_east)));

    double next_yaw_deg = rigid_yaw_deg;
    if (this->m_view_globe_north_up_locked)
    {
        // North-up holds the yaw against the small heading drift of a rigid
        // turn over the curved surface. Near a pole north swings around the
        // target with every step (a pass beside the pole would spin the view
        // about the crosshair), and across one it points backwards: there
        // the lock is released and the turn stays rigid; the compass
        // restores north up.
        const double hold_yaw_rad = qDegreesToRadians(this->m_view_globe_yaw_deg);
        const double hold_right_east = std::cos(hold_yaw_rad);
        const double hold_right_north = std::sin(hold_yaw_rad);
        const bool hold = std::max(std::abs(this->m_centerLat), std::abs(next_lat_deg))
                <= GlobeNorthUpMaximumLatitudeDeg
            && hold_right_east * right_east + hold_right_north * right_north > 0.0;
        if (hold)
            next_yaw_deg = this->m_view_globe_yaw_deg;
        else
            this->m_view_globe_north_up_locked = false;
    }

    // Update center and yaw as one state change before emitting either signal.
    // Emitting centerChanged first and fixing yaw afterwards would still expose
    // one transient frame in the arbitrary polar tangent basis.
    const bool center_changed =
        !coordinatesEqual(next_lon_deg, this->m_centerLon)
        || !coordinatesEqual(next_lat_deg, this->m_centerLat);
    const bool yaw_changed =
        !coordinatesEqual(next_yaw_deg, this->m_view_globe_yaw_deg);
    if (!center_changed && !yaw_changed)
        return;

    this->m_centerLon = next_lon_deg;
    this->m_centerLat = next_lat_deg;
    this->m_view_globe_yaw_deg = next_yaw_deg;

    if (center_changed)
        emitCenterChanged();
    if (yaw_changed)
        emit viewGlobeCameraChanged();
}

void MapModel::panByPixelsGlobeKeyboard(
    const QPoint &delta, const QSize &viewport)
{
    if (delta.isNull() || !viewport.isValid())
        return;

    // Keyboard movement must not inherit the grazing-angle amplification of
    // a ground/terrain ray cast. Derive a pitch-independent physical scale
    // from the orbit distance and vertical FOV instead, then rotate the
    // requested screen movement by yaw into the local east/north tangent
    // plane. Keyboard Globe panning deliberately stays pitch-independent.
    const double safe_height = qMax(1, viewport.height());
    const double half_fov_rad = qDegreesToRadians(GlobeFieldOfViewDeg * 0.5);
    const double distance_m = qMax(
        MinViewGlobeDistanceM, this->m_view_globe_distance_m);
    const double meters_per_pixel =
        2.0 * distance_m * std::tan(half_fov_rad) / safe_height;
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
        return;

    const double yaw_rad = qDegreesToRadians(
        normalizedYawDegrees(this->m_view_globe_yaw_deg));
    const double screen_right_east = std::cos(yaw_rad);
    const double screen_right_north = std::sin(yaw_rad);
    const double screen_up_east = -std::sin(yaw_rad);
    const double screen_up_north = std::cos(yaw_rad);

    const double east_m = meters_per_pixel * (
        -screen_right_east * double(delta.x())
        + screen_up_east * double(delta.y()));
    const double north_m = meters_per_pixel * (
        -screen_right_north * double(delta.x())
        + screen_up_north * double(delta.y()));

    // The same rigid turn as a drag (rotateGlobe()), so keys run over the
    // poles too: the target moves along the great circle heading east_m /
    // north_m, about the axis normal to its direction and that heading.
    const GeoWgs84Ellipsoid::LocalFrame frame =
        GeoWgs84Ellipsoid::localFrameAtGeodetic(this->m_centerLon, this->m_centerLat, 0.0);
    GeoWgs84Ellipsoid::EcefPositionD heading;
    heading.x = double(frame.east.x()) * east_m + double(frame.north.x()) * north_m;
    heading.y = double(frame.east.y()) * east_m + double(frame.north.y()) * north_m;
    heading.z = double(frame.east.z()) * east_m + double(frame.north.z()) * north_m;
    const GeoWgs84Ellipsoid::EcefPositionD target =
        GeoWgs84Ellipsoid::geodeticToEcefD(this->m_centerLon, this->m_centerLat, 0.0);
    rotateGlobe(globeRotationBetween(target, heading).axis,
                std::hypot(east_m, north_m) / ecefLength(target));
}

void MapModel::clampCenter(const QSize &viewport)
{
    if (!viewport.isValid())
        return;

    // This clamp exists to keep the flat 2D map's on-screen extent within
    // Web Mercator's own representable range (it round-trips centerLat
    // through latToTileY()/tileYToLat(), both of which clamp internally to
    // +-GeoWebMercator::MaximumLatitude, i.e. ~85.05 degrees). The Globe
    // camera has no such limit -- it looks directly at the WGS84 ellipsoid
    // via ECEF math, which is perfectly well-defined all the way to the
    // true poles (90 degrees) -- so applying this here would silently stop
    // any attempt to pan the globe to a real polar latitude a few degrees
    // short of the actual pole, for no reason that applies to Globe at all.
    if (this->m_view_mode == MapViewMode::Globe)
        return;

    // In normalised Mercator the world is 0..1 high: keep the viewport
    // inside it, or centre the world when the viewport is taller.
    const double half_viewport_height = viewport.height() / view2dWorldSizePx() / 2.0;
    double center_y = GeoWebMercator::latToTileY(this->m_centerLat, 0);
    if (half_viewport_height >= 0.5)
        center_y = 0.5;
    else
        center_y = std::clamp(center_y, half_viewport_height, 1.0 - half_viewport_height);

    this->m_centerLat = GeoWebMercator::tileYToLat(center_y, 0);
}

void MapModel::setProvider(MapProvider provider)
{
    if (provider == MapProvider::Custom || this->m_provider_directory == nullptr)
        return;
    setProviderId(this->m_provider_directory->builtinProviderId(provider));
}

void MapModel::setProviderId(const QString &provider_id)
{
    if (provider_id.isEmpty() || this->m_provider_id == provider_id
        || this->m_provider_directory == nullptr
        || !this->m_provider_directory->isProviderEnabled(provider_id))
    {
        return;
    }
    this->m_provider_id = provider_id;
    this->m_provider = MapProvider::Custom;
    for (int number = 1; number <= 4; ++number)
    {
        if (provider_id == this->m_provider_directory->builtinProviderId(MapProvider(number)))
        {
            this->m_provider = MapProvider(number);
            break;
        }
    }
    emit providerIdChanged(this->m_provider_id);
    emit providerChanged(this->m_provider);
}

void MapModel::setViewMode(MapViewMode view_mode, const QSize &viewport)
{
    if (this->m_view_mode == view_mode)
        return;

    // A view mode switch starts the globe as an orbit again.
    setViewGlobeCameraMode(MapGlobeCameraMode::Orbit);

    const MapViewMode previous_view_mode = this->m_view_mode;
    const double old_latitude = this->m_centerLat;
    const int viewport_height_px = viewport.isValid()
        ? qMax(1, viewport.height())
        : GlobeZoomReferenceViewportHeightPx;

    bool globe_distance_changed = false;
    double transferred_2d_zoom = std::numeric_limits<double>::quiet_NaN();

    // 2D and Globe use different native camera representations, but a view
    // mode switch must not look like an arbitrary zoom jump. Transfer the
    // current screen scale before changing mode so the first Globe frame is
    // already at the same 2D-equivalent zoom level.
    if (previous_view_mode == MapViewMode::TwoD && view_mode == MapViewMode::Globe)
    {
        const double next_distance_m = qBound(
            MinViewGlobeDistanceM,
            viewGlobeDistanceMForZoomLevel(
                view2dContinuousZoom(), this->m_centerLat, viewport_height_px),
            MaxViewGlobeDistanceM);
        globe_distance_changed = !coordinatesEqual(
            next_distance_m, this->m_view_globe_distance_m);
        this->m_view_globe_distance_m = next_distance_m;
    }
    else if (previous_view_mode == MapViewMode::Globe
             && view_mode == MapViewMode::TwoD)
    {
        transferred_2d_zoom = viewGlobeZoomLevelForDistanceM(
            this->m_view_globe_distance_m, this->m_centerLat, viewport_height_px);
    }

    this->m_eye_pose.reset();
    this->m_view_mode = view_mode;

    // A Globe center may legitimately sit anywhere up to the true poles.
    // When returning to a flat Web Mercator view, bring that shared center
    // back into the projection's representable latitude range immediately
    // instead of leaving a +-90 degree value in flat-map state.
    if (this->m_view_mode != MapViewMode::Globe)
    {
        this->m_centerLat = std::clamp(
            this->m_centerLat,
            -GeoWebMercator::MaximumLatitude,
            GeoWebMercator::MaximumLatitude);
    }

    if (this->m_view_mode != MapViewMode::Globe
        && this->m_view_3d_navigation_state == MapView3dNavigationState::Rotate)
    {
        this->m_view_3d_rotate_interaction_depth = 0;
        this->m_view_3d_navigation_state = MapView3dNavigationState::Pan;
        emit view3dNavigationStateChanged(this->m_view_3d_navigation_state);
    }

    emit viewModeChanged(this->m_view_mode);

    if (std::isfinite(transferred_2d_zoom))
        setView2dContinuousZoom(transferred_2d_zoom, viewport);

    if (globe_distance_changed)
        emit viewGlobeCameraChanged();

    if (!coordinatesEqual(this->m_centerLat, old_latitude))
        emitCenterChanged();
}

void MapModel::setView3dNetworkGroundOffsetM(double offset_m)
{
    if (!std::isfinite(offset_m))
        return;

    const double bounded_offset_m = qBound(
        MinView3dNetworkGroundOffsetM,
        offset_m,
        MaxView3dNetworkGroundOffsetM);
    if (coordinatesEqual(bounded_offset_m, this->m_view_3d_network_ground_offset_m))
        return;

    this->m_view_3d_network_ground_offset_m = bounded_offset_m;
    emit view3dNetworkGroundOffsetChanged(bounded_offset_m);
}

void MapModel::setView3dVerticalExaggeration(double exaggeration)
{
    if (!std::isfinite(exaggeration))
        return;

    const double bounded_exaggeration = qBound(
        MinView3dVerticalExaggeration,
        exaggeration,
        MaxView3dVerticalExaggeration);
    if (coordinatesEqual(bounded_exaggeration, this->m_view_3d_vertical_exaggeration))
        return;

    this->m_view_3d_vertical_exaggeration = bounded_exaggeration;
    emit view3dVerticalExaggerationChanged(bounded_exaggeration);
}

void MapModel::beginView3dRotateInteraction()
{
    // This generic 3D interaction state belongs exclusively to Globe now.
    if (this->m_view_mode != MapViewMode::Globe)
        return;

    ++this->m_view_3d_rotate_interaction_depth;
    if (this->m_view_3d_navigation_state == MapView3dNavigationState::Rotate)
        return;

    this->m_view_3d_navigation_state = MapView3dNavigationState::Rotate;
    emit view3dNavigationStateChanged(this->m_view_3d_navigation_state);
}

void MapModel::endView3dRotateInteraction()
{
    if (this->m_view_3d_rotate_interaction_depth <= 0)
        return;

    --this->m_view_3d_rotate_interaction_depth;
    if (this->m_view_3d_rotate_interaction_depth > 0)
        return;

    if (this->m_view_3d_navigation_state == MapView3dNavigationState::Pan)
        return;

    this->m_view_3d_navigation_state = MapView3dNavigationState::Pan;
    emit view3dNavigationStateChanged(this->m_view_3d_navigation_state);
}

void MapModel::setViewGlobeYawDeg(double yaw_deg)
{
    const double next_yaw = normalizedYawDegrees(yaw_deg);

    // Setting an exact north-up heading is also the explicit north-up lock
    // used by the globe compass. Any other absolute heading releases it.
    // Update the lock even when the numeric yaw is already unchanged so a
    // compass click at 0 degrees can re-enable north-up panning.
    this->m_view_globe_north_up_locked = coordinatesEqual(next_yaw, 0.0);

    if (coordinatesEqual(next_yaw, this->m_view_globe_yaw_deg))
    {
        // Keep the stored north heading exact as well; the comparison epsilon
        // should not leave a tiny residual yaw behind after a compass reset.
        this->m_view_globe_yaw_deg = next_yaw;
        return;
    }

    this->m_view_globe_yaw_deg = next_yaw;
    emit viewGlobeCameraChanged();
}

void MapModel::setViewGlobePitchDeg(double pitch_deg)
{
    const double next_pitch = qBound(
        viewGlobeMinPitchDeg(), pitch_deg, MaxViewGlobePitchDeg);
    if (coordinatesEqual(next_pitch, this->m_view_globe_pitch_deg))
        return;

    this->m_view_globe_pitch_deg = next_pitch;
    emit viewGlobeCameraChanged();
}

void MapModel::setViewGlobeDistanceM(double distance_m)
{
    if (!std::isfinite(distance_m))
        return;

    const double next_distance = qBound(
        MinViewGlobeDistanceM, distance_m, MaxViewGlobeDistanceM);
    if (coordinatesEqual(next_distance, this->m_view_globe_distance_m))
        return;

    this->m_view_globe_distance_m = next_distance;
    emit viewGlobeCameraChanged();
}

void MapModel::setViewGlobeCameraCollisionLiftM(double lift_m)
{
    setViewGlobeTerrainHeightOffsetsM(
        this->m_view_globe_vertical_offset_m, lift_m);
}

void MapModel::setViewGlobeTerrainHeightOffsetsM(
    double vertical_offset_m, double camera_collision_lift_m)
{
    if (!std::isfinite(vertical_offset_m)
        || !std::isfinite(camera_collision_lift_m))
    {
        return;
    }

    const double next_collision_lift_m = qMax(
        0.0, camera_collision_lift_m);
    const bool vertical_offset_changed = !coordinatesEqual(
        vertical_offset_m, this->m_view_globe_vertical_offset_m);
    const bool collision_lift_changed = !coordinatesEqual(
        next_collision_lift_m,
        this->m_view_globe_camera_collision_lift_m);
    if (!vertical_offset_changed && !collision_lift_changed)
        return;

    this->m_view_globe_vertical_offset_m = vertical_offset_m;
    this->m_view_globe_camera_collision_lift_m =
        next_collision_lift_m;
    emit viewGlobeTerrainHeightChanged();
}

void MapModel::setViewGlobeZoomLevel(double zoom_level, const QSize &viewport)
{
    if (!std::isfinite(zoom_level))
        return;

    const double distance_m = viewGlobeDistanceMForZoomLevel(
        zoom_level, this->m_centerLat,
        viewport.isValid() ? viewport.height() : GlobeZoomReferenceViewportHeightPx);
    setViewGlobeDistanceM(distance_m);
}

void MapModel::setViewGlobeFocusAnchor(
    double lon, double lat, double vertical_offset_m,
    double yaw_deg, double pitch_deg, double distance_m,
    double camera_collision_lift_m, const QSize &viewport)
{
    if (!std::isfinite(lon) || !std::isfinite(lat)
        || !std::isfinite(vertical_offset_m) || !std::isfinite(yaw_deg)
        || !std::isfinite(pitch_deg) || !std::isfinite(distance_m)
        || !std::isfinite(camera_collision_lift_m))
    {
        return;
    }

    const double old_lon = this->m_centerLon;
    const double old_lat = this->m_centerLat;
    const double old_vertical_offset_m = this->m_view_globe_vertical_offset_m;
    const double old_yaw_deg = this->m_view_globe_yaw_deg;
    const double old_pitch_deg = this->m_view_globe_pitch_deg;
    const double old_distance_m = this->m_view_globe_distance_m;
    const double old_collision_lift_m =
        this->m_view_globe_camera_collision_lift_m;

    this->m_centerLon = GeoWebMercator::normalizeLongitude(lon);
    this->m_centerLat = std::clamp(lat, -90.0, 90.0);
    if (viewport.isValid())
        clampCenter(viewport);

    this->m_view_globe_vertical_offset_m = vertical_offset_m;
    this->m_view_globe_yaw_deg = normalizedYawDegrees(yaw_deg);
    this->m_view_globe_pitch_deg = qBound(
        viewGlobeMinPitchDeg(), pitch_deg, MaxViewGlobePitchDeg);
    this->m_view_globe_distance_m = qBound(
        MinViewGlobeDistanceM, distance_m, MaxViewGlobeDistanceM);
    this->m_view_globe_camera_collision_lift_m = qMax(
        0.0, camera_collision_lift_m);

    const bool center_changed = !coordinatesEqual(this->m_centerLon, old_lon)
        || !coordinatesEqual(this->m_centerLat, old_lat);
    const bool camera_changed = !coordinatesEqual(
        this->m_view_globe_vertical_offset_m, old_vertical_offset_m)
        || !coordinatesEqual(this->m_view_globe_yaw_deg, old_yaw_deg)
        || !coordinatesEqual(this->m_view_globe_pitch_deg, old_pitch_deg)
        || !coordinatesEqual(this->m_view_globe_distance_m, old_distance_m)
        || !coordinatesEqual(
            this->m_view_globe_camera_collision_lift_m,
            old_collision_lift_m);

    if (center_changed)
        emitCenterChanged();
    if (camera_changed)
        emit viewGlobeCameraChanged();
}

void MapModel::orbitViewGlobe(double yaw_delta_deg, double pitch_delta_deg)
{
    const double next_yaw = normalizedYawDegrees(this->m_view_globe_yaw_deg + yaw_delta_deg);
    const double next_pitch = qBound(
        viewGlobeMinPitchDeg(),
        this->m_view_globe_pitch_deg + pitch_delta_deg,
        MaxViewGlobePitchDeg);

    // Any deliberate yaw rotation leaves north-up mode. Pitch-only orbiting
    // does not, so the user can tilt the globe while keeping north locked.
    if (!coordinatesEqual(yaw_delta_deg, 0.0))
        this->m_view_globe_north_up_locked = false;

    if (coordinatesEqual(next_yaw, this->m_view_globe_yaw_deg)
        && coordinatesEqual(next_pitch, this->m_view_globe_pitch_deg))
    {
        return;
    }

    this->m_view_globe_yaw_deg = next_yaw;
    this->m_view_globe_pitch_deg = next_pitch;
    emit viewGlobeCameraChanged();
}

void MapModel::orbitViewGlobeByPointerDelta(const QPoint &delta_pixels, bool include_pitch)
{
    const double sensitivity = this->m_orbit_sensitivity_source
        ? this->m_orbit_sensitivity_source()
        : 1.0;
    // In free look the pointer-driven turns (HUD compass drag, joystick)
    // turn the view about the eye as well; Ctrl / middle-button orbits
    // switch to Orbit before their first delta.
    if (this->m_view_globe_camera_mode == MapGlobeCameraMode::FreeLook)
    {
        if (this->m_view_mode != MapViewMode::Globe || this->m_eye_pose || delta_pixels.isNull())
            return;
        lookViewGlobe(
            qDegreesToRadians(double(delta_pixels.x()) * ViewGlobeLookDegreesPerPixel * sensitivity),
            include_pitch
                ? -qDegreesToRadians(double(delta_pixels.y()) * ViewGlobeLookDegreesPerPixel * sensitivity)
                : 0.0,
            std::numeric_limits<double>::quiet_NaN());
        return;
    }
    const double yaw_delta_deg =
        double(delta_pixels.x()) * ViewGlobeOrbitYawDegreesPerPixel * sensitivity;
    const double pitch_delta_deg = include_pitch
        ? double(-delta_pixels.y()) * ViewGlobeOrbitPitchDegreesPerPixel * sensitivity
        : 0.0;
    orbitViewGlobe(yaw_delta_deg, pitch_delta_deg);
}

namespace
{
// GeographicLib's local frame at a geodetic point (the rotation columns of
// GeoWgs84Ellipsoid::orbitCameraEyeEcefD()): east, north, ellipsoid normal.
struct LocalEnuD
{
    double east[3] = {0.0, 0.0, 0.0};
    double north[3] = {0.0, 0.0, 0.0};
    double up[3] = {0.0, 0.0, 0.0};
};

LocalEnuD localEnuAt(double lon_deg, double lat_deg)
{
    const double lon_rad = qDegreesToRadians(lon_deg);
    const double lat_rad = qDegreesToRadians(lat_deg);
    LocalEnuD frame;
    frame.east[0] = -std::sin(lon_rad);
    frame.east[1] = std::cos(lon_rad);
    frame.east[2] = 0.0;
    frame.north[0] = -std::sin(lat_rad) * std::cos(lon_rad);
    frame.north[1] = -std::sin(lat_rad) * std::sin(lon_rad);
    frame.north[2] = std::cos(lat_rad);
    frame.up[0] = std::cos(lat_rad) * std::cos(lon_rad);
    frame.up[1] = std::cos(lat_rad) * std::sin(lon_rad);
    frame.up[2] = std::sin(lat_rad);
    return frame;
}

double dot3(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Smallest WGS84 radius of curvature (meridional, at the equator:
// a * (1 - e^2)). The normal of a point `d` ahead is turned up to d / R
// against the eye's; the smallest R gives the safe bound.
constexpr double MinimumWgs84CurvatureRadiusM = 6335439.0;
}

bool MapModel::setViewGlobeOrbitEyeAndTarget(const GeoWgs84Ellipsoid::EcefPositionD &eye,
                                             const GeoWgs84Ellipsoid::EcefPositionD &target)
{
    if (this->m_view_mode != MapViewMode::Globe || this->m_eye_pose)
        return false;

    double lon_deg = 0.0;
    double lat_deg = 0.0;
    double height_m = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(target, &lon_deg, &lat_deg, &height_m))
        return false;
    const LocalEnuD frame = localEnuAt(lon_deg, lat_deg);

    // Eye relative to the target, without the eye-only lift.
    const double lift_m = qMax(0.0, this->m_view_globe_camera_collision_lift_m);
    const double relative[3] = {
        eye.x - target.x - frame.up[0] * lift_m,
        eye.y - target.y - frame.up[1] * lift_m,
        eye.z - target.z - frame.up[2] * lift_m};
    const double east_m = dot3(relative, frame.east);
    const double north_m = dot3(relative, frame.north);
    const double up_m = dot3(relative, frame.up);

    const double horizontal_m = std::hypot(east_m, north_m);
    const double distance_m = std::sqrt(horizontal_m * horizontal_m + up_m * up_m);
    if (!std::isfinite(distance_m) || !(horizontal_m > 0.0))
        return false;
    // orbitCameraEyeEcefD(): eye horizontal = east*sin(yaw) - north*cos(yaw).
    const double pitch_deg = qRadiansToDegrees(std::atan2(up_m, horizontal_m));
    const double yaw_deg = qRadiansToDegrees(std::atan2(east_m, -north_m));
    if (pitch_deg < viewGlobeMinPitchDeg() || pitch_deg > MaxViewGlobePitchDeg
        || distance_m < MinViewGlobeDistanceM || distance_m > MaxViewGlobeDistanceM)
    {
        return false;
    }

    // No viewport: the anchor is a point on the drawn globe, no 2D extent
    // clamp may move it.
    // Counted before the camera signals go out: a follow step they trigger
    // already sees the new revision.
    ++this->m_view_globe_reanchor_revision;
    setViewGlobeFocusAnchor(lon_deg, lat_deg, height_m, yaw_deg, pitch_deg, distance_m,
                            lift_m, QSize());
    return true;
}

void MapModel::lookViewGlobeByPointerDelta(const QPoint &delta_pixels)
{
    if (this->m_view_mode != MapViewMode::Globe || this->m_eye_pose || delta_pixels.isNull())
        return;
    const double sensitivity = this->m_orbit_sensitivity_source
        ? this->m_orbit_sensitivity_source()
        : 1.0;
    lookViewGlobe(
        qDegreesToRadians(double(delta_pixels.x()) * ViewGlobeLookDegreesPerPixel * sensitivity),
        -qDegreesToRadians(double(delta_pixels.y()) * ViewGlobeLookDegreesPerPixel * sensitivity),
        std::numeric_limits<double>::quiet_NaN());
}

void MapModel::setViewGlobeLookDepressionDeg(double depression_deg)
{
    if (this->m_view_mode != MapViewMode::Globe || this->m_eye_pose || !std::isfinite(depression_deg))
        return;
    lookViewGlobe(0.0, 0.0, -qDegreesToRadians(depression_deg));
}

double MapModel::viewGlobeLookDepressionDeg() const
{
    const double pitch_deg = qBound(viewGlobeMinPitchDeg(), this->m_view_globe_pitch_deg,
                                    MaxViewGlobePitchDeg);
    const double distance_m = qMax(MinViewGlobeDistanceM, this->m_view_globe_distance_m);
    const GeoWgs84Ellipsoid::EcefPositionD eye = GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
        this->m_centerLon, this->m_centerLat, this->m_view_globe_yaw_deg, pitch_deg, distance_m,
        this->m_view_globe_vertical_offset_m, this->m_view_globe_camera_collision_lift_m);
    const GeoWgs84Ellipsoid::EcefPositionD target = GeoWgs84Ellipsoid::geodeticToEcefD(
        this->m_centerLon, this->m_centerLat, this->m_view_globe_vertical_offset_m);
    const double forward[3] = {target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const double length = std::sqrt(dot3(forward, forward));
    double eye_lon_deg = 0.0;
    double eye_lat_deg = 0.0;
    if (!(length > 0.0) || !GeoWgs84Ellipsoid::ecefToGeodetic(eye, &eye_lon_deg, &eye_lat_deg))
        return pitch_deg;
    const LocalEnuD frame = localEnuAt(eye_lon_deg, eye_lat_deg);
    return -qRadiansToDegrees(std::asin(qBound(-1.0, dot3(forward, frame.up) / length, 1.0)));
}

double MapModel::viewGlobeMinPitchDeg() const
{
    return this->m_view_globe_camera_mode == MapGlobeCameraMode::FreeLook
        ? MinViewGlobeFreeLookPitchDeg
        : MinViewGlobePitchDeg;
}

void MapModel::setViewGlobeCameraMode(MapGlobeCameraMode mode)
{
    if (this->m_view_globe_camera_mode == mode)
        return;
    this->m_view_globe_camera_mode = mode;
    // Orbit keeps the eye above its target's horizon: a free-look view
    // above that tilts down to it (the orbit's 0..90 degree lock).
    if (mode == MapGlobeCameraMode::Orbit && this->m_view_globe_pitch_deg < MinViewGlobePitchDeg)
    {
        this->m_view_globe_pitch_deg = MinViewGlobePitchDeg;
        emit viewGlobeCameraChanged();
    }
    emit viewGlobeCameraModeChanged(mode);
}

void MapModel::lookViewGlobe(double heading_delta_rad, double elevation_delta_rad,
                             double absolute_elevation_rad)
{
    // The current rig, clamped like the renderer (both modes' range).
    const double pitch_deg = qBound(MinViewGlobeFreeLookPitchDeg, this->m_view_globe_pitch_deg,
                                    MaxViewGlobePitchDeg);
    const double distance_m = qMax(MinViewGlobeDistanceM, this->m_view_globe_distance_m);
    const GeoWgs84Ellipsoid::EcefPositionD eye = GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
        this->m_centerLon, this->m_centerLat, this->m_view_globe_yaw_deg, pitch_deg, distance_m,
        this->m_view_globe_vertical_offset_m, this->m_view_globe_camera_collision_lift_m);
    const GeoWgs84Ellipsoid::EcefPositionD target = GeoWgs84Ellipsoid::geodeticToEcefD(
        this->m_centerLon, this->m_centerLat, this->m_view_globe_vertical_offset_m);

    const double forward_length = std::sqrt(
        (target.x - eye.x) * (target.x - eye.x) + (target.y - eye.y) * (target.y - eye.y)
        + (target.z - eye.z) * (target.z - eye.z));
    if (!(forward_length > 0.0))
        return;
    const double forward[3] = {(target.x - eye.x) / forward_length,
                               (target.y - eye.y) / forward_length,
                               (target.z - eye.z) / forward_length};

    // Heading/elevation of the view in the EYE's local frame: turning about
    // the eye's own up keeps the horizon level, as in an FPS look.
    double eye_lon_deg = 0.0;
    double eye_lat_deg = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(eye, &eye_lon_deg, &eye_lat_deg))
        return;
    const LocalEnuD frame = localEnuAt(eye_lon_deg, eye_lat_deg);
    const double heading_rad = std::atan2(dot3(forward, frame.east), dot3(forward, frame.north))
        + heading_delta_rad;
    double elevation_rad = std::isfinite(absolute_elevation_rad)
        ? absolute_elevation_rad
        : std::asin(qBound(-1.0, dot3(forward, frame.up), 1.0)) + elevation_delta_rad;

    // Straight down to straight up, short of the exact vertical (no heading
    // there). The target `distance_m` ahead has its own up, turned by up to
    // d / R against the eye's: near the vertical the rig's yaw would flip
    // (the picture would turn upside down), so that angle comes off twice.
    const double limit_rad = qMax(
        0.0, qDegreesToRadians(MaxViewGlobeLookAngleDeg)
                 - 2.0 * distance_m / MinimumWgs84CurvatureRadiusM);
    elevation_rad = qBound(-limit_rad, elevation_rad, limit_rad);

    const double horizontal = std::cos(elevation_rad);
    const double direction[3] = {
        frame.east[0] * horizontal * std::sin(heading_rad)
            + frame.north[0] * horizontal * std::cos(heading_rad) + frame.up[0] * std::sin(elevation_rad),
        frame.east[1] * horizontal * std::sin(heading_rad)
            + frame.north[1] * horizontal * std::cos(heading_rad) + frame.up[1] * std::sin(elevation_rad),
        frame.east[2] * horizontal * std::sin(heading_rad)
            + frame.north[2] * horizontal * std::cos(heading_rad) + frame.up[2] * std::sin(elevation_rad)};
    GeoWgs84Ellipsoid::EcefPositionD next_target;
    next_target.x = eye.x + direction[0] * distance_m;
    next_target.y = eye.y + direction[1] * distance_m;
    next_target.z = eye.z + direction[2] * distance_m;

    // Free look's own pitch range from here on (looking above the horizon
    // puts the target above the eye: negative rig pitch).
    setViewGlobeCameraMode(MapGlobeCameraMode::FreeLook);
    // A deliberate heading change leaves north-up, as an orbit yaw does.
    const bool north_up_locked = this->m_view_globe_north_up_locked;
    if (heading_delta_rad != 0.0)
        this->m_view_globe_north_up_locked = false;
    if (!setViewGlobeOrbitEyeAndTarget(eye, next_target))
        this->m_view_globe_north_up_locked = north_up_locked;
}

CoordinateWGS84 MapModel::wgs84FromScreen(const QPoint &pos, const QSize &viewport) const
{
    const QPointF center = centerMercator();
    const double world_size_px = view2dWorldSizePx();
    const double x = center.x() + (pos.x() - viewport.width() / 2.0) / world_size_px;
    const double y = std::clamp(center.y() + (pos.y() - viewport.height() / 2.0) / world_size_px, 0.0, 1.0);

    CoordinateWGS84 wgs;
    wgs.latitude_deg = GeoWebMercator::tileYToLat(y, 0);
    wgs.longitude_deg = GeoWebMercator::normalizeLongitude(GeoWebMercator::tileXToLon(x, 0));
    return wgs;
}

QPointF MapModel::screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport) const
{
    return screenFromWgs84(coord.longitude_deg, coord.latitude_deg, viewport);
}

QPointF MapModel::screenFromWgs84(double lon, double lat, const QSize &viewport) const
{
    // The world copy nearest to the camera centre.
    const QPointF center = centerMercator();
    const double x = GeoWebMercator::nearestWrappedTileX(
        GeoWebMercator::lonToTileX(GeoWebMercator::normalizeLongitude(lon), 0), center.x(), 0);
    const double y = GeoWebMercator::latToTileY(lat, 0);
    const double world_size_px = view2dWorldSizePx();
    return QPointF(double(viewport.width()) / 2.0 + (x - center.x()) * world_size_px,
                   double(viewport.height()) / 2.0 + (y - center.y()) * world_size_px);
}

QPointF MapModel::screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport,
                                  double wrap_reference_lon) const
{
    return screenFromWgs84(
        coord.longitude_deg, coord.latitude_deg, viewport, wrap_reference_lon);
}

QPointF MapModel::screenFromWgs84(double lon, double lat, const QSize &viewport,
                                  double wrap_reference_lon) const
{
    // The reference picks its world copy nearest to the camera centre; the
    // point then takes the copy nearest to the reference, so the parts of
    // one entity (pipe vertices) never split across the antimeridian.
    const QPointF center = centerMercator();
    const double reference_base_x = GeoWebMercator::lonToTileX(
        GeoWebMercator::normalizeLongitude(wrap_reference_lon), 0);
    const double reference_x = GeoWebMercator::nearestWrappedTileX(reference_base_x, center.x(), 0);
    const double local_x = GeoWebMercator::nearestWrappedTileX(
        GeoWebMercator::lonToTileX(GeoWebMercator::normalizeLongitude(lon), 0), reference_base_x, 0);
    const double x = local_x + reference_x - reference_base_x;
    const double y = GeoWebMercator::latToTileY(lat, 0);
    const double world_size_px = view2dWorldSizePx();
    return QPointF(double(viewport.width()) / 2.0 + (x - center.x()) * world_size_px,
                   double(viewport.height()) / 2.0 + (y - center.y()) * world_size_px);
}

void MapModel::emitCenterChanged()
{
    MapRenderDiagnostics::add(MapRenderDiagnostics::Counter::CenterChange);
    CoordinateWGS84 wgs;
    wgs.latitude_deg = this->m_centerLat;
    wgs.longitude_deg = this->m_centerLon;
    emit centerChangedWGS84(wgs);

    const CoordinateUTM utm = GeoMetricProjection::wgs84ToUtm(wgs);
    emit centerChangedUTM(utm);
}
