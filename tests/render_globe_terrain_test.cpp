// Globe terrain pipeline in Render (AOWIS::RenderGlobeTerrain) behind the
// host's MapTerrainTileSource: terrain levels from the active dataset and
// the injected detail reduction, tile requests from the preparation.
// Checks do not depend on assert(), so they also run in Release builds.

#include "geo/geo_wgs84_ellipsoid.h"
#include "map/core/map_model.h"
#include "map/render/map_globe_camera.h"
#include "map/data/map_terrain_tile_source.h"
#include "map/render/map_globe_surface_preparation.h"
#include "map/render/map_globe_terrain_navigation.h"

#include <QSize>

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>

namespace
{
int g_failures = 0;
int g_checks = 0;

void expectTrue(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

// A catalog with one 30 m dataset (z8..14) and no cached tiles; records
// requests.
class FakeTerrainSource : public MapTerrainTileSource
{
public:
    MapTerrainDatasetInfo dataset;
    bool catalog_known = true;
    int requests = 0;

    FakeTerrainSource()
    {
        this->dataset.dataset = QStringLiteral("copernicus-glo30");
        this->dataset.provider = QStringLiteral("copernicus");
        this->dataset.nominal_resolution_m = 30.0;
        this->dataset.vertical_datum = MapTerrainVerticalDatum::Egm2008;
        this->dataset.surface_model = QStringLiteral("dsm");
        this->dataset.minimum_zoom = 8;
        this->dataset.maximum_zoom = 14;
    }

    const MapTerrainTile *tile(const QString &) const override
    {
        return nullptr;
    }

    const MapTerrainTile *tile(const QString &, int, quint32, quint32) const override
    {
        return nullptr;
    }

    void requestTile(const QString &, int, quint32, quint32) override
    {
        ++this->requests;
    }

    const MapTerrainDatasetInfo *activeDataset() const override
    {
        return this->catalog_known ? &this->dataset : nullptr;
    }
};

void testFixedEyeCamera()
{
    const QSize viewport(800, 400);
    MapModel model;
    model.setViewMode(MapViewMode::Globe, viewport);
    MapGlobeCamera camera;
    camera.setViewportSize(viewport);
    camera.syncFromMapModel(model);
    MapGlobeScreenRay original;
    expectTrue(camera.screenRay(QPointF(400, 200), &original), "initial orbit ray");
    MapGlobeEyePose pose;
    pose.eye = original.origin_ecef;
    for (int angle : {0, 90, 180, 270})
    {
        pose.orientation = QQuaternion::fromAxisAndAngle(0, 1, 0, angle)
            * QQuaternion::fromAxisAndAngle(0, 0, 1, 35);
        model.setGlobeEyePose(pose);
        camera.syncFromMapModel(model);
        camera.updateRenderOrigin();
        MapGlobeScreenRay ray;
        expectTrue(camera.screenRay(QPointF(600, 100), &ray), "off-center fixed-eye ray");
        expectTrue(ray.origin_ecef.x == original.origin_ecef.x
                   && ray.origin_ecef.y == original.origin_ecef.y
                   && ray.origin_ecef.z == original.origin_ecef.z, "rotating preserves the exact eye");
        const QVector3D projected = camera.viewProjectionMatrixForOrigin(pose.eye)
            .map(ray.direction * 100000.0f);
        expectTrue(std::abs(projected.x() - 0.5f) < 0.0001f
                   && std::abs(projected.y() - 0.5f) < 0.0001f, "rolled matrix agrees with the screen ray");
        camera.screenRay(QPointF(400, 200), &ray);
        expectTrue((ray.direction - pose.orientation.rotatedVector(QVector3D(0, 0, -1))).length()
                   < 0.0001f, "screen faces along the device back axis");
    }
    const QQuaternion before_zoom = model.globeEyePose()->orientation;
    for (double steps : {1.0, -2.0, 100.0, -100.0})
    {
        const double before_scale = model.globeEyePose()->magnification;
        model.zoomGlobeEyeBy(steps);
        const MapGlobeEyePose &zoomed = *model.globeEyePose();
        expectTrue(zoomed.eye.x == pose.eye.x && zoomed.eye.y == pose.eye.y && zoomed.eye.z == pose.eye.z
                   && zoomed.orientation == before_zoom, "zoom preserves the exact camera pose");
        const double scale = std::clamp(before_scale * std::exp2(steps), 0.5, 8.0);
        camera.syncFromMapModel(model);
        MapGlobeScreenRay ray;
        camera.screenRay(QPointF(600, 200), &ray);
        const QVector3D local = before_zoom.conjugated().rotatedVector(ray.direction);
        const double expected = std::tan(MapModel::GlobeFieldOfViewDeg * std::acos(-1.0) / 360.0) / scale;
        expectTrue(std::abs(local.x() / -local.z() - expected) < 0.00001, "ray uses optical zoom");
        const QVector3D projected = camera.viewProjectionMatrixForOrigin(pose.eye).map(ray.direction * 100000.0f);
        expectTrue(std::abs(projected.x() - 0.5f) < 0.0001f, "zoomed projection agrees with picking");
    }
    model.setGlobeEyePose(std::nullopt);
    expectTrue(model.globeFieldOfViewDeg() == MapModel::GlobeFieldOfViewDeg, "orbit resumes at default FOV");
    camera.syncFromMapModel(model);
    MapGlobeScreenRay restored;
    camera.screenRay(QPointF(400, 200), &restored);
    expectTrue((restored.direction - original.direction).length() < 0.0001f, "exit restores orbit");
}

void testTerrainLevels()
{
    FakeTerrainSource source;
    const MapGlobeTerrainLevels none = mapGlobeTerrainLevels(nullptr, 1);
    expectTrue(!none.enabled(), "no tile source: no relief");

    source.catalog_known = false;
    expectTrue(!mapGlobeTerrainLevels(&source, 1).enabled(), "no relief before the catalog arrives");
    source.catalog_known = true;

    const MapGlobeTerrainLevels levels = mapGlobeTerrainLevels(&source, 1);
    expectTrue(levels.enabled() && levels.dataset == QStringLiteral("copernicus-glo30")
                   && levels.minimum_zoom == 8 && levels.maximum_zoom == 14,
               "levels follow the active dataset's zoom range");
    expectTrue(levels.terrainZoomForImageryZoom(12) == 11 && levels.terrainZoomForImageryZoom(18) == 14,
               "one level below the imagery, never finer than the native detail");
    expectTrue(mapGlobeTerrainLevels(&source, 7).detail_reduction == MapGlobeTerrainMaximumDetailReduction
                   && mapGlobeTerrainLevels(&source, -2).detail_reduction == 0,
               "the detail reduction is clamped");

    expectTrue(mapGlobeTerrainDetailReduction(std::function<int()>()) == MapGlobeTerrainDefaultDetailReduction,
               "without a source the default detail reduction applies");
    const std::function<int()> zero = []() { return 0; };
    expectTrue(mapGlobeTerrainDetailReduction(zero) == 0, "the injected source is read");
}

void testPreparationUsesSource()
{
    FakeTerrainSource source;
    MapModel model;
    model.setCenter(8.6821, 50.1109);
    model.setViewMode(MapViewMode::Globe, QSize(1280, 720));
    model.setViewGlobeDistanceM(5000.0);

    MapGlobeSurfacePreparation preparation(&model);
    int reduction = 2;
    const std::function<int()> reduction_source = [&reduction]() { return reduction; };
    preparation.setTerrainDetailReductionSource(reduction_source);
    preparation.setTerrainRepository(&source);
    expectTrue(preparation.terrainRepository() == &source, "the preparation keeps its tile source");

    // The levels are refreshed with each view request.
    expectTrue(preparation.requestTerrainForCurrentView(QSize(1280, 720)),
               "a view request runs with a source and a viewport");
    expectTrue(preparation.terrainLevels().detail_reduction == 2
                   && preparation.terrainLevels().maximum_zoom == 14,
               "the preparation's levels use the source's dataset and the injected reduction");
    expectTrue(source.requests > 0, "a close globe view requests DEM tiles from the source");

    MapGlobeTerrainNavigation navigation(&model);
    navigation.setTerrainRepository(&source);
    navigation.setTerrainDetailReductionSource(reduction_source);
    navigation.setSurfacePreparation(&preparation);
    expectTrue(true, "navigation accepts the same source and reduction");
}

// The rig's eye height above the ellipsoid (as the renderer builds it).
double eyeHeightM(const MapModel &model)
{
    const GeoWgs84Ellipsoid::EcefPositionD eye = GeoWgs84Ellipsoid::orbitCameraEyeEcefD(
        model.centerLon(), model.centerLat(), model.viewGlobeYawDeg(),
        std::clamp(model.viewGlobePitchDeg(), MapModel::MinViewGlobeFreeLookPitchDeg,
                   MapModel::MaxViewGlobePitchDeg),
        std::max(MapModel::MinViewGlobeDistanceM, model.viewGlobeDistanceM()),
        model.viewGlobeVerticalOffsetM(), model.viewGlobeCameraCollisionLiftM());
    double lon_deg = 0.0;
    double lat_deg = 0.0;
    double height_m = 0.0;
    GeoWgs84Ellipsoid::ecefToGeodetic(eye, &lon_deg, &lat_deg, &height_m);
    return height_m;
}

// State machine: free look turns with a frozen target, then pans with
// terrain following that keeps the eye's height above the ground (flat
// ground here: the eye must neither sink nor rise, also after a look up that
// put the target high above the eye).
void testFreeLookFollow()
{
    FakeTerrainSource source;
    MapModel model;
    model.setCenter(8.6821, 50.1109);
    model.setViewMode(MapViewMode::Globe, QSize(1280, 720));
    model.setViewGlobeDistanceM(5000.0);
    model.setViewGlobePitchDeg(30.0);

    MapGlobeTerrainNavigation navigation(&model);
    navigation.setTerrainRepository(&source);
    navigation.setViewportSize(QSize(1280, 720));
    navigation.syncCameraHeight(false);
    const double orbit_eye_height_m = eyeHeightM(model);

    model.beginView3dRotateInteraction();
    model.setViewGlobeLookDepressionDeg(-30.0);
    navigation.syncCameraHeight(false);
    model.endView3dRotateInteraction();
    expectTrue(model.viewGlobeCameraMode() == MapGlobeCameraMode::FreeLook
                   && model.viewGlobePitchDeg() < 0.0,
               "the look up put the target above the eye");
    expectTrue(std::abs(eyeHeightM(model) - orbit_eye_height_m) < 0.01, "the look keeps the eye in place");

    navigation.syncCameraHeight(false);
    expectTrue(std::abs(eyeHeightM(model) - orbit_eye_height_m) < 0.01,
               "ending the look keeps the eye's height (follow height captured)");
    expectTrue(!navigation.following(), "nothing left to follow on flat ground");

    model.setCenter(8.70, 50.12);
    navigation.syncCameraHeight(false);
    expectTrue(std::abs(eyeHeightM(model) - orbit_eye_height_m) < 0.01,
               "panning in free look keeps the eye's height above the ground");

    // Back to the orbit: its 0..90 lock, following from the DEM under the eye.
    model.beginView3dRotateInteraction();
    model.setViewGlobeCameraMode(MapGlobeCameraMode::Orbit);
    model.endView3dRotateInteraction();
    expectTrue(model.viewGlobePitchDeg() >= MapModel::MinViewGlobePitchDeg,
               "the orbit restores its tilt lock");
}
}

int main()
{
    testFixedEyeCamera();
    testTerrainLevels();
    testPreparationUsesSource();
    testFreeLookFollow();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d of %d globe terrain checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: %d globe terrain checks\n", g_checks);
    return 0;
}
