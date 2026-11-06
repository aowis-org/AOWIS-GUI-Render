#include "map/render/map_globe_network_scene.h"

#include "map/core/map_model.h"
#include "map/render/map_icon_atlas.h"
#include "map/render/map_globe_link_geometry.h"
#include "map/render/map_globe_underground_geometry.h"
#include "map/render/map_node_declutter.h"
#include "map/render/map_render_diagnostics.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "network/infrastructure_entity_traits.h"
#include "network/network_symbology_rendering.h"

#include <GeographicLib/Geodesic.hpp>

#include <cmath>

namespace
{
quint64 entityRenderKey(InfrastructureEntity entity_type, quint32 render_id)
{
    return (quint64(quint32(int(entity_type))) << 32) | quint64(render_id);
}

bool finiteCoordinate(const CoordinateWGS84 &coordinate)
{
    return std::isfinite(coordinate.longitude_deg) && std::isfinite(coordinate.latitude_deg);
}

// How far below the sampled terrain a point has to sit before it counts as
// "buried" -- small enough to ignore DEM sampling noise/float error for a
// pipe running essentially at grade, large enough to not flag on that noise.
constexpr double UndergroundToleranceM = 0.1;

struct GeodesicSpan
{
    double distance_m = 0.0;
    double initial_azimuth_deg = 0.0;
};

bool geodesicSpanBetween(
    const CoordinateWGS84 &start, const CoordinateWGS84 &end,
    GeodesicSpan *span)
{
    if (span == nullptr || !finiteCoordinate(start) || !finiteCoordinate(end))
        return false;

    double final_azimuth_deg = 0.0;
    GeographicLib::Geodesic::WGS84().Inverse(
        start.latitude_deg, start.longitude_deg,
        end.latitude_deg, end.longitude_deg,
        span->distance_m, span->initial_azimuth_deg, final_azimuth_deg);
    return std::isfinite(span->distance_m)
        && std::isfinite(span->initial_azimuth_deg)
        && span->distance_m >= 0.0;
}

bool geodesicCoordinateAtDistance(
    const CoordinateWGS84 &start, double initial_azimuth_deg, double distance_m,
    CoordinateWGS84 *coordinate)
{
    if (coordinate == nullptr || !finiteCoordinate(start)
        || !std::isfinite(initial_azimuth_deg) || !std::isfinite(distance_m))
    {
        return false;
    }

    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    GeographicLib::Geodesic::WGS84().Direct(
        start.latitude_deg, start.longitude_deg, initial_azimuth_deg, distance_m,
        latitude_deg, longitude_deg);
    if (!std::isfinite(longitude_deg) || !std::isfinite(latitude_deg))
        return false;

    coordinate->longitude_deg = longitude_deg;
    coordinate->latitude_deg = latitude_deg;
    return true;
}

CoordinateWGS84 coordinateWithDeclutterOffset(
    const CoordinateWGS84 &coordinate, const QPointF &offset_m)
{
    if (qFuzzyIsNull(offset_m.x()) && qFuzzyIsNull(offset_m.y()))
        return coordinate;

    CoordinateWGS84 result = coordinate;
    double longitude_deg = coordinate.longitude_deg;
    double latitude_deg = coordinate.latitude_deg;
    if (GeoWgs84Ellipsoid::offsetGeodetic(
            coordinate.longitude_deg, coordinate.latitude_deg,
            offset_m.x(), offset_m.y(), &longitude_deg, &latitude_deg))
    {
        result.longitude_deg = longitude_deg;
        result.latitude_deg = latitude_deg;
    }
    return result;
}
}

void MapGlobeNetworkScene::setNetworkSnapshot(const NetworkRenderSnapshot &snapshot)
{
    const MapRetainedNetworkUpdate update = this->retained_network.setSnapshot(snapshot);
    const MapGlobeSceneInvalidation plan =
        MapGlobeSceneSync::forSnapshot(update.geometry_changed);
    if (plan.geometry)
    {
        // Use a network-local origin for the retained vertices. Unlike the
        // camera's sticky terrain origin, this anchor changes only on actual
        // geometry revisions, never on pan/orbit/zoom or camera rebasing.
        const NetworkRenderSnapshot &geometry = this->retained_network.snapshot();
        bool found_anchor = false;
        CoordinateWGS84 anchor;
        for (const NetworkRenderNode &node : geometry.nodes)
        {
            if (finiteCoordinate(node.coordinate_wgs84))
            {
                anchor = node.coordinate_wgs84;
                found_anchor = true;
                break;
            }
        }
        if (!found_anchor)
        {
            for (const NetworkRenderLink &link : geometry.links)
            {
                for (const CoordinateWGS84 &coordinate : link.vertices_wgs84)
                {
                    if (finiteCoordinate(coordinate))
                    {
                        anchor = coordinate;
                        found_anchor = true;
                        break;
                    }
                }
                if (found_anchor)
                    break;
            }
        }
        if (!found_anchor)
        {
            for (const NetworkRenderDemandPointAttachment &attachment
                 : geometry.demand_point_attachments)
            {
                if (finiteCoordinate(attachment.demand_point_coordinate_wgs84))
                {
                    anchor = attachment.demand_point_coordinate_wgs84;
                    found_anchor = true;
                    break;
                }
            }
        }
        if (found_anchor)
        {
            this->retained_origin.setGeometryOrigin(
                GeoWgs84Ellipsoid::geodeticToEcefD(
                    anchor.longitude_deg, anchor.latitude_deg, 0.0));
        }
        applySceneInvalidation(plan);
    }
}

bool MapGlobeNetworkScene::setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids)
{
    if (this->hidden_entity_uuids == hidden_entity_uuids)
        return false;

    this->hidden_entity_uuids = hidden_entity_uuids;
    rebuildNetworkGeometry();
    return true;
}

void MapGlobeNetworkScene::applySceneInvalidation(const MapGlobeSceneInvalidation &plan)
{
    // One full geometry pass already refreshes dependent streams; never
    // follow it with redundant icons, X-Ray or flow rebuilds.
    if (plan.geometry)
    {
        rebuildNetworkGeometry();
        return;
    }
    if (plan.icons)
        rebuildIcons();
    if (plan.models)
    {
        rebuildTankInstances();
        rebuildReservoirInstances();
    }
    if (plan.junctions)
        rebuildJunctionInstances();
    if (plan.underground)
        rebuildUndergroundXRayGeometry();
    if (plan.flow)
        rebuildFlowDirections();
    if (plan.highlights)
        rebuildHighlights();
}

void MapGlobeNetworkScene::setSymbology(const MapNetworkRenderSymbology &symbology)
{
    // Colours are not part of the geometry (entity references, see
    // mapNetworkSetEntityReference()): only sizes, visibility toggles and
    // whether flow directions exist at all rebuild anything here.
    const bool link_thickness_changed =
        this->symbology.link_thickness_unit != symbology.link_thickness_unit
        || this->symbology.link_thickness_px != symbology.link_thickness_px
        || this->symbology.link_thickness_m != symbology.link_thickness_m;
    const bool icon_changed =
        this->symbology.icon_size_unit != symbology.icon_size_unit
        || this->symbology.icon_size_px != symbology.icon_size_px
        || this->symbology.icon_size_m != symbology.icon_size_m
        || this->symbology.show_icons != symbology.show_icons;
    const bool junction_instance_changed =
        this->symbology.node_size_unit != symbology.node_size_unit
        || (symbology.node_size_unit == NetworkSymbologySizeUnit::Meters
            && this->symbology.node_size_m != symbology.node_size_m);
    // Chevrons exist for every link in both orientations while a result
    // gives flow directions; the direction itself is per-entity GPU data.
    const bool flow_direction_changed =
        this->symbology.show_flow_direction != symbology.show_flow_direction
        || this->symbology.flow_direction_size_px != symbology.flow_direction_size_px
        || this->symbology.flow_directions.isEmpty() != symbology.flow_directions.isEmpty()
        || link_thickness_changed;
    const bool highlights_changed =
        link_thickness_changed || this->symbology.show_junctions != symbology.show_junctions;

    MapGlobeSceneSymbologyChanges changes;
    changes.link_thickness_changed = link_thickness_changed;
    changes.icon_changed = icon_changed;
    changes.junction_size_changed = junction_instance_changed;
    changes.flow_changed = flow_direction_changed;
    changes.highlights_changed = highlights_changed;
    changes.has_demand_attachments =
        !this->retained_network.snapshot().demand_point_attachments.isEmpty();
    changes.underground_enabled = this->underground_xray_enabled;

    this->symbology = symbology;
    applySceneInvalidation(MapGlobeSceneSync::forSymbology(changes));
}

void MapGlobeNetworkScene::setSelectedEntity(
    InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (this->selected_entity_type == entity_type && this->selected_entity_uuid == uuid)
        return;

    this->selected_entity_type = entity_type;
    this->selected_entity_uuid = uuid;
    applySceneInvalidation(MapGlobeSceneSync::forSelection());
}

void MapGlobeNetworkScene::setSimulationErrorEntities(
    const QHash<QUuid, InfrastructureEntity> &error_entities,
    const QSet<QUuid> &stale_entity_uuids)
{
    this->simulation_error_entities = error_entities;
    this->simulation_stale_entity_uuids = stale_entity_uuids;
    applySceneInvalidation(MapGlobeSceneSync::forDiagnostics());
}

bool MapGlobeNetworkScene::setUse3dIconModels(bool enabled)
{
    if (this->use_3d_icon_models == enabled)
        return false;

    this->use_3d_icon_models = enabled;
    applySceneInvalidation(MapGlobeSceneSync::forIconModelMode());
    return true;
}

bool MapGlobeNetworkScene::setNodeDeclutteringEnabled(bool enabled)
{
    if (this->node_decluttering_enabled == enabled)
        return false;

    this->node_decluttering_enabled = enabled;
    rebuildNetworkGeometry();
    return true;
}

bool MapGlobeNetworkScene::setGroundOffsetM(double offset_m)
{
    if (!std::isfinite(offset_m))
        return false;

    const double bounded_offset_m = qBound(
        MapModel::MinView3dNetworkGroundOffsetM,
        offset_m,
        MapModel::MaxView3dNetworkGroundOffsetM);
    if (qFuzzyCompare(
            1.0 + this->vertical_transform.networkGroundOffsetM(),
            1.0 + bounded_offset_m))
        return false;

    this->vertical_transform.setNetworkGroundOffsetM(bounded_offset_m);
    rebuildNetworkGeometry();
    return true;
}

bool MapGlobeNetworkScene::setVerticalExaggeration(double exaggeration)
{
    if (!std::isfinite(exaggeration))
        return false;

    const double bounded_exaggeration = qBound(
        MapModel::MinView3dVerticalExaggeration,
        exaggeration,
        MapModel::MaxView3dVerticalExaggeration);
    if (qFuzzyCompare(
            1.0 + this->vertical_transform.verticalExaggeration(),
            1.0 + bounded_exaggeration))
        return false;

    this->vertical_transform.setVerticalExaggeration(bounded_exaggeration);
    rebuildNetworkGeometry();
    return true;
}

void MapGlobeNetworkScene::setTerrainElevationResolver(TerrainElevationResolver resolver)
{
    this->terrain_elevation_resolver = std::move(resolver);
}

void MapGlobeNetworkScene::setSurfaceHeightResolver(SurfaceHeightResolver resolver)
{
    this->surface_height_resolver = std::move(resolver);
}

double MapGlobeNetworkScene::drapedHeightM(
    const CoordinateWGS84 &render_coordinate, double *surface_height_m) const
{
    // No relief drawn there (not loaded yet, outside the terrain window):
    // the drawn surface is the ellipsoid itself.
    double height_m = 0.0;
    if (!this->surface_height_resolver(render_coordinate, &height_m) || !std::isfinite(height_m))
        height_m = 0.0;
    *surface_height_m = height_m;
    // The same lift as the rest of the network above coincident terrain.
    return height_m + this->vertical_transform.networkLiftM();
}

void MapGlobeNetworkScene::moveDemandPoint(
    quint32 render_id, int icon_marker_index, const QVector3D &center)
{
    const quint64 key = entityRenderKey(InfrastructureEntity::DemandPoint, render_id);
    const QHash<quint64, QVector<int>>::const_iterator node_indices =
        this->node_vertex_indices_by_entity.constFind(key);
    if (node_indices != this->node_vertex_indices_by_entity.cend())
    {
        for (int vertex_index : node_indices.value())
        {
            MapNetworkNodeVertex &vertex = this->node_vertices[vertex_index];
            vertex.center_x = center.x();
            vertex.center_y = center.y();
            vertex.center_z = center.z();
        }
    }
    // The demand point's only link segment is its attachment line, which
    // starts at the demand point; its end stays on the junction / pipe.
    const QHash<quint64, QVector<int>>::const_iterator link_indices =
        this->link_vertex_indices_by_entity.constFind(key);
    if (link_indices != this->link_vertex_indices_by_entity.cend() && !link_indices.value().isEmpty())
    {
        const MapNetworkLinkVertex &first = this->link_vertices.at(link_indices.value().first());
        const QVector3D end(first.end_x, first.end_y, first.end_z);
        const QVector3D width_direction = linkWidthDirection(center, end);
        for (int vertex_index : link_indices.value())
        {
            MapNetworkLinkVertex &vertex = this->link_vertices[vertex_index];
            vertex.start_x = center.x();
            vertex.start_y = center.y();
            vertex.start_z = center.z();
            vertex.width_direction_x = width_direction.x();
            vertex.width_direction_y = width_direction.y();
            vertex.width_direction_z = width_direction.z();
        }
    }
    if (icon_marker_index >= 0 && icon_marker_index < this->icon_markers.size())
        this->icon_markers[icon_marker_index].center = center;
}

bool MapGlobeNetworkScene::refreshDemandPointHeights()
{
    if (!this->surface_height_resolver || this->draped_demand_points.isEmpty())
        return false;

    // Below 5 cm nothing visible moves (2 m network lift; the drawn-surface
    // samples carry float noise of the camera-relative terrain vertices).
    constexpr double HeightToleranceM = 0.05;
    bool moved = false;
    for (DrapedDemandPoint &point : this->draped_demand_points)
    {
        double surface_height_m = 0.0;
        const double height_m = drapedHeightM(point.render_coordinate, &surface_height_m);
        if (std::abs(surface_height_m - point.surface_height_m) < HeightToleranceM)
            continue;
        point.surface_height_m = surface_height_m;
        moveDemandPoint(point.render_id, point.icon_marker_index,
                        ecefPositionAtHeight(point.render_coordinate, height_m));
        moved = true;
    }
    if (moved)
    {
        // Icons and highlights are copies of the moved vertices.
        rebuildIcons();
        rebuildHighlights();
    }
    return moved;
}

bool MapGlobeNetworkScene::refreshDrapedPipeBendHeights()
{
    if (!this->surface_height_resolver || this->draped_pipe_bends.isEmpty())
        return false;

    // LOD changes and camera movement can notify us without changing the
    // actual surface beneath any pipe bend. Avoid rebuilding GPU buffers on
    // those notifications. A real change needs new connected segments.
    constexpr double HeightToleranceM = 0.05;
    for (const DrapedPipeBend &bend : this->draped_pipe_bends)
    {
        double surface_height_m = 0.0;
        drapedHeightM(bend.coordinate, &surface_height_m);
        if (std::abs(surface_height_m - bend.surface_height_m) >= HeightToleranceM)
        {
            rebuildNetworkGeometry();
            return true;
        }
    }
    return false;
}

bool MapGlobeNetworkScene::setRenderOriginEcef(
    const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef)
{
    // A camera rebase is NOT a geometry revision. Only an empty/uninitialized
    // retained scene may adopt the camera's origin once. Normal snapshots
    // already established their own geometry-local anchor.
    const bool initialized = this->retained_origin.initializeFromCamera(origin_ecef);
    if (initialized && this->retained_network.hasSnapshot())
        rebuildNetworkGeometry();
    return initialized;
}

GeoWgs84Ellipsoid::EcefPositionD MapGlobeNetworkScene::renderOriginEcef() const
{
    return this->retained_origin.originEcef();
}

bool MapGlobeNetworkScene::setUndergroundXRayEnabled(bool enabled)
{
    if (this->underground_xray_enabled == enabled)
        return false;

    this->underground_xray_enabled = enabled;
    if (enabled)
        rebuildUndergroundXRayGeometry();
    else
    {
        this->underground_link_vertices.clear();
        this->underground_junction_instances.clear();
    }
    return true;
}

bool MapGlobeNetworkScene::refreshUndergroundXRayGeometry()
{
    if (!this->underground_xray_enabled
        || !this->terrain_elevation_resolver
        || (this->retained_network.snapshot().links.isEmpty()
            && this->retained_network.snapshot().nodes.isEmpty()))
    {
        return false;
    }

    rebuildUndergroundXRayGeometry();
    return true;
}

bool MapGlobeNetworkScene::setFlowDirectionPixelsPerMeter(double pixels_per_meter)
{
    const double bounded_pixels_per_meter =
        std::isfinite(pixels_per_meter) && pixels_per_meter > 0.0
        ? pixels_per_meter
        : 0.0;
    if (qFuzzyCompare(
            1.0 + this->flow_direction_pixels_per_meter,
            1.0 + bounded_pixels_per_meter))
    {
        return false;
    }

    this->flow_direction_pixels_per_meter = bounded_pixels_per_meter;
    applySceneInvalidation(MapGlobeSceneSync::forFlowScale(
        this->use_3d_icon_models,
        this->symbology.icon_size_unit == NetworkSymbologySizeUnit::Pixels));
    return true;
}

const QVector<MapNetworkLinkVertex> &MapGlobeNetworkScene::linkVertices() const
{
    return this->link_vertices;
}

const QVector<MapNetworkNodeVertex> &MapGlobeNetworkScene::nodeVertices() const
{
    return this->node_vertices;
}

const QVector<MapNetworkLinkVertex> &MapGlobeNetworkScene::selectedLinkVertices() const
{
    return this->selected_link_vertices;
}

const QVector<MapNetworkNodeVertex> &MapGlobeNetworkScene::selectedNodeVertices() const
{
    return this->selected_node_vertices;
}

const QVector<MapNetworkLinkVertex> &MapGlobeNetworkScene::diagnosticLinkVertices() const
{
    return this->diagnostic_link_vertices;
}

const QVector<MapNetworkNodeVertex> &MapGlobeNetworkScene::diagnosticNodeVertices() const
{
    return this->diagnostic_node_vertices;
}

const QVector<MapNetworkLinkVertex> &MapGlobeNetworkScene::flowDirectionVertices() const
{
    return this->flow_direction_vertices;
}

const QVector<MapNetworkIconVertex> &MapGlobeNetworkScene::iconVertices() const
{
    return this->icon_vertices;
}

const QVector<MapGlobeTankInstance> &MapGlobeNetworkScene::tankInstances() const
{
    return this->tank_instances;
}

const QVector<MapGlobeReservoirInstance> &MapGlobeNetworkScene::reservoirInstances() const
{
    return this->reservoir_instances;
}

const QVector<MapNetworkLinkVertex> &MapGlobeNetworkScene::undergroundLinkVertices() const
{
    return this->underground_link_vertices;
}

const QVector<MapGlobeJunctionInstance> &
MapGlobeNetworkScene::undergroundJunctionInstances() const
{
    return this->underground_junction_instances;
}

const QVector<MapGlobeJunctionInstance> &MapGlobeNetworkScene::junctionInstances() const
{
    return this->junction_instances;
}

quint64 MapGlobeNetworkScene::geometryRevision() const
{
    return this->geometry_revision;
}

bool MapGlobeNetworkScene::hasGeometry() const
{
    return !this->link_vertices.isEmpty()
        || !this->node_vertices.isEmpty()
        || !this->junction_instances.isEmpty();
}

NetworkSymbologySizeUnit MapGlobeNetworkScene::nodeSizeUnit() const
{
    return this->symbology.node_size_unit;
}

int MapGlobeNetworkScene::nodeSizePx() const
{
    return this->symbology.node_size_px;
}

double MapGlobeNetworkScene::nodeSizeM() const
{
    return this->symbology.node_size_m;
}

NetworkSymbologySizeUnit MapGlobeNetworkScene::iconSizeUnit() const
{
    return this->symbology.icon_size_unit;
}

int MapGlobeNetworkScene::iconSizePx() const
{
    return this->symbology.icon_size_px;
}

double MapGlobeNetworkScene::iconSizeM() const
{
    return this->symbology.icon_size_m;
}

NetworkSymbologySizeUnit MapGlobeNetworkScene::linkThicknessUnit() const
{
    return this->symbology.link_thickness_unit;
}

int MapGlobeNetworkScene::linkThicknessPx() const
{
    return this->symbology.link_thickness_px;
}

double MapGlobeNetworkScene::linkThicknessM() const
{
    return this->symbology.link_thickness_m;
}

QVector3D MapGlobeNetworkScene::ecefPosition(
    const CoordinateWGS84 &coordinate, double elevation_m) const
{
    // Always uses the entity's own real elevation, exactly like
    // the flat TwoD scene already
    // does for its own node/link placement -- no separate "is terrain
    // ready yet" gate. An earlier version of this function pinned
    // everything to the bare ellipsoid (elevation 0) until
    // Globe terrain renderer::isVisibleTerrainReady() reported the visible
    // tile window's relief as loaded, to avoid a first-load flash of
    // network floating above still-flat terrain. In practice that gate
    // caused far more visible harm than it prevented: the visible tile
    // window changes continuously during ordinary orbiting/panning (tiles
    // enter/leave view, LOD subdivides/merges, edge stitching gets
    // recomputed), each of which could flip "ready" back to false and
    // snap the *entire* network down to the bare ellipsoid and back --
    // exactly the "jumps between underground and surface" / "flickers
    // during camera movement" symptom this class was fighting. Simply not
    // having the gate is both simpler and correct: any
    // brief mismatch between network and not-yet-loaded terrain during
    // the very first frames of a session resolves itself once relief
    // arrives without any
    // equivalent synchronization dance.
    return ecefPositionAtHeight(coordinate, this->vertical_transform.networkHeightM(elevation_m));
}

QVector3D MapGlobeNetworkScene::ecefPositionAtHeight(
    const CoordinateWGS84 &coordinate, double height_m) const
{
    // Computed in double (geodeticToEcefD(), not geodeticToEcef()) and
    // subtracted against the retained network ECEF anchor -- also double -- before ever
    // narrowing to the QVector3D (float32) this function returns. Both
    // operands of that subtraction carry full ECEF-scale (~6.378e6 m)
    // precision right up until the subtraction happens, so the result is
    // accurate to double precision; only *after* subtracting does the
    // magnitude drop to something small for a geographically local network
    // (bounded by its extent from the retained anchor), which is where
    // narrowing to float32 becomes safe. Extremely wide networks will use
    // multiple chunk-local frames in the future GPU backend. Getting the order of
    // operations right here is the entire fix: computing raw ECEF in
    // float32 first (as geodeticToEcef() does) and subtracting afterward,
    // or subtracting two float32 ECEF positions directly, both reintroduce
    // the ~0.5-1m-at-best precision floor this is specifically avoiding --
    // see the class comment above and the EcefPositionD comment in
    // geo_wgs84_ellipsoid.h for the full reasoning.
    const GeoWgs84Ellipsoid::EcefPositionD absolute_ecef = GeoWgs84Ellipsoid::geodeticToEcefD(
        coordinate.longitude_deg, coordinate.latitude_deg, height_m);
    return QVector3D(
        float(absolute_ecef.x - this->retained_origin.originEcef().x),
        float(absolute_ecef.y - this->retained_origin.originEcef().y),
        float(absolute_ecef.z - this->retained_origin.originEcef().z));
}

void MapGlobeNetworkScene::rebuildNetworkGeometry()
{
    const MapRenderDiagnostics::Scope timing(MapRenderDiagnostics::Counter::GlobeGeometryRebuilt);
    MapRenderDiagnostics::add(MapRenderDiagnostics::Counter::DemandAttachmentsPrepared,
        quint64(this->retained_network.snapshot().demand_point_attachments.size()));
    this->link_vertices.clear();
    this->node_vertices.clear();
    this->selected_link_vertices.clear();
    this->selected_node_vertices.clear();
    this->diagnostic_link_vertices.clear();
    this->diagnostic_node_vertices.clear();
    this->flow_direction_vertices.clear();
    this->icon_vertices.clear();
    this->icon_markers.clear();
    this->junction_markers.clear();
    this->link_paths.clear();
    this->underground_link_vertices.clear();
    this->draped_demand_points.clear();
    this->draped_pipe_bends.clear();
    this->entity_keys_by_uuid.clear();
    this->link_vertex_indices_by_entity.clear();
    this->node_vertex_indices_by_entity.clear();
    this->geometry_revision = this->retained_network.snapshot().geometry_revision;

    // Used only as a placeholder height for vertices whose own elevation is
    // missing/non-finite, so an incomplete dataset still lands near the rest
    // of the network instead of at the bare 0 m ellipsoid surface (which, for
    // a network sitting at real elevation, could be far enough below the
    // globe's DEM-displaced terrain to look buried).
    bool fallback_elevation_initialized = false;
    this->fallback_elevation_m = 0.0;
    for (const NetworkRenderNode &node : this->retained_network.snapshot().nodes)
    {
        if (!finiteCoordinate(node.coordinate_wgs84) || !std::isfinite(node.elevation_m))
            continue;

        this->fallback_elevation_m = fallback_elevation_initialized
            ? qMin(this->fallback_elevation_m, node.elevation_m)
            : node.elevation_m;
        fallback_elevation_initialized = true;
    }

    struct PreparedNode
    {
        const NetworkRenderNode *node = nullptr;
        double elevation_m = 0.0;
    };

    QVector<PreparedNode> prepared_nodes;
    prepared_nodes.reserve(this->retained_network.snapshot().nodes.size());
    for (const NetworkRenderNode &node : this->retained_network.snapshot().nodes)
    {
        if (this->hidden_entity_uuids.contains(node.uuid)
            || !finiteCoordinate(node.coordinate_wgs84))
        {
            continue;
        }

        const double elevation_m = std::isfinite(node.elevation_m)
            ? node.elevation_m : this->fallback_elevation_m;
        prepared_nodes.append({&node, elevation_m});
    }

    this->node_declutter_offsets_m.clear();
    if (this->node_decluttering_enabled && prepared_nodes.size() > 1)
    {
        const CoordinateWGS84 &reference_coordinate =
            prepared_nodes.first().node->coordinate_wgs84;
        const GeoWgs84Ellipsoid::EcefPositionD reference_ecef =
            GeoWgs84Ellipsoid::geodeticToEcefD(
                reference_coordinate.longitude_deg,
                reference_coordinate.latitude_deg, 0.0);
        const GeoWgs84Ellipsoid::LocalFrame reference_frame =
            GeoWgs84Ellipsoid::localFrameAtGeodetic(
                reference_coordinate.longitude_deg,
                reference_coordinate.latitude_deg, 0.0);

        QVector<MapNodeDeclutterInput> declutter_inputs;
        declutter_inputs.reserve(prepared_nodes.size());
        for (const PreparedNode &prepared : prepared_nodes)
        {
            const CoordinateWGS84 &coordinate = prepared.node->coordinate_wgs84;
            const GeoWgs84Ellipsoid::EcefPositionD node_ecef =
                GeoWgs84Ellipsoid::geodeticToEcefD(
                    coordinate.longitude_deg, coordinate.latitude_deg, 0.0);
            const double delta_x = node_ecef.x - reference_ecef.x;
            const double delta_y = node_ecef.y - reference_ecef.y;
            const double delta_z = node_ecef.z - reference_ecef.z;
            const QPointF tangent_center(
                delta_x * double(reference_frame.east.x())
                    + delta_y * double(reference_frame.east.y())
                    + delta_z * double(reference_frame.east.z()),
                delta_x * double(reference_frame.north.x())
                    + delta_y * double(reference_frame.north.y())
                    + delta_z * double(reference_frame.north.z()));
            declutter_inputs.append({prepared.node->render_id, tangent_center});
        }

        this->node_declutter_offsets_m = computeNodeDeclutterOffsets(
            declutter_inputs, MapNodeDeclutterMinimumSeparationMeters);
    }

    qsizetype node_quad_count = 0;
    for (const PreparedNode &prepared : prepared_nodes)
    {
        const NetworkRenderNode &node = *prepared.node;
        if (node.entity_type != InfrastructureEntity::Junction)
            ++node_quad_count;
    }
    this->node_vertices.reserve(node_quad_count * 6);
    // Draped demand point positions for their attachment lines below.
    QHash<quint32, QVector3D> draped_demand_point_centers;
    for (const PreparedNode &prepared : prepared_nodes)
    {
        const NetworkRenderNode &node = *prepared.node;
        const CoordinateWGS84 render_coordinate = coordinateWithDeclutterOffset(
            node.coordinate_wgs84, this->node_declutter_offsets_m.value(node.render_id));
        // Demand points lie on the drawn terrain (setSurfaceHeightResolver()).
        const bool draped = node.entity_type == InfrastructureEntity::DemandPoint
            && this->surface_height_resolver;
        DrapedDemandPoint draped_point;
        QVector3D center;
        if (draped)
        {
            draped_point.render_id = node.render_id;
            draped_point.render_coordinate = render_coordinate;
            center = ecefPositionAtHeight(
                render_coordinate, drapedHeightM(render_coordinate, &draped_point.surface_height_m));
            draped_demand_point_centers.insert(node.render_id, center);
        }
        else
        {
            center = ecefPosition(render_coordinate, prepared.elevation_m);
        }
        this->entity_keys_by_uuid.insert(
            node.uuid, entityRenderKey(node.entity_type, node.render_id));
        appendNode(node.entity_type, node.render_id, center);

        if (mapHasIcon(node.entity_type))
        {
            if (draped)
                draped_point.icon_marker_index = int(this->icon_markers.size());
            IconMarker marker;
            marker.entity_type = node.entity_type;
            marker.render_id = node.render_id;
            marker.center = center;
            this->icon_markers.append(marker);
        }

        if (node.entity_type == InfrastructureEntity::Junction)
        {
            JunctionMarker marker;
            marker.render_id = node.render_id;
            marker.center = center;
            this->junction_markers.append(marker);
        }
        if (draped)
            this->draped_demand_points.append(draped_point);
    }

    qsizetype segment_count = 0;
    for (const NetworkRenderLink &link : this->retained_network.snapshot().links)
    {
        if (!this->hidden_entity_uuids.contains(link.uuid))
            segment_count += qMax<qsizetype>(0, link.vertices_wgs84.size() - 1);
    }
    segment_count += this->retained_network.snapshot().demand_point_attachments.size();
    this->link_vertices.reserve(segment_count * 6);

    for (const NetworkRenderLink &link : this->retained_network.snapshot().links)
    {
        if (this->hidden_entity_uuids.contains(link.uuid) || link.vertices_wgs84.size() < 2)
            continue;

        LinkPath link_path;
        link_path.entity_type = link.entity_type;
        link_path.render_id = link.render_id;

        bool have_previous = false;
        QVector3D previous;
        CoordinateWGS84 previous_coordinate;
        double previous_render_height_m = 0.0;
        for (qsizetype vertex_index = 0;
             vertex_index < link.vertices_wgs84.size(); ++vertex_index)
        {
            const CoordinateWGS84 &raw_coordinate = link.vertices_wgs84.at(vertex_index);
            if (!finiteCoordinate(raw_coordinate))
            {
                have_previous = false;
                continue;
            }

            CoordinateWGS84 coordinate = raw_coordinate;
            if (vertex_index == 0)
            {
                coordinate = coordinateWithDeclutterOffset(
                    raw_coordinate,
                    this->node_declutter_offsets_m.value(link.start_node_render_id));
            }
            else if (vertex_index == link.vertices_wgs84.size() - 1)
            {
                coordinate = coordinateWithDeclutterOffset(
                    raw_coordinate,
                    this->node_declutter_offsets_m.value(link.end_node_render_id));
            }

            const double raw_elevation_m = vertex_index < link.elevations_m.size()
                ? link.elevations_m.at(vertex_index)
                : this->fallback_elevation_m;
            const double elevation_m =
                std::isfinite(raw_elevation_m)
                ? raw_elevation_m : this->fallback_elevation_m;
            double render_height_m = this->vertical_transform.networkHeightM(elevation_m);
            // Intermediate pipe bends have only geographic coordinates in the
            // hydraulic model. Their visual height follows the drawn terrain
            // like demand points; the real endpoint elevations stay intact.
            if (link.entity_type == InfrastructureEntity::Pipe && vertex_index > 0
                && vertex_index + 1 < link.vertices_wgs84.size()
                && this->surface_height_resolver)
            {
                double surface_height_m = 0.0;
                render_height_m = drapedHeightM(coordinate, &surface_height_m);
                this->draped_pipe_bends.append({coordinate, surface_height_m});
            }
            const QVector3D current = ecefPositionAtHeight(coordinate, render_height_m);

            if (have_previous)
            {
                GeodesicSpan geodesic_span;
                const bool have_geodesic_span = geodesicSpanBetween(
                    previous_coordinate, coordinate, &geodesic_span);
                const double span_length_m = have_geodesic_span
                    ? geodesic_span.distance_m : 0.0;
                MapGlobeLinkGeometry::emitCurvedSegments(
                    previous, current, span_length_m,
                    [&](double ratio, QVector3D *sample_ecef) -> bool {
                        CoordinateWGS84 sample_coordinate;
                        if (!geodesicCoordinateAtDistance(
                                previous_coordinate, geodesic_span.initial_azimuth_deg,
                                geodesic_span.distance_m * ratio, &sample_coordinate))
                            return false;
                        const double sample_height_m = previous_render_height_m
                            + (render_height_m - previous_render_height_m) * ratio;
                        *sample_ecef = ecefPositionAtHeight(sample_coordinate, sample_height_m);
                        return true;
                    },
                    [&](const QVector3D &segment_start, const QVector3D &segment_end) {
                        appendLinkSegment(
                            link.entity_type, link.render_id, segment_start, segment_end);
                        const float segment_length_m =
                            (segment_end - segment_start).length();
                        link_path.segments.append(
                            {segment_start, segment_end, segment_length_m});
                        link_path.total_length_m += double(segment_length_m);
                    });
            }

            previous = current;
            previous_coordinate = coordinate;
            previous_render_height_m = render_height_m;
            have_previous = true;
        }

        this->entity_keys_by_uuid.insert(
            link.uuid, entityRenderKey(link.entity_type, link.render_id));

        if (!link_path.segments.isEmpty())
        {
            if (mapHasIcon(link.entity_type))
            {
                const float total_length = float(link_path.total_length_m);

                if (total_length > 0.0f)
                {
                    const float target = total_length / 2.0f;
                    float traversed = 0.0f;
                    for (const SceneSegment &segment : link_path.segments)
                    {
                        const float segment_length = (segment.end - segment.start).length();
                        if (segment_length <= 0.0f)
                            continue;
                        if (traversed + segment_length < target)
                        {
                            traversed += segment_length;
                            continue;
                        }

                        const float ratio = qBound(
                            0.0f, (target - traversed) / segment_length, 1.0f);
                        IconMarker marker;
                        marker.entity_type = link.entity_type;
                        marker.render_id = link.render_id;
                        marker.center = segment.start
                            + (segment.end - segment.start) * ratio;
                        this->icon_markers.append(marker);
                        break;
                    }
                }
            }
            this->link_paths.append(link_path);
        }
    }

    rebuildJunctionInstances();
    rebuildUndergroundXRayGeometry();
    rebuildFlowDirections();
    for (const NetworkRenderDemandPointAttachment &attachment :
         this->retained_network.snapshot().demand_point_attachments)
    {
        if (this->hidden_entity_uuids.contains(attachment.demand_point_uuid)
            || !finiteCoordinate(attachment.demand_point_coordinate_wgs84)
            || !finiteCoordinate(attachment.attachment_coordinate_wgs84))
        {
            continue;
        }

        // From the demand point (draped on the terrain, or at the fallback
        // elevation without a surface resolver) to the junction / pipe at
        // its own elevation.
        const QHash<quint32, QVector3D>::const_iterator draped_center =
            draped_demand_point_centers.constFind(attachment.demand_point_render_id);
        QVector3D start;
        if (draped_center != draped_demand_point_centers.cend())
        {
            start = draped_center.value();
        }
        else
        {
            const CoordinateWGS84 demand_point_render_coordinate = coordinateWithDeclutterOffset(
                attachment.demand_point_coordinate_wgs84,
                this->node_declutter_offsets_m.value(attachment.demand_point_render_id));
            start = ecefPosition(demand_point_render_coordinate, this->fallback_elevation_m);
        }
        const double attachment_elevation_m = std::isfinite(attachment.attachment_elevation_m)
            ? attachment.attachment_elevation_m : this->fallback_elevation_m;
        const QVector3D end = ecefPosition(attachment.attachment_coordinate_wgs84, attachment_elevation_m);
        const qsizetype first_vertex = this->link_vertices.size();
        appendLinkSegment(
            InfrastructureEntity::DemandPoint, attachment.demand_point_render_id,
            start, end);
        const float attachment_half_width_px = 0.75f;
        const float base_half_width_px = float(this->symbology.link_thickness_px) / 2.0f;
        const float attachment_size_adjust_px = this->symbology.link_thickness_unit
                == NetworkSymbologySizeUnit::Meters
            ? -attachment_half_width_px
            : attachment_half_width_px - base_half_width_px;
        for (qsizetype index = first_vertex; index < this->link_vertices.size(); ++index)
            this->link_vertices[index].size_adjust_px = attachment_size_adjust_px;
    }

    rebuildIcons();
    rebuildTankInstances();
    rebuildReservoirInstances();
    rebuildHighlights();
}

void MapGlobeNetworkScene::rebuildUndergroundXRayGeometry()
{
    this->underground_link_vertices.clear();
    this->underground_junction_instances.clear();
    if (!this->underground_xray_enabled || !this->terrain_elevation_resolver)
        return;

    for (const NetworkRenderLink &link : this->retained_network.snapshot().links)
    {
        if (this->hidden_entity_uuids.contains(link.uuid)
            || link.vertices_wgs84.size() < 2)
        {
            continue;
        }

        bool have_previous = false;
        QVector3D previous;
        CoordinateWGS84 previous_coordinate;
        double previous_elevation_m = 0.0;
        for (qsizetype vertex_index = 0;
             vertex_index < link.vertices_wgs84.size(); ++vertex_index)
        {
            const CoordinateWGS84 &raw_coordinate = link.vertices_wgs84.at(vertex_index);
            if (!finiteCoordinate(raw_coordinate))
            {
                have_previous = false;
                continue;
            }

            CoordinateWGS84 coordinate = raw_coordinate;
            if (vertex_index == 0)
            {
                coordinate = coordinateWithDeclutterOffset(
                    raw_coordinate,
                    this->node_declutter_offsets_m.value(link.start_node_render_id));
            }
            else if (vertex_index == link.vertices_wgs84.size() - 1)
            {
                coordinate = coordinateWithDeclutterOffset(
                    raw_coordinate,
                    this->node_declutter_offsets_m.value(link.end_node_render_id));
            }

            const double raw_elevation_m = vertex_index < link.elevations_m.size()
                ? link.elevations_m.at(vertex_index)
                : this->fallback_elevation_m;
            double elevation_m = std::isfinite(raw_elevation_m)
                ? raw_elevation_m : this->fallback_elevation_m;
            if (link.entity_type == InfrastructureEntity::Pipe && vertex_index > 0
                && vertex_index + 1 < link.vertices_wgs84.size()
                && this->surface_height_resolver)
            {
                double surface_height_m = 0.0;
                const double render_height_m = drapedHeightM(coordinate, &surface_height_m);
                // X-Ray must classify the same terrain-draped centerline as
                // the normal renderer, not the unrelated model interpolation.
                elevation_m = (render_height_m - this->vertical_transform.networkLiftM())
                    / this->vertical_transform.verticalExaggeration();
            }
            const QVector3D current = ecefPosition(coordinate, elevation_m);

            if (have_previous)
            {
                appendUndergroundSubdivisions(
                    link.entity_type, link.render_id,
                    previous_coordinate, previous_elevation_m, previous,
                    coordinate, elevation_m, current);
            }

            previous = current;
            previous_coordinate = coordinate;
            previous_elevation_m = elevation_m;
            have_previous = true;
        }
    }

    if (this->junction_instances.isEmpty())
        return;

    QHash<quint32, MapGlobeJunctionInstance> junction_instances_by_render_id;
    junction_instances_by_render_id.reserve(this->junction_instances.size());
    for (const MapGlobeJunctionInstance &instance : this->junction_instances)
        junction_instances_by_render_id.insert(instance.render_id, instance);

    const double rendered_underground_tolerance_m =
        this->vertical_transform.renderedVerticalDistanceM(UndergroundToleranceM);
    this->underground_junction_instances.reserve(this->junction_instances.size());
    for (const NetworkRenderNode &node : this->retained_network.snapshot().nodes)
    {
        if (node.entity_type != InfrastructureEntity::Junction
            || this->hidden_entity_uuids.contains(node.uuid)
            || !finiteCoordinate(node.coordinate_wgs84))
        {
            continue;
        }

        const double elevation_m = std::isfinite(node.elevation_m)
            ? node.elevation_m : this->fallback_elevation_m;
        const CoordinateWGS84 render_coordinate = coordinateWithDeclutterOffset(
            node.coordinate_wgs84,
            this->node_declutter_offsets_m.value(node.render_id));

        double terrain_elevation_m = 0.0;
        double terrain_cell_size_m = 0.0;
        if (!this->terrain_elevation_resolver(
                render_coordinate, &terrain_elevation_m, &terrain_cell_size_m)
            || this->vertical_transform.networkDepthBelowTerrainM(
                    elevation_m, terrain_elevation_m)
                <= rendered_underground_tolerance_m)
        {
            continue;
        }

        const QHash<quint32, MapGlobeJunctionInstance>::const_iterator instance_iterator =
            junction_instances_by_render_id.constFind(node.render_id);
        if (instance_iterator != junction_instances_by_render_id.cend())
            this->underground_junction_instances.append(instance_iterator.value());
    }
}

QVector3D MapGlobeNetworkScene::linkWidthDirection(
    const QVector3D &start, const QVector3D &end) const
{
    if ((end - start).lengthSquared() <= 1e-12f)
        return QVector3D();

    const QVector3D midpoint_relative = (start + end) * 0.5f;
    GeoWgs84Ellipsoid::EcefPositionD midpoint_ecef;
    midpoint_ecef.x = this->retained_origin.originEcef().x + double(midpoint_relative.x());
    midpoint_ecef.y = this->retained_origin.originEcef().y + double(midpoint_relative.y());
    midpoint_ecef.z = this->retained_origin.originEcef().z + double(midpoint_relative.z());

    double longitude_deg = 0.0;
    double latitude_deg = 0.0;
    if (!GeoWgs84Ellipsoid::ecefToGeodetic(
            midpoint_ecef, &longitude_deg, &latitude_deg))
        return QVector3D();

    const GeoWgs84Ellipsoid::LocalFrame local_frame =
        GeoWgs84Ellipsoid::localFrameAtGeodetic(longitude_deg, latitude_deg, 0.0);
    return MapGlobeLinkGeometry::widthDirection(start, end, local_frame.up);
}

void MapGlobeNetworkScene::appendLinkSegment(
    InfrastructureEntity entity_type, quint32 render_id,
    const QVector3D &start, const QVector3D &end)
{
    const quint64 entity_key = entityRenderKey(entity_type, render_id);
    const QVector3D width_direction = linkWidthDirection(start, end);
    MapGlobeLinkGeometry::emitRibbonCorners(
        start, end, width_direction,
        [&](const MapGlobeRibbonCorner &corner) {
            MapNetworkLinkVertex vertex;
            vertex.start_x = corner.start.x();
            vertex.start_y = corner.start.y();
            vertex.start_z = corner.start.z();
            vertex.end_x = corner.end.x();
            vertex.end_y = corner.end.y();
            vertex.end_z = corner.end.z();
            vertex.along = corner.along;
            vertex.side = corner.side;
            vertex.width_direction_x = corner.width_direction.x();
            vertex.width_direction_y = corner.width_direction.y();
            vertex.width_direction_z = corner.width_direction.z();
            vertex.render_id = render_id;
            vertex.entity_type = entity_type;
            applyLinkColor(&vertex);
            this->link_vertices.append(vertex);
            this->link_vertex_indices_by_entity[entity_key].append(this->link_vertices.size() - 1);
        });
}

void MapGlobeNetworkScene::appendUndergroundLinkSegment(
    InfrastructureEntity entity_type, quint32 render_id,
    const QVector3D &start, const QVector3D &end)
{
    const QVector3D width_direction = linkWidthDirection(start, end);
    MapGlobeLinkGeometry::emitRibbonCorners(
        start, end, width_direction,
        [&](const MapGlobeRibbonCorner &corner) {
            MapNetworkLinkVertex vertex;
            vertex.start_x = corner.start.x();
            vertex.start_y = corner.start.y();
            vertex.start_z = corner.start.z();
            vertex.end_x = corner.end.x();
            vertex.end_y = corner.end.y();
            vertex.end_z = corner.end.z();
            vertex.along = corner.along;
            vertex.side = corner.side;
            vertex.render_id = render_id;
            vertex.width_direction_x = corner.width_direction.x();
            vertex.width_direction_y = corner.width_direction.y();
            vertex.width_direction_z = corner.width_direction.z();
            vertex.entity_type = entity_type;
            // The X-Ray path draws the entity's normal colour, dashed.
            applyLinkColor(&vertex);
            this->underground_link_vertices.append(vertex);
        });
}

void MapGlobeNetworkScene::appendUndergroundCurvedRun(
    InfrastructureEntity entity_type, quint32 render_id,
    const CoordinateWGS84 &start_coordinate, double start_elevation_m,
    const QVector3D &start_ecef,
    const CoordinateWGS84 &end_coordinate, double end_elevation_m,
    const QVector3D &end_ecef)
{
    GeodesicSpan geodesic_span;
    const bool have_geodesic_span = geodesicSpanBetween(
        start_coordinate, end_coordinate, &geodesic_span);
    const double span_length_m = have_geodesic_span
        ? geodesic_span.distance_m : 0.0;
    MapGlobeLinkGeometry::emitCurvedSegments(
        start_ecef, end_ecef, span_length_m,
        [&](double ratio, QVector3D *sample_ecef) -> bool {
            CoordinateWGS84 sample_coordinate;
            if (!geodesicCoordinateAtDistance(
                    start_coordinate, geodesic_span.initial_azimuth_deg,
                    geodesic_span.distance_m * ratio, &sample_coordinate))
                return false;
            const double sample_elevation_m = start_elevation_m
                + (end_elevation_m - start_elevation_m) * ratio;
            *sample_ecef = ecefPosition(sample_coordinate, sample_elevation_m);
            return true;
        },
        [&](const QVector3D &segment_start, const QVector3D &segment_end) {
            appendUndergroundLinkSegment(
                entity_type, render_id, segment_start, segment_end);
        });
}

void MapGlobeNetworkScene::appendUndergroundSubdivisions(
    InfrastructureEntity entity_type, quint32 render_id,
    const CoordinateWGS84 &start_coordinate, double start_elevation_m,
    const QVector3D &start_ecef,
    const CoordinateWGS84 &end_coordinate, double end_elevation_m,
    const QVector3D &end_ecef)
{
    GeodesicSpan geodesic_span;
    const bool have_geodesic_span = geodesicSpanBetween(
        start_coordinate, end_coordinate, &geodesic_span);
    const double span_length_m = have_geodesic_span
        ? geodesic_span.distance_m
        : double((end_ecef - start_ecef).length());

    // Only the host knows the GeographicLib geodesic and terrain LOD. Render
    // owns the sample density and contiguous buried-run policy; intermediate
    // classification samples are not individual dashed draw segments.
    const std::function<CoordinateWGS84(double)> coordinate_at_ratio =
        [&](double ratio) -> CoordinateWGS84 {
            if (ratio >= 1.0)
                return end_coordinate;
            CoordinateWGS84 coordinate = start_coordinate;
            if (ratio > 0.0 && have_geodesic_span)
            {
                geodesicCoordinateAtDistance(
                    start_coordinate, geodesic_span.initial_azimuth_deg,
                    geodesic_span.distance_m * ratio, &coordinate);
            }
            else if (ratio > 0.0)
            {
                // Same defensive linear fallback as the previous X-Ray path.
                coordinate.longitude_deg = start_coordinate.longitude_deg
                    + (end_coordinate.longitude_deg - start_coordinate.longitude_deg) * ratio;
                coordinate.latitude_deg = start_coordinate.latitude_deg
                    + (end_coordinate.latitude_deg - start_coordinate.latitude_deg) * ratio;
            }
            return coordinate;
        };

    const int subdivision_count = MapGlobeUndergroundGeometry::subdivisionCount(
        span_length_m,
        [&](double ratio) -> double {
            double ignored_elevation_m = 0.0;
            double terrain_cell_size_m = 0.0;
            if (this->terrain_elevation_resolver
                && this->terrain_elevation_resolver(
                    coordinate_at_ratio(ratio), &ignored_elevation_m,
                    &terrain_cell_size_m))
            {
                return terrain_cell_size_m;
            }
            return 0.0;
        });

    const double rendered_underground_tolerance_m =
        this->vertical_transform.renderedVerticalDistanceM(UndergroundToleranceM);
    MapGlobeUndergroundGeometry::emitBuriedRuns(
        subdivision_count,
        [&](double ratio) -> bool {
            const CoordinateWGS84 coordinate = coordinate_at_ratio(ratio);
            const double elevation_m = start_elevation_m
                + (end_elevation_m - start_elevation_m) * ratio;
            double terrain_elevation_m = 0.0;
            double terrain_cell_size_m = 0.0;
            return this->terrain_elevation_resolver
                && this->terrain_elevation_resolver(
                    coordinate, &terrain_elevation_m, &terrain_cell_size_m)
                && this->vertical_transform.networkDepthBelowTerrainM(
                    elevation_m, terrain_elevation_m)
                    > rendered_underground_tolerance_m;
        },
        [&](double start_ratio, double end_ratio) {
            const CoordinateWGS84 run_start = coordinate_at_ratio(start_ratio);
            const CoordinateWGS84 run_end = coordinate_at_ratio(end_ratio);
            const double run_start_elevation_m = start_elevation_m
                + (end_elevation_m - start_elevation_m) * start_ratio;
            const double run_end_elevation_m = start_elevation_m
                + (end_elevation_m - start_elevation_m) * end_ratio;
            const QVector3D run_start_ecef = start_ratio <= 0.0
                ? start_ecef : (start_ratio >= 1.0
                    ? end_ecef : ecefPosition(run_start, run_start_elevation_m));
            const QVector3D run_end_ecef = end_ratio <= 0.0
                ? start_ecef : (end_ratio >= 1.0
                    ? end_ecef : ecefPosition(run_end, run_end_elevation_m));
            appendUndergroundCurvedRun(
                entity_type, render_id,
                run_start, run_start_elevation_m, run_start_ecef,
                run_end, run_end_elevation_m, run_end_ecef);
        });
}

void MapGlobeNetworkScene::appendNode(
    InfrastructureEntity entity_type, quint32 render_id, const QVector3D &center)
{
    // Globe junctions are represented only by compact sphere impostor
    // instances. A transparent node quad would still consume six vertices
    // and base-node bandwidth for every junction.
    if (entity_type == InfrastructureEntity::Junction)
        return;

    const quint64 entity_key = entityRenderKey(entity_type, render_id);
    const float corners[6][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f}
    };

    for (int index = 0; index < 6; ++index)
    {
        MapNetworkNodeVertex vertex;
        vertex.center_x = center.x();
        vertex.center_y = center.y();
        vertex.center_z = center.z();
        vertex.corner_x = corners[index][0];
        vertex.corner_y = corners[index][1];
        vertex.metric_billboard = 1.0f;
        vertex.render_id = render_id;
        vertex.entity_type = entity_type;
        applyNodeColor(&vertex);
        this->node_vertices.append(vertex);
        this->node_vertex_indices_by_entity[entity_key].append(this->node_vertices.size() - 1);
    }
}

void MapGlobeNetworkScene::applyLinkColor(MapNetworkLinkVertex *vertex) const
{
    // Demand point attachments take their demand point's colour.
    mapNetworkSetEntityReference(vertex, vertex->render_id,
                                 vertex->entity_type != InfrastructureEntity::DemandPoint);
}

void MapGlobeNetworkScene::applyNodeColor(MapNetworkNodeVertex *vertex) const
{
    // The shader also drops the marker under a shown icon.
    mapNetworkSetEntityReference(vertex, vertex->render_id, false);
}

void MapGlobeNetworkScene::rebuildFlowDirections()
{
    const MapRenderDiagnostics::Scope timing(MapRenderDiagnostics::Counter::GlobeFlowRebuilt);
    this->flow_direction_vertices.clear();

    // Built once per scale for every link long enough, in both
    // orientations: the shader shows the set matching the link's current
    // flow direction (per-entity GPU data), so time steps never rebuild them.
    if (!this->symbology.show_flow_direction
        || this->symbology.flow_direction_size_px <= 0
        || this->symbology.flow_directions.isEmpty()
        || !std::isfinite(this->flow_direction_pixels_per_meter)
        || this->flow_direction_pixels_per_meter <= 0.0)
    {
        return;
    }

    const MapGlobeFlowSettings settings{
        this->flow_direction_pixels_per_meter,
        this->symbology.flow_direction_size_px,
        this->symbology.link_thickness_px
    };
    const GeoWgs84Ellipsoid::EcefPositionD &origin = this->retained_origin.originEcef();
    const MapGlobeFlowFrame frame{
        origin.x, origin.y, origin.z,
        GeoWgs84Ellipsoid::EquatorialRadiusM,
        GeoWgs84Ellipsoid::PolarRadiusM
    };

    qsizetype estimated_marker_count = 0;
    for (const LinkPath &path : this->link_paths)
        estimated_marker_count += MapGlobeFlowGeometry::markerCount(path, settings);
    this->flow_direction_vertices.reserve(estimated_marker_count * 24);

    for (const LinkPath &path : this->link_paths)
    {
        MapGlobeFlowGeometry::emitStrokes(
            path, path.entity_type == InfrastructureEntity::Pipe,
            settings, frame,
            [this](const MapGlobeFlowStroke &stroke)
            {
                appendFlowDirectionStroke(
                    stroke.start, stroke.end, stroke.render_id,
                    stroke.orientation, stroke.half_width_px);
            });
    }
}

void MapGlobeNetworkScene::appendFlowDirectionStroke(
    const QVector3D &start, const QVector3D &end,
    quint32 render_id, float orientation, float half_width_px)
{
    const float corners[6][2] = {
        {0.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, -1.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    const float base_half_width_px = float(this->symbology.link_thickness_px) / 2.0f;

    for (int index = 0; index < 6; ++index)
    {
        MapNetworkLinkVertex vertex;
        vertex.start_x = start.x();
        vertex.start_y = start.y();
        vertex.start_z = start.z();
        vertex.end_x = end.x();
        vertex.end_y = end.y();
        vertex.end_z = end.z();
        vertex.along = corners[index][0];
        vertex.side = corners[index][1];
        vertex.render_id = render_id;
        vertex.entity_type = InfrastructureEntity::Pipe;
        mapNetworkSetEntityReference(&vertex, render_id, true, orientation);
        vertex.size_adjust_px = this->symbology.link_thickness_unit
                == NetworkSymbologySizeUnit::Meters
            ? -half_width_px
            : half_width_px - base_half_width_px;
        this->flow_direction_vertices.append(vertex);
    }
}

void MapGlobeNetworkScene::rebuildIcons()
{
    this->icon_vertices.clear();
    if (!this->symbology.show_icons)
        return;

    this->icon_vertices.reserve(this->icon_markers.size() * 6);
    for (const IconMarker &marker : this->icon_markers)
        appendIcon(marker);
}

void MapGlobeNetworkScene::appendIcon(const IconMarker &marker)
{
    if (this->use_3d_icon_models
        && (marker.entity_type == InfrastructureEntity::Tank
            || marker.entity_type == InfrastructureEntity::Reservoir))
    {
        return;
    }

    const MapIconAtlasEntry atlas_entry = mapIconAtlasEntry(marker.entity_type);
    if (!atlas_entry.valid)
        return;

    const float half_width_ratio = float(atlas_entry.width_ratio / 2.0);
    const float half_height_ratio = float(atlas_entry.height_ratio / 2.0);
    // Fill colour and the selection tint come from the entity's data.
    const bool node_entity =
        InfrastructureEntityTraits::isHydraulicPointEntity(marker.entity_type);

    const float corners[6][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, -1.0f},
        {1.0f, 1.0f},
        {-1.0f, 1.0f}
    };

    const float u_left = float(atlas_entry.uv_rect.left());
    const float u_right = float(atlas_entry.uv_rect.right());
    const float v_top = float(atlas_entry.uv_rect.top());
    const float v_bottom = float(atlas_entry.uv_rect.bottom());
    for (int index = 0; index < 6; ++index)
    {
        MapNetworkIconVertex vertex;
        vertex.center_x = marker.center.x();
        vertex.center_y = marker.center.y();
        vertex.center_z = marker.center.z();
        vertex.offset_x_ratio = corners[index][0] * half_width_ratio;
        vertex.offset_y_ratio = corners[index][1] * half_height_ratio;
        vertex.u = corners[index][0] < 0.0f ? u_left : u_right;
        vertex.v = corners[index][1] < 0.0f ? v_bottom : v_top;
        mapNetworkSetEntityReference(&vertex, marker.render_id, !node_entity);
        vertex.render_id = marker.render_id;
        vertex.entity_type = marker.entity_type;
        this->icon_vertices.append(vertex);
    }
}

bool MapGlobeNetworkScene::modelBasisAt(
    const QVector3D &center,
    QVector3D *basis_x,
    QVector3D *basis_y,
    QVector3D *basis_z) const
{
    const GeoWgs84Ellipsoid::EcefPositionD &origin = this->retained_origin.originEcef();
    const MapGlobeFlowFrame frame{
        origin.x, origin.y, origin.z,
        GeoWgs84Ellipsoid::EquatorialRadiusM,
        GeoWgs84Ellipsoid::PolarRadiusM
    };
    return MapGlobeFlowGeometry::modelBasisAt(
        center, frame, basis_x, basis_y, basis_z);
}

void MapGlobeNetworkScene::rebuildTankInstances()
{
    this->tank_instances.clear();
    if (!this->use_3d_icon_models || !this->symbology.show_icons)
        return;

    double marker_size_m = 0.0;
    if (this->symbology.icon_size_unit == NetworkSymbologySizeUnit::Meters)
    {
        marker_size_m = this->symbology.icon_size_m;
    }
    else
    {
        if (!(this->flow_direction_pixels_per_meter > 0.0))
            return;
        marker_size_m = this->symbology.icon_size_px / this->flow_direction_pixels_per_meter;
    }
    if (!std::isfinite(marker_size_m) || marker_size_m <= 0.0)
        return;

    quint32 selected_render_id = 0;
    if (this->selected_entity_type == InfrastructureEntity::Tank
        && !this->selected_entity_uuid.isNull())
    {
        const QHash<QUuid, quint64>::const_iterator selected_iterator =
            this->entity_keys_by_uuid.constFind(this->selected_entity_uuid);
        if (selected_iterator != this->entity_keys_by_uuid.cend())
        {
            const quint32 render_id = quint32(selected_iterator.value() & 0xffffffffULL);
            if (entityRenderKey(InfrastructureEntity::Tank, render_id)
                == selected_iterator.value())
            {
                selected_render_id = render_id;
            }
        }
    }

    const float world_marker_size = float(marker_size_m);
    for (const IconMarker &marker : this->icon_markers)
    {
        if (marker.entity_type != InfrastructureEntity::Tank)
            continue;

        QVector3D basis_x;
        QVector3D basis_y;
        QVector3D basis_z;
        if (!modelBasisAt(marker.center, &basis_x, &basis_y, &basis_z))
            continue;

        MapGlobeTankInstance instance;
        instance.render_id = marker.render_id;
        instance.base_center = marker.center + basis_z * 0.02f;
        instance.basis_x = basis_x;
        instance.basis_y = basis_y;
        instance.basis_z = basis_z;
        instance.radius_world = world_marker_size * 0.44f;
        instance.base_height_world = world_marker_size * 0.20f;
        instance.body_height_world = world_marker_size * 0.78f;
        instance.roof_height_world = world_marker_size * 0.26f;
        instance.selected = marker.render_id == selected_render_id ? 1.0f : 0.0f;
        this->tank_instances.append(instance);
    }
}

void MapGlobeNetworkScene::rebuildReservoirInstances()
{
    this->reservoir_instances.clear();
    if (!this->use_3d_icon_models || !this->symbology.show_icons)
        return;

    double marker_size_m = 0.0;
    if (this->symbology.icon_size_unit == NetworkSymbologySizeUnit::Meters)
    {
        marker_size_m = this->symbology.icon_size_m;
    }
    else
    {
        if (!(this->flow_direction_pixels_per_meter > 0.0))
            return;
        marker_size_m = this->symbology.icon_size_px / this->flow_direction_pixels_per_meter;
    }
    if (!std::isfinite(marker_size_m) || marker_size_m <= 0.0)
        return;

    quint32 selected_render_id = 0;
    if (this->selected_entity_type == InfrastructureEntity::Reservoir
        && !this->selected_entity_uuid.isNull())
    {
        const QHash<QUuid, quint64>::const_iterator selected_iterator =
            this->entity_keys_by_uuid.constFind(this->selected_entity_uuid);
        if (selected_iterator != this->entity_keys_by_uuid.cend())
        {
            const quint32 render_id = quint32(selected_iterator.value() & 0xffffffffULL);
            if (entityRenderKey(InfrastructureEntity::Reservoir, render_id)
                == selected_iterator.value())
            {
                selected_render_id = render_id;
            }
        }
    }

    const float world_marker_size = float(marker_size_m);
    for (const IconMarker &marker : this->icon_markers)
    {
        if (marker.entity_type != InfrastructureEntity::Reservoir)
            continue;

        QVector3D basis_x;
        QVector3D basis_y;
        QVector3D basis_z;
        if (!modelBasisAt(marker.center, &basis_x, &basis_y, &basis_z))
            continue;

        MapGlobeReservoirInstance instance;
        instance.render_id = marker.render_id;
        instance.base_center = marker.center + basis_z * 0.02f;
        instance.basis_x = basis_x;
        instance.basis_y = basis_y;
        instance.basis_z = basis_z;
        instance.radius_world = world_marker_size * 0.48f;
        instance.wall_height_world = world_marker_size * 0.34f;
        instance.selected = marker.render_id == selected_render_id ? 1.0f : 0.0f;
        this->reservoir_instances.append(instance);
    }
}

void MapGlobeNetworkScene::rebuildJunctionInstances()
{
    this->junction_instances.clear();
    if (this->junction_markers.isEmpty())
        return;

    // Globe network geometry is already real ECEF meters (see this class's
    // top-of-file comment), so unlike the flat scene junction path,
    // which must convert node_size_m into tangent-plane world units, the
    // configured metre size is usable directly as the sphere's world-space
    // radius. When the configured unit is pixels, renderer backends derive
    // the world-space radius from the current camera scale instead.
    float radius_world = 1.0f;
    if (this->symbology.node_size_unit == NetworkSymbologySizeUnit::Meters)
        radius_world = float(this->symbology.node_size_m * 0.5);

    this->junction_instances.reserve(this->junction_markers.size());
    for (const JunctionMarker &marker : this->junction_markers)
    {
        MapGlobeJunctionInstance instance;
        instance.render_id = marker.render_id;
        instance.style_index = float(marker.render_id);
        instance.center_x = marker.center.x();
        instance.center_y = marker.center.y();
        instance.center_z = marker.center.z();
        instance.radius_world = radius_world;
        this->junction_instances.append(instance);
    }
}

void MapGlobeNetworkScene::rebuildHighlights()
{
    this->selected_link_vertices.clear();
    this->selected_node_vertices.clear();
    this->diagnostic_link_vertices.clear();
    this->diagnostic_node_vertices.clear();

    const bool selected_has_error = !this->selected_entity_uuid.isNull()
        && this->simulation_error_entities.value(
            this->selected_entity_uuid, InfrastructureEntity::Unknown)
                == this->selected_entity_type;

    if (!this->selected_entity_uuid.isNull()
        && this->selected_entity_type != InfrastructureEntity::Unknown)
    {
        const QHash<QUuid, quint64>::const_iterator selected_iterator =
            this->entity_keys_by_uuid.constFind(this->selected_entity_uuid);
        if (selected_iterator != this->entity_keys_by_uuid.cend())
        {
            const quint64 expected_key = entityRenderKey(
                this->selected_entity_type, quint32(selected_iterator.value() & 0xffffffffULL));
            if (expected_key == selected_iterator.value())
            {
                const float base_link_width = float(this->symbology.link_thickness_px);
                const float selected_link_width = qMax(
                    3.0f, base_link_width + (selected_has_error ? 6.0f : 2.0f));
                // Selected 3D node models carry their own selection state,
                // so adding the flat node highlight decal as well would draw
                // a second, unrelated marker through the model. Keep the
                // decal only for node types that are actually rendered as
                // flat markers in the current Globe configuration.
                const bool selected_is_3d_model =
                    this->selected_entity_type == InfrastructureEntity::Junction
                    || (this->use_3d_icon_models
                        && (this->selected_entity_type == InfrastructureEntity::Tank
                            || this->selected_entity_type == InfrastructureEntity::Reservoir));
                QVector<MapNetworkNodeVertex> *selected_node_target =
                    selected_is_3d_model ? nullptr : &this->selected_node_vertices;
                appendEntityHighlight(
                    this->selected_entity_type,
                    quint32(selected_iterator.value() & 0xffffffffULL),
                    QColor(0, 190, 255),
                    (selected_link_width - base_link_width) / 2.0f,
                    selected_has_error ? 5.0f : 2.0f,
                    &this->selected_link_vertices,
                    selected_node_target);
            }
        }
    }

    for (QHash<QUuid, InfrastructureEntity>::const_iterator error_iterator =
             this->simulation_error_entities.cbegin();
         error_iterator != this->simulation_error_entities.cend(); ++error_iterator)
    {
        const QHash<QUuid, quint64>::const_iterator entity_iterator =
            this->entity_keys_by_uuid.constFind(error_iterator.key());
        if (entity_iterator == this->entity_keys_by_uuid.cend())
            continue;

        const InfrastructureEntity entity_type = error_iterator.value();
        const quint32 render_id = quint32(entity_iterator.value() & 0xffffffffULL);
        if (entityRenderKey(entity_type, render_id) != entity_iterator.value())
            continue;

        const float base_link_width = float(this->symbology.link_thickness_px);
        const float diagnostic_link_width = qMax(3.0f, base_link_width + 2.0f);
        const QColor color = this->simulation_stale_entity_uuids.contains(error_iterator.key())
            ? QColor(128, 128, 128)
            : QColor(255, 0, 0);
        QVector<MapNetworkNodeVertex> *diagnostic_node_target =
            entity_type == InfrastructureEntity::Junction
            ? nullptr
            : &this->diagnostic_node_vertices;
        appendEntityHighlight(
            entity_type, render_id, color,
            (diagnostic_link_width - base_link_width) / 2.0f, 2.0f,
            &this->diagnostic_link_vertices,
            diagnostic_node_target);
    }
}

void MapGlobeNetworkScene::appendEntityHighlight(
    InfrastructureEntity entity_type, quint32 render_id, const QColor &color,
    float link_size_adjust_px, float node_size_adjust_px,
    QVector<MapNetworkLinkVertex> *link_target,
    QVector<MapNetworkNodeVertex> *node_target) const
{
    const quint64 key = entityRenderKey(entity_type, render_id);
    if (link_target != nullptr)
    {
        const QVector<int> link_indices = this->link_vertex_indices_by_entity.value(key);
        link_target->reserve(link_target->size() + link_indices.size());
        for (int vertex_index : link_indices)
        {
            if (vertex_index < 0 || vertex_index >= this->link_vertices.size())
                continue;

            MapNetworkLinkVertex vertex = this->link_vertices.at(vertex_index);
            vertex.red = color.redF();
            vertex.green = color.greenF();
            vertex.blue = color.blueF();
            vertex.alpha = color.alphaF();
            vertex.size_adjust_px = link_size_adjust_px;
            link_target->append(vertex);
        }
    }

    if (node_target != nullptr
        && (entity_type != InfrastructureEntity::Junction || this->symbology.show_junctions))
    {
        const QVector<int> node_indices = this->node_vertex_indices_by_entity.value(key);
        node_target->reserve(node_target->size() + node_indices.size());
        for (int vertex_index : node_indices)
        {
            if (vertex_index < 0 || vertex_index >= this->node_vertices.size())
                continue;

            MapNetworkNodeVertex vertex = this->node_vertices.at(vertex_index);
            vertex.red = color.redF();
            vertex.green = color.greenF();
            vertex.blue = color.blueF();
            vertex.alpha = color.alphaF();
            vertex.size_adjust_px = node_size_adjust_px;
            node_target->append(vertex);
        }
    }
}
