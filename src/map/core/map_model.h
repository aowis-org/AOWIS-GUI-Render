#ifndef MAP_MODEL_H
#define MAP_MODEL_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

#include <functional>

#include "geo/geo_metric_projection.h"
#include "geo/geo_web_mercator.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "map/core/map_view_types.h"
#include "map/render/map_camera_view_state.h"
#include "map/render/map_globe_picking.h"

class MapImageryProviderDirectory;

struct MapGlobeScreenRay;

enum class MapView3dNavigationState
{
    Pan,
    Rotate
};

class MapModel : public QObject
{
    Q_OBJECT

public:
    // provider_directory resolves provider ids to imagery sources; it must
    // outlive the model. Without one, tileSourcePath() is empty.
    explicit MapModel(const MapImageryProviderDirectory *provider_directory = nullptr,
                      QObject *parent = nullptr);

    // Multiplier for orbit drags, read on every drag so a changed setting
    // applies at once. Defaults to 1.
    void setOrbitSensitivitySource(const std::function<double()> &source);

    static constexpr int TileSize = GeoWebMercator::TileSize;
    static constexpr int MinZoom = 1;
    static constexpr int MaxZoom = 20;
    static constexpr double MinView3dNetworkGroundOffsetM = 0.0;
    static constexpr double MaxView3dNetworkGroundOffsetM = 50.0;
    static constexpr double MinView3dVerticalExaggeration = 0.5;
    static constexpr double MaxView3dVerticalExaggeration = 5.0;
    static constexpr double DefaultView3dVerticalExaggeration = 1.0;
    // "Globe" view mode: orbit camera around the WGS84 ellipsoid, target is
    // always the ordinary map center (centerLon()/centerLat()), so the globe
    // opens centered on the current 2D map position -- no separate target
    // state needed. Distance is expressed directly in real meters.
    //
    // The Min/Max/Default distances below are pinned to specific 2D-
    // equivalent zoom levels via viewGlobeDistanceMForZoomLevel() (see
    // that function for the exact formula, derived directly from the 2D
    // Web Mercator ground resolution in real meters) -- using a reference viewport height of
    // GlobeZoomReferenceViewportHeightPx and latitude 0 (equator). Actual
    // interactive display/editing (the footer zoom control, wheel/keyboard
    // zoom) uses the live viewport height and current latitude instead, so
    // this reference only sets where the outer clamps sit, not the
    // reported zoom level.
    //   MinViewGlobeDistanceM  == viewGlobeDistanceMForZoomLevel(MaxZoom, 0, 1000)
    //     ~360 m -- matches 2D's own maximum zoom (19), i.e. as close/
    //     detailed as 2D ever gets.
    //   MaxViewGlobeDistanceM  == viewGlobeDistanceMForZoomLevel(GlobeMinZoomLevel, 0, 1000)
    //     ~11.81 Mm -- deliberately NOT 2D's own minimum zoom (1): zoom 1
    //     shows the entire planet compressed into a couple of tiles, which
    //     on a globe camera means the sphere shrinks to a small fraction of
    //     the viewport (all but unusable). GlobeMinZoomLevel is chosen so
    //     the globe still fills most of the view when fully zoomed out.
    static constexpr int GlobeZoomReferenceViewportHeightPx = 1000;
    static constexpr double GlobeMinZoomLevel = 4.0;
    static constexpr double GlobeFieldOfViewDeg = 45.0;
    // The zoom<->distance formula's cos(latitude) term means its
    // sensitivity to latitude itself grows without bound approaching a
    // pole (d(zoom)/d(lat) is proportional to tan(lat)): near the equator,
    // dragging a few degrees changes the implied zoom by a fraction of a
    // level, but within a degree or two of 90 degrees the same drag can
    // swing it by several levels. Interactively that means the globe's
    // imagery window can end up trying to rebuild itself at a rapidly
    // thrashing sequence of zoom levels while panning near a pole, which
    // is what the visible tearing/mismatched-tile artifacts near the poles
    // actually were -- not a rendering bug so much as feeding the formula
    // a latitude where its own derivative is enormous. Clamping the
    // latitude *input to the zoom formula only* (not the camera, and not
    // where a person can actually pan to -- see clampCenter()) keeps the
    // implied zoom level -- and therefore the imagery window -- stable
    // once close to a pole, without limiting how close the camera itself
    // can get.
    static constexpr double GlobeZoomFormulaMaxLatitudeDeg = 75.0;
    static constexpr double MinViewGlobeDistanceM = 360.42;
    static constexpr double MaxViewGlobeDistanceM = 11810260.0;
    static constexpr double MinViewGlobeCameraGroundClearanceM = 2.0;
    static constexpr double DefaultViewGlobeDistanceM = MaxViewGlobeDistanceM;
    static constexpr double MinViewGlobePitchDeg = 0.0;
    static constexpr double MaxViewGlobePitchDeg = 90.0;
    // Straight down (90 degrees) rather than an oblique angle: looking
    // directly along the local "up" axis at the target is the only pitch
    // that geometrically guarantees the ellipsoid renders as a symmetric
    // disc centered exactly on the crosshair on entry, regardless of
    // viewing distance or FOV. An oblique default pitch makes the visible
    // near/far horizon asymmetric around the target -- the target point
    // itself still projects to dead center (the camera math guarantees
    // that at any pitch), but the sphere's visible silhouette skews to one
    // side of it, which reads as "the globe isn't centered". Middle-drag
    // still tilts away from this if a person wants the oblique look.
    static constexpr double DefaultViewGlobePitchDeg = 90.0;
    // North-up (the compass) is held while panning below this latitude.
    // Closer to a pole, north swings around the crosshair with every step,
    // so a pan there releases it and turns the view rigidly.
    static constexpr double GlobeNorthUpMaximumLatitudeDeg = 80.0;
    static constexpr double ViewGlobeOrbitYawDegreesPerPixel = 0.35;
    static constexpr double ViewGlobeOrbitPitchDegreesPerPixel = 0.25;

    MapCameraViewState cameraViewState() const;
    const std::optional<MapGlobeEyePose> &globeEyePose() const { return this->m_eye_pose; }
    void setGlobeEyePose(const std::optional<MapGlobeEyePose> &pose);
    // The one 2D camera zoom (Web Mercator level, MinZoom..MaxZoom, any
    // fraction). There is no integer zoom state: the imagery tile level is
    // derived from it where tiles are drawn (MapBgfx2dImagery::imageryZoomFor()).
    double view2dContinuousZoom() const;
    double centerLon() const;
    double centerLat() const;

    // The globe camera distance (meters, looking straight down) that shows
    // the same vertical ground resolution as MapViewMode::TwoD would at
    // the given (continuous) zoom level, for the given viewport height and
    // latitude. A camera distance is chosen so that a viewport-height's
    // worth of vertical extent at GlobeFieldOfViewDeg matches the requested
    // 2D meters-per-pixel directly in real meters:
    //   metersPerPixel(lat, zoom) = circumference * cos(lat) / (TileSize * 2^zoom)
    //   distance_m = viewport_height_px * metersPerPixel(lat, zoom) / (2 * tan(FOV/2))
    static double viewGlobeDistanceMForZoomLevel(
        double zoom_level, double latitude_deg, int viewport_height_px);
    // Inverse of the above: the continuous 2D-equivalent zoom level whose
    // ground resolution matches the given globe camera distance.
    static double viewGlobeZoomLevelForDistanceM(
        double distance_m, double latitude_deg, int viewport_height_px);

    MapProvider provider() const;
    QString providerId() const;
    int imageryMaximumZoom() const;
    int imageryTileZoom(int requested_zoom) const;
    MapViewMode viewMode() const;
    double view3dNetworkGroundOffsetM() const;
    double view3dVerticalExaggeration() const;
    MapView3dNavigationState view3dNavigationState() const;
    double viewGlobeYawDeg() const;
    double viewGlobePitchDeg() const;
    double viewGlobeDistanceM() const;
    // Smoothed displayed terrain height (meters, including vertical
    // exaggeration) beneath the camera. It translates the complete Globe
    // orbit rig without changing its straight-line distance/zoom.
    double viewGlobeVerticalOffsetM() const;
    // Eye-only safety lift above the smoothed rig while orbiting around a
    // frozen target. Both its attack and release are damped; on return to Pan
    // it is transferred into the whole-rig offset without moving the eye.
    double viewGlobeCameraCollisionLiftM() const;
    // Continuous, 2D-equivalent zoom level for the globe's current camera
    // distance and center latitude, using the live viewport height --
    // see viewGlobeZoomLevelForDistanceM() for the exact relationship.
    // This is what the footer zoom control displays while in Globe mode.
    double viewGlobeZoomLevel(const QSize &viewport) const;
    // Imagery tile addressing for an explicit tile level (the renderers pick
    // the level they draw; the camera has none).
    QString tileCachePrefix(int zoom) const;
    QString tileSourcePath(int zoom) const;
    QString tileCacheKey(int x, int y, int zoom) const;
    QString tileEndpoint(int x, int y, int zoom) const;

    CoordinateWGS84 wgs84FromScreen(const QPoint &pos, const QSize &viewport) const;
    QPointF screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport) const;
    QPointF screenFromWgs84(double lon, double lat, const QSize &viewport) const;
    QPointF screenFromWgs84(const CoordinateWGS84 &coord, const QSize &viewport,
                            double wrap_reference_lon) const;
    QPointF screenFromWgs84(double lon, double lat, const QSize &viewport,
                            double wrap_reference_lon) const;

    void fitViewToBounds(const CoordinateWGS84 &minimum, const CoordinateWGS84 &maximum,
                         const QSize &viewport, double elevation_minimum_m,
                         double elevation_maximum_m);
    void setCenter(double lon, double lat, const QSize &viewport = QSize());
    void setView2dContinuousZoom(double continuous_zoom, const QSize &viewport = QSize());

    // Sub-pixel anchor: an integer anchor cannot express the exact centre of
    // an odd-sized viewport and makes repeated zoom steps drift laterally.
    void zoomByAt(double steps, const QPointF &anchorPos, const QSize &viewport);
    void panByPixels(const QPoint &delta, const QSize &viewport);
    // Drag pan: turns the globe about its centre so the ground under the
    // crosshair moves by delta pixels. crosshair_radius_m is the distance
    // of the drawn surface at the crosshair (terrain) from the Earth's
    // centre; 0 takes the ellipsoid there.
    void panByPixelsGlobe(const QPoint &delta, const QSize &viewport, double crosshair_radius_m);
    // Keys: a pitch-independent ground speed from the orbit distance. Both
    // pans are rigid turns of the camera rig (rotateGlobe()): they cross the
    // poles.
    void panByPixelsGlobeKeyboard(const QPoint &delta, const QSize &viewport);

    void setProvider(MapProvider provider);
    void setProviderId(const QString &provider_id);
    void setViewMode(MapViewMode view_mode, const QSize &viewport = QSize());
    void setView3dNetworkGroundOffsetM(double offset_m);
    void setView3dVerticalExaggeration(double exaggeration);
    void beginView3dRotateInteraction();
    void endView3dRotateInteraction();

    void setViewGlobeYawDeg(double yaw_deg);
    void setViewGlobePitchDeg(double pitch_deg);
    void setViewGlobeDistanceM(double distance_m);
    void setViewGlobeCameraCollisionLiftM(double lift_m);
    // Atomically updates both terrain-follow components and emits one repaint
    // notification. The animation controller uses this instead of invoking
    // the two individual setters on every timer tick.
    void setViewGlobeTerrainHeightOffsetsM(
        double vertical_offset_m, double camera_collision_lift_m);
    // Sets distance from a 2D-equivalent zoom level via
    // viewGlobeDistanceMForZoomLevel(), using the current center latitude
    // and the given (live) viewport height. Used by the footer zoom
    // control's edit path.
    void setViewGlobeZoomLevel(double zoom_level, const QSize &viewport);
    // Explicit Globe re-anchor utility used when an orbit interaction captures
    // the visible terrain under the crosshair. The complete orbit rig is
    // updated atomically so the newly captured geographic/height target,
    // yaw/pitch and distance describe one coherent camera before any repaint.
    // camera_collision_lift_m is normally zero for a freshly captured pivot;
    // terrain-clearance handling may add a new eye-only lift afterwards while
    // the pivot itself remains frozen for the duration of the orbit.
    void setViewGlobeFocusAnchor(
        double lon, double lat, double vertical_offset_m,
        double yaw_deg, double pitch_deg, double distance_m,
        double camera_collision_lift_m, const QSize &viewport = QSize());
    // The host's provider catalog changed: keeps the selected provider if it
    // is still enabled, else switches to the first enabled one, and emits
    // providerChanged().
    void providerCatalogChanged();
    // A provider fallback changed: the sources behind the selection differ.
    void providerFallbackChanged();
    void orbitViewGlobe(double yaw_delta_deg, double pitch_delta_deg);
    void orbitViewGlobeByPointerDelta(const QPoint &delta_pixels, bool include_pitch);
    // Model-only ellipsoid fallback for the geodetic coordinate under
    // screen_position. The Globe resolves against its rendered DEM
    // first because MapModel intentionally has no renderer/terrain-mesh access.
    bool globeCoordinateAtScreen(const QPoint &screen_position, const QSize &viewport,
                                 CoordinateWGS84 *coordinate) const;

signals:
    // Any change of view2dContinuousZoom() (every frame while zooming).
    void view2dContinuousZoomChanged(double zoom);
    void centerChangedWGS84(CoordinateWGS84 wgs);
    void centerChangedUTM(CoordinateUTM utm);
    void providerChanged(MapProvider provider);
    void providerIdChanged(const QString &provider_id);
    void viewModeChanged(MapViewMode view_mode);
    void view3dVerticalExaggerationChanged(double exaggeration);
    void view3dNavigationStateChanged(MapView3dNavigationState state);
    void view3dNetworkGroundOffsetChanged(double offset_m);
    void viewGlobeCameraChanged();
    // Internal terrain-follow animation only. Keeping this separate prevents
    // height settling from being mistaken for user camera/zoom movement by
    // the editor/monitor synchronization layer.
    void viewGlobeTerrainHeightChanged();

private:
    // Sets centre and 2D zoom together, then clamps the centre for the new
    // zoom (the clamp depends on it) and emits each change once.
    void setView(double lon, double lat, double continuous_zoom, const QSize &viewport);
    // Camera centre in normalised Web Mercator: zoom-0 tile units, x and y
    // in 0..1 (x unwrapped as the longitude is normalised).
    QPointF centerMercator() const;
    // Screen pixels per normalised Mercator unit at the current 2D zoom
    // (TileSize * 2^zoom), i.e. the width of the whole world in pixels.
    double view2dWorldSizePx() const;
    void clampCenter(const QSize &viewport);
    void emitCenterChanged();
    // The rendered globe camera's ray through screen_position.
    bool globeScreenRay(const QPointF &screen_position, const QSize &viewport,
                        MapGlobeScreenRay *ray) const;
    // Where that ray meets the sphere of radius_m about the Earth's centre
    // (its closest approach, lifted onto the sphere, when it passes by).
    bool globeSpherePointAtScreen(const QPointF &screen_position, const QSize &viewport,
                                  double radius_m, GeoWgs84Ellipsoid::EcefPositionD *point) const;
    // Turns the camera rig (target and view direction) by angle_rad about
    // the unit axis through the Earth's centre. North-up holds the yaw up
    // to GlobeNorthUpMaximumLatitudeDeg and is released beyond.
    void rotateGlobe(const GeoWgs84Ellipsoid::EcefPositionD &axis, double angle_rad);
    QString providerPath() const;

    const MapImageryProviderDirectory *m_provider_directory = nullptr;
    std::function<double()> m_orbit_sensitivity_source;

    double m_view_2d_continuous_zoom = 18.0;
    double m_centerLon = 18.19331;
    double m_centerLat = 11.98119;

    MapProvider m_provider = MapProvider::ArcGISSat;
    QString m_provider_id = QStringLiteral("a0d790c2-f231-55fd-b95e-23c9bd6cb3fc");
    std::optional<MapGlobeEyePose> m_eye_pose;
    quint64 m_pose_revision = 0;
    MapViewMode m_view_mode = MapViewMode::TwoD;
    double m_view_3d_network_ground_offset_m = 0.0;
    double m_view_3d_vertical_exaggeration = DefaultView3dVerticalExaggeration;
    MapView3dNavigationState m_view_3d_navigation_state = MapView3dNavigationState::Pan;
    int m_view_3d_rotate_interaction_depth = 0;

    double m_view_globe_yaw_deg = 0.0;
    double m_view_globe_pitch_deg = DefaultViewGlobePitchDeg;
    double m_view_globe_distance_m = DefaultViewGlobeDistanceM;
    double m_view_globe_vertical_offset_m = 0.0;
    double m_view_globe_camera_collision_lift_m = 0.0;
    bool m_view_globe_north_up_locked = true;
};

#endif // MAP_MODEL_H
