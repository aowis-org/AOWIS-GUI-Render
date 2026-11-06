#ifndef GEO_WGS84_ELLIPSOID_H
#define GEO_WGS84_ELLIPSOID_H

#include <QPointF>
#include <QSize>
#include <QVector3D>

// Converts geodetic coordinates (longitude/latitude/height above the WGS84
// ellipsoid) into earth-centered, earth-fixed (ECEF) coordinates, for the
// "Globe" map view mode, which renders planet Earth as its actual WGS84
// ellipsoid shape rather than the flat Web Mercator plane used by the 2D
// view modes. Also provides the shared orbit-camera basis, screen-ray/
// ellipsoid intersection, and ECEF -> geodetic conversion used by both the
// GPU camera (MapGlobeCamera) and the CPU-side picking/panning math (MapModel)
// -- mirroring how the 2D view already keeps its own screen<->geo
// conversions in MapModel independent of the GPU camera (see
// MapModel::wgs84FromScreen()/screenFromWgs84()).
//
// The geodetic <-> ECEF conversions themselves are delegated to
// GeographicLib::Geocentric, which is already vendored and linked into this
// project (see geo_metric_projection.h for the existing UTM use of
// GeographicLib). Hand-rolling those formulas here would just be a
// worse-tested reimplementation of what GeographicLib already provides,
// including the local east/north/up frame used by the globe orbit camera
// below.
class GeoWgs84Ellipsoid
{
public:
    GeoWgs84Ellipsoid() = delete;

    // WGS84 semi-major axis (equatorial radius) and semi-minor axis (polar
    // radius), in meters. EquatorialRadiusM is used for camera framing
    // (near/far clip planes, default orbit distance); both are used
    // together by rayIntersection() below, since that is a genuine
    // ellipsoid (not sphere) intersection. The actual vertex geometry
    // always goes through GeographicLib, which carries the full ellipsoid
    // itself.
    static constexpr double EquatorialRadiusM = 6378137.0;
    static constexpr double FlatteningInverse = 298.257223563;
    static constexpr double PolarRadiusM =
        EquatorialRadiusM * (1.0 - 1.0 / FlatteningInverse);

    // A point on (or above) the ellipsoid together with the local
    // east/north/up unit basis at that point, all expressed in ECEF meters.
    // "position" is the ECEF coordinate itself; "east"/"north"/"up" are unit
    // directions, not positions.
    struct LocalFrame
    {
        QVector3D position;
        QVector3D east;
        QVector3D north;
        QVector3D up;
    };

    // Eye/target/basis of an orbit camera: "distance_m" away from the
    // geodetic (target_lon_deg, target_lat_deg) point, offset by yaw_deg
    // (heading around that point's local up axis) and pitch_deg (tilt up
    // from the local horizon), matching the same yaw/pitch/distance orbit
    // shape as the map orbit camera, anchored to the ellipsoid's
    // local frame instead of the flat Web Mercator plane. Shared by
    // MapGlobeCamera (GPU view/projection matrix) and MapModel (CPU-side
    // screen-ray picking for click-drag panning), so the two stay in sync
    // by construction instead of by keeping two hand-written copies of this
    // math in agreement.
    struct OrbitCameraBasis
    {
        QVector3D eye;
        QVector3D target;
        QVector3D forward;
        QVector3D right;
        QVector3D up;
    };

    // An ECEF position kept in double precision throughout, rather than the
    // QVector3D (float) used everywhere else in this class. QVector3D is
    // fine for *directions* (unit-length, so always near magnitude 1) and
    // for positions that stay near some local origin, but a raw ECEF position is ~6.378e6 m
    // from the coordinate origin (Earth's center) regardless of where on
    // the planet it is, and float32 only carries ~7 significant decimal
    // digits -- meaning ~0.5-1m of resolution *at best* once you're at that
    // magnitude, before any arithmetic is even done on it. Subtracting two
    // such positions that happen to be close together in reality (e.g. the
    // orbit camera's eye and target, which can be mere meters apart at
    // close-in Globe zoom) is where this actually bites: two float32 values
    // that agree in their first 6-7 significant digits leave almost nothing
    // once you cancel those digits out, so the "small" result -- which is
    // the physically meaningful one, since it is what determines the
    // camera's actual view direction -- is mostly rounding noise. That
    // noise pattern shifts from frame to frame as the orbit angle changes
    // continuously, which is what shows up on screen as network geometry
    // jittering/jumping in height while orbiting.
    //
    // EcefPositionD exists so that this kind of "position minus nearby
    // position" arithmetic can happen in double (~15-17 significant
    // decimal digits, i.e. sub-millimeter resolution even at ECEF
    // magnitude) *before* narrowing to float32 -- at which point the value
    // being narrowed is small (bounded by how far apart the two positions
    // actually are, not by Earth's radius), so the narrowing itself no
    // longer loses meaningful precision. See orbitCameraBasisRelativeToOrigin()
    // below and MapGlobeNetworkScene::ecefPosition(), which is the other
    // half of this fix.
    struct EcefPositionD
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    // The Globe counterpart of OrbitCameraBasis, except eye/target are
    // expressed *relative to a caller-supplied ECEF origin* instead of
    // being raw (Earth-center-relative) ECEF positions. See the
    // EcefPositionD comment above for why that distinction matters: as long
    // as the origin is reasonably close to eye/target (which it will be --
    // see globeRenderOriginEcef()), these vectors stay small enough that
    // float32 represents them with plenty of precision, unlike
    // OrbitCameraBasis::eye/target above.
    struct OrbitCameraBasisRelative
    {
        QVector3D eye;
        QVector3D target;
        QVector3D forward;
        QVector3D right;
        QVector3D up;
    };

    // Converts geodetic coordinates to ECEF meters. height_m is height above
    // the WGS84 ellipsoid (0 for a point on the surface).
    static QVector3D geodeticToEcef(double lon_deg, double lat_deg, double height_m);

    // Same conversion as geodeticToEcef(), but keeps the result in double
    // precision throughout instead of narrowing to QVector3D's float32 --
    // see the EcefPositionD comment above for why that matters. Callers
    // that need to do further position arithmetic at ECEF scale (subtract
    // two nearby ECEF points, place vertices relative to a render origin,
    // etc.) should start from here rather than from geodeticToEcef(), and
    // only narrow to float32 once the result is already small.
    static EcefPositionD geodeticToEcefD(double lon_deg, double lat_deg, double height_m);

    // Same conversion, but also returns the local east/north/up frame at
    // that point.
    static LocalFrame localFrameAtGeodetic(double lon_deg, double lat_deg, double height_m);

    // Inverse of geodeticToEcef(): finds the geodetic longitude/latitude of
    // the ellipsoid point nearest to an arbitrary ECEF position. height_m is
    // optional -- most callers only ever want "which lon/lat is this point
    // over" and pass nullptr, but terrain-aware globe picking (bisecting a
    // ray against actual DEM relief rather than the bare ellipsoid) needs
    // the point's true height above the ellipsoid too, so it is available
    // as a byproduct of the same GeographicLib reverse conversion rather
    // than a second, separate computation. Always succeeds for any non-zero
    // input; only returns false for the degenerate (0,0,0) case.
    static bool ecefToGeodetic(
        const QVector3D &ecef, double *lon_deg, double *lat_deg, double *height_m = nullptr);

    // Double-precision counterpart used when the input is a camera position:
    // narrowing an Earth-radius-scale ECEF point to QVector3D before the
    // reverse conversion would quantize the sub-camera point by roughly a
    // meter. This overload preserves the precision carried by EcefPositionD.
    static bool ecefToGeodetic(
        const EcefPositionD &ecef, double *lon_deg, double *lat_deg,
        double *height_m = nullptr);

    // Moves a geodetic point by a local east/north offset along WGS84. This
    // remains well behaved at high latitude and across the antimeridian,
    // unlike adding meter-derived deltas directly to longitude/latitude.
    static bool offsetGeodetic(
        double lon_deg, double lat_deg, double east_m, double north_m,
        double *offset_lon_deg, double *offset_lat_deg);

    // Returns the orbit eye without narrowing its Earth-scale ECEF position
    // to float. camera_collision_lift_m moves only the eye along the target's
    // local up direction; target_height_m still translates the whole rig.
    static EcefPositionD orbitCameraEyeEcefD(
        double target_lon_deg, double target_lat_deg,
        double yaw_deg, double pitch_deg, double distance_m,
        double target_height_m = 0.0,
        double camera_collision_lift_m = 0.0);

    // Builds the orbit camera basis described above. pitch_deg is expected
    // to already be clamped by the caller (this function does not know
    // MapModel's Min/MaxViewGlobePitchDeg bounds, to keep this header free
    // of a dependency on map/core). target_height_m lifts the target (and
    // therefore the whole eye/target rig) that far above the ellipsoid
    // along the target's local "up" -- 0 for the bare sea-level ellipsoid
    // surface, or a terrain-follow offset for the complete rig.
    // camera_collision_lift_m moves only the eye upward, preserving the
    // target while adding terrain clearance.
    static OrbitCameraBasis orbitCameraBasis(
        double target_lon_deg, double target_lat_deg,
        double yaw_deg, double pitch_deg, double distance_m,
        double target_height_m = 0.0,
        double camera_collision_lift_m = 0.0);

    // Same orbit camera rig as orbitCameraBasis() above -- identical
    // yaw/pitch/distance/target_height_m conventions, and the resulting
    // eye/target are the same physical points -- but every position is
    // accumulated in double precision (starting from the target's own ECEF
    // position, through the eye's offset from it) and only narrowed to
    // float32 *after* origin_ecef has already been subtracted off. This is
    // what makes the result safe to feed into a GPU view matrix at close-in
    // Globe zoom, where orbitCameraBasis() itself is not (see the
    // EcefPositionD comment above). origin_ecef should be
    // MapGlobeCamera::renderOriginEcef() (or the exact ECEF position the
    // caller has otherwise agreed to render relative to) -- passing
    // anything far from the actual eye/target defeats the purpose, since
    // the whole point is that eye/target stay *close to* origin_ecef so
    // their float32 narrowing has something small to work with.
    static OrbitCameraBasisRelative orbitCameraBasisRelativeToOrigin(
        double target_lon_deg, double target_lat_deg,
        double yaw_deg, double pitch_deg, double distance_m,
        double target_height_m, const EcefPositionD &origin_ecef,
        double camera_collision_lift_m = 0.0);

    // Nearest intersection of the ray (origin + t*direction, t >= 0) with
    // the WGS84 ellipsoid. direction need not be normalized. Returns false
    // if the ray misses the ellipsoid entirely or points away from it --
    // callers (screen-ray picking near the globe's horizon/limb) must
    // handle that as "no pick" rather than assuming a hit.
    static bool rayIntersection(
        const QVector3D &origin, const QVector3D &direction, QVector3D *intersection);

    // Double-precision ECEF counterpart used by Globe surface picking. The
    // direction need not be normalized. distance_m is the physical distance
    // from origin to the nearest forward hit and is therefore expressed in
    // meters regardless of the input direction's magnitude.
    static bool rayIntersection(
        const EcefPositionD &origin, const QVector3D &direction,
        EcefPositionD *intersection, double *distance_m);

    // Maps a screen position to a world-space ray through the given orbit
    // camera basis (same vertical_fov_deg/viewport convention as the GPU
    // perspective projection built from that same basis). This is the one
    // place the NDC -> ray formula lives; MapGlobeCamera's GPU-side globe ray
    // and MapModel's CPU-side globe picking/panning each build their own
    // OrbitCameraBasis (they know different things: synced camera state vs.
    // model state) but both route the actual ray math through here instead
    // of keeping their own copies of the same formula.
    static bool screenRay(
        const OrbitCameraBasis &basis, const QPointF &screen_position,
        const QSize &viewport, double vertical_fov_deg,
        QVector3D *eye, QVector3D *direction);
};

#endif // GEO_WGS84_ELLIPSOID_H
