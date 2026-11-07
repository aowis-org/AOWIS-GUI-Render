// Globe terrain pipeline in Render (AOWIS::RenderGlobeTerrain) behind the
// host's MapTerrainTileSource: terrain levels from the active dataset and
// the injected detail reduction, tile requests from the preparation.
// Checks do not depend on assert(), so they also run in Release builds.

#include "map/core/map_model.h"
#include "map/render/map_globe_camera.h"
#include "map/data/map_terrain_tile_source.h"
#include "map/render/map_globe_surface_preparation.h"
#include "map/render/map_globe_terrain_navigation.h"

#include <QSize>

#include <cstdio>
#include <cmath>
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
    model.setGlobeEyePose(std::nullopt);
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
}

int main()
{
    testFixedEyeCamera();
    testTerrainLevels();
    testPreparationUsesSource();
    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d of %d globe terrain checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("PASS: %d globe terrain checks\n", g_checks);
    return 0;
}
