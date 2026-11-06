#ifndef MAP_GLOBE_NETWORK_SCENE_H
#define MAP_GLOBE_NETWORK_SCENE_H

#include "map/render/map_retained_network_scene.h"
#include "map/render/map_globe_retained_origin.h"
#include "map/render/map_network_render_symbology.h"
#include "map/render/map_globe_render_instances.h"
#include "map/render/map_globe_flow_geometry.h"
#include "map/render/map_globe_scene_sync.h"
#include "map/render/map_globe_vertical_transform.h"
#include "map/render/map_network_render_data.h"
#include "geo/geo_wgs84_ellipsoid.h"

#include <QColor>
#include <QHash>
#include <QPointF>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <QVector3D>

#include <functional>

// Builds EPANET network render geometry (pipes, junctions, tanks,
// reservoirs, pumps, valves) for the "Globe" map view mode.
//
// This mirrors the retained scene-building role used by the flat TwoD
// view, but every vertex is placed on the actual WGS84 ellipsoid in ECEF
// meters instead of local Web Mercator
// tangent-plane world units. There is no antimeridian seam to manage here --
// ECEF coordinates are globally unambiguous -- but there *is* still an
// origin, for a different reason than the flat TwoD scene: its local
// origin exists to give TwoD a manageable coordinate range in
// the first place (Web Mercator world units span the whole projected
// planet), whereas ECEF's origin (Earth's center) already gives every
// vertex a magnitude of ~6.378e6 m regardless of where on the planet it is
// -- far enough from zero that float32 (which every GPU vertex buffer here
// ultimately is) only has ~0.5-1m of resolution left once you're at that
// magnitude, before any camera-relative arithmetic is even done. See
// renderOriginEcef() and ecefPosition() for how this class avoids that:
// network vertices are placed relative to a geometry-owned ECEF origin
// (not the camera's periodically rebased origin). The network view matrix
// is independently evaluated in that retained coordinate frame, so the
// float32 narrowing happens only after the large-magnitude part of the
// position has already been subtracted off in double precision.
//
// Uses MapNetworkLinkVertex/MapNetworkNodeVertex/MapNetworkIconVertex
// directly, so renderer backends can consume the same prepared scene data
// without rebuilding geometry into backend-specific structures.
// The current renderer computes stroke thickness/node radius/icon size
// in screen-space pixels purely from the projected clip-space positions of
// the vertices they are given, so flat and Globe geometry can share them
// unchanged. The one path that needs Globe-specific geometric input is
// metre-sized link width.
// Globe link vertices provide render backends with a per-segment local tangent
// width direction derived from the WGS84 east/north/up frame, so a metre of
// pipe width means a metre sideways along the local Earth surface rather
// than along the raw global ECEF XY plane. Long digitized spans are also
// adaptively subdivided along the WGS84 geodesic before those vertices are
// built, preventing one long ECEF chord from cutting through the curved
// Earth. Flat TwoD vertices leave the optional tangent direction zero
// and retain their flat XY-plane behavior.
//
// Coincident-node decluttering uses the same one-metre policy as the flat
// retained scene, but clusters nodes in a local east/north tangent plane and
// applies the resulting metre offsets back on WGS84 before ECEF conversion.
// Junctions are analytic sphere impostors -- see junctionInstances() -- and
// tanks/reservoirs can be represented by their Globe-oriented 3D models.
// Underground X-Ray classifies both link segments and junctions against the
// visible terrain surface.
// Solid mode remains the mode for seeing the complete network through terrain.
class MapGlobeNetworkScene
{
public:
    // Read-only terrain lookup at an arbitrary coordinate, used solely to
    // classify link segments and junctions as underground for X-Ray mode. In addition to
    // elevation it reports the effective terrain cell size at that point so
    // classification density follows terrain LOD. Never influences where
    // any geometry is actually placed.
    using TerrainElevationResolver = std::function<bool(
        const CoordinateWGS84 &coordinate,
        double *elevation_m,
        double *cell_size_m)>;

    // The height above the WGS84 ellipsoid of the terrain surface exactly as
    // it is drawn (vertical exaggeration applied, at the LOD on screen);
    // false where no relief is drawn, i.e. the surface is the ellipsoid.
    using SurfaceHeightResolver = std::function<bool(
        const CoordinateWGS84 &coordinate,
        double *height_m)>;

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    bool setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids);
    void setSymbology(const MapNetworkRenderSymbology &symbology);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids);
    bool setUse3dIconModels(bool enabled);
    bool setNodeDeclutteringEnabled(bool enabled);
    // Height, in meters, added above each entity's own elevation before it is
    // placed on the ellipsoid -- the Globe counterpart of
    // the flat TwoD network ground offset, sharing the same
    // MapModel::view3dNetworkGroundOffsetM() control since both exist to
    // keep network geometry from z-fighting with the terrain mesh beneath
    // it. Returns true if the (bounded) value actually changed.
    bool setGroundOffsetM(double offset_m);
    // Updates the shared Globe vertical transform used by terrain and network
    // placement. Returns true if the (bounded) value actually changed.
    bool setVerticalExaggeration(double exaggeration);
    // Injects the read-only terrain-elevation lookup used by X-Ray
    // classification (see setUndergroundXRayEnabled()). Safe to leave unset
    // -- X-Ray will simply never find anything to highlight without it.
    // Set once by the renderer backend; the resolver closure itself
    // always queries live repository state, so there's no need to re-set it
    // as terrain tiles load in.
    void setTerrainElevationResolver(TerrainElevationResolver resolver);
    // Demand points and intermediate pipe bends have no independent
    // physical elevation in the model. With this resolver they follow the
    // drawn terrain plus network lift. Junction/pipe endpoint elevations
    // remain hydraulic-model values. Set once by the renderer backend;
    // terrain revisions refresh draped geometry as necessary.
    void setSurfaceHeightResolver(SurfaceHeightResolver resolver);
    // The drawn terrain changed (window rebuilt, terrain meshes applied):
    // samples the surface under every demand point again and moves the ones
    // whose height changed (markers, icons, attachment lines, highlights) in
    // place, without a network rebuild. True when any vertex moved.
    bool refreshDemandPointHeights();
    // Pipe bend coordinates are geographic only in the hydraulic model.
    // Follow the drawn surface when terrain tiles/LOD change, but rebuild
    // only if a sampled bend height actually changed (not every terrain tick).
    bool refreshDrapedPipeBendHeights();
    // While true, rebuildNetworkGeometry() also walks each link in short
    // subdivisions, compares each subdivision's own implied elevation
    // against the resolver's terrain sample at the same point, and
    // collects the contiguous "below terrain" runs into
    // undergroundLinkVertices()/undergroundJunctionInstances(), for the
    // caller to draw through terrain with its X-Ray rendering path.
    // A plain bool rather than
    // a backend-specific underground-mode enum, to avoid a
    // renderer<->scene dependency -- the caller maps XRay to true and
    // Hide/Solid to false ("Solid" needs no per-segment classification).
    // Returns true if changed.
    bool setUndergroundXRayEnabled(bool enabled);
    // Re-runs only the underground classification against the terrain
    // resolver's current cache. Normal Globe network geometry is left
    // untouched, so terrain streaming cannot force a full network rebuild.
    // Returns true when classification was performed.
    bool refreshUndergroundXRayGeometry();
    // Initializes the retained frame from the camera only when no network
    // geometry has supplied a stable ECEF anchor. Subsequent camera-origin
    // changes are deliberately ignored: network vertices/GPU buffers stay
    // in their retained frame. Returns true only on first initialization.
    bool setRenderOriginEcef(const GeoWgs84Ellipsoid::EcefPositionD &origin_ecef);
    GeoWgs84Ellipsoid::EcefPositionD renderOriginEcef() const;
    // Screen scale at the Globe orbit target, used to give flow-direction
    // chevrons the same pixel-based length, spacing, and terrain clearance
    // with the same scale semantics as the network models. Returns true when the scale changed and
    // flowDirectionVertices() was rebuilt.
    bool setFlowDirectionPixelsPerMeter(double pixels_per_meter);

    const QVector<MapNetworkLinkVertex> &linkVertices() const;
    const QVector<MapNetworkNodeVertex> &nodeVertices() const;
    const QVector<MapNetworkLinkVertex> &selectedLinkVertices() const;
    const QVector<MapNetworkNodeVertex> &selectedNodeVertices() const;
    const QVector<MapNetworkLinkVertex> &diagnosticLinkVertices() const;
    const QVector<MapNetworkNodeVertex> &diagnosticNodeVertices() const;
    const QVector<MapNetworkLinkVertex> &flowDirectionVertices() const;
    const QVector<MapNetworkIconVertex> &iconVertices() const;
    const QVector<MapGlobeTankInstance> &tankInstances() const;
    const QVector<MapGlobeReservoirInstance> &reservoirInstances() const;
    const QVector<MapNetworkLinkVertex> &undergroundLinkVertices() const;
    const QVector<MapGlobeJunctionInstance> &undergroundJunctionInstances() const;
    // Analytic sphere-impostor instances for junction entities -- see the class
    // comment above. Renderer backends consume the same compact instance
    // layout for analytic junction rendering. Junction entities are NOT also present in
    // nodeVertices() with a visible alpha -- see applyNodeColor() -- so
    // they render exactly once, as an impostor, never as a flat marker
    // underneath it.
    const QVector<MapGlobeJunctionInstance> &junctionInstances() const;
    quint64 geometryRevision() const;
    bool hasGeometry() const;

    NetworkSymbologySizeUnit nodeSizeUnit() const;
    int nodeSizePx() const;
    double nodeSizeM() const;
    NetworkSymbologySizeUnit iconSizeUnit() const;
    int iconSizePx() const;
    double iconSizeM() const;
    NetworkSymbologySizeUnit linkThicknessUnit() const;
    int linkThicknessPx() const;
    double linkThicknessM() const;

private:
    struct IconMarker
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
        quint32 render_id = 0;
        QVector3D center;
    };

    // Lightweight record of just
    // what rebuildJunctionInstances() needs (render_id and position),
    // kept separate from MapGlobeJunctionInstance, which additionally carries
    // the radius and stable GPU style-table index.
    struct JunctionMarker
    {
        quint32 render_id = 0;
        QVector3D center;
    };

    using SceneSegment = MapGlobeFlowSegment;

    // GUI retains the hydraulic entity type; Render owns the neutral path
    // geometry and consumes only the pipe/non-pipe placement distinction.
    struct LinkPath : MapGlobeFlowPath
    {
        InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    };

    void applySceneInvalidation(const MapGlobeSceneInvalidation &plan);
    void rebuildNetworkGeometry();
    void rebuildUndergroundXRayGeometry();
    QVector3D ecefPosition(const CoordinateWGS84 &coordinate, double elevation_m) const;
    // ecefPosition() for a height already above the ellipsoid (rendered
    // metres: exaggeration and lift applied by the caller).
    QVector3D ecefPositionAtHeight(const CoordinateWGS84 &coordinate, double height_m) const;
    // Rendered height of a draped demand point at its render coordinate:
    // the drawn surface (0 m where no relief is drawn) plus the network lift.
    double drapedHeightM(const CoordinateWGS84 &render_coordinate, double *surface_height_m) const;
    // Moves one draped demand point (node quad, attachment line start, icon
    // marker) to center.
    void moveDemandPoint(quint32 render_id, int icon_marker_index, const QVector3D &center);
    void appendLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                           const QVector3D &start, const QVector3D &end);
    QVector3D linkWidthDirection(
        const QVector3D &start, const QVector3D &end) const;
    void appendNode(InfrastructureEntity entity_type, quint32 render_id,
                    const QVector3D &center);
    // Writes the entity reference the GPU colours the vertex by
    // (mapNetworkSetEntityReference()).
    void applyLinkColor(MapNetworkLinkVertex *vertex) const;
    void applyNodeColor(MapNetworkNodeVertex *vertex) const;
    void rebuildFlowDirections();
    // One chevron stroke of a link in one orientation (entity reference).
    void appendFlowDirectionStroke(
        const QVector3D &start, const QVector3D &end,
        quint32 render_id, float orientation, float half_width_px);
    void rebuildIcons();
    void appendIcon(const IconMarker &marker);
    void rebuildTankInstances();
    void rebuildReservoirInstances();
    bool modelBasisAt(
        const QVector3D &center,
        QVector3D *basis_x,
        QVector3D *basis_y,
        QVector3D *basis_z) const;
    // Rebuilds compact placement/radius instances from the junction markers.
    // Colour, selection, diagnostics and visibility come from the per-entity
    // GPU data (MapBgfxEntityStyle), not from the instances.
    void rebuildJunctionInstances();
    void rebuildHighlights();
    void appendEntityHighlight(InfrastructureEntity entity_type, quint32 render_id,
                               const QColor &color, float link_size_adjust_px,
                               float node_size_adjust_px,
                               QVector<MapNetworkLinkVertex> *link_target,
                               QVector<MapNetworkNodeVertex> *node_target) const;
    void appendUndergroundLinkSegment(InfrastructureEntity entity_type, quint32 render_id,
                                      const QVector3D &start, const QVector3D &end);
    void appendUndergroundCurvedRun(
        InfrastructureEntity entity_type, quint32 render_id,
        const CoordinateWGS84 &start_coordinate, double start_elevation_m,
        const QVector3D &start_ecef,
        const CoordinateWGS84 &end_coordinate, double end_elevation_m,
        const QVector3D &end_ecef);
    // Subdivides one already-placed link segment (from consecutive digitized
    // vertices) into short spans and appends each contiguous "below terrain"
    // run to underground_link_vertices. The classification interval follows
    // the terrain cell size resolved for the currently rendered Globe LOD,
    // using the same terrain-LOD-aware adaptive policy as link subdivision.
    void appendUndergroundSubdivisions(
        InfrastructureEntity entity_type, quint32 render_id,
        const CoordinateWGS84 &start_coordinate, double start_elevation_m,
        const QVector3D &start_ecef,
        const CoordinateWGS84 &end_coordinate, double end_elevation_m,
        const QVector3D &end_ecef);

    MapRetainedNetworkScene retained_network;
    QSet<QUuid> hidden_entity_uuids;
    QVector<MapNetworkLinkVertex> link_vertices;
    QVector<MapNetworkNodeVertex> node_vertices;
    QVector<MapNetworkLinkVertex> selected_link_vertices;
    QVector<MapNetworkNodeVertex> selected_node_vertices;
    QVector<MapNetworkLinkVertex> diagnostic_link_vertices;
    QVector<MapNetworkNodeVertex> diagnostic_node_vertices;
    QVector<MapNetworkLinkVertex> flow_direction_vertices;
    QVector<MapNetworkIconVertex> icon_vertices;
    QVector<IconMarker> icon_markers;
    QVector<MapGlobeTankInstance> tank_instances;
    QVector<MapGlobeReservoirInstance> reservoir_instances;
    QVector<JunctionMarker> junction_markers;
    QVector<MapGlobeJunctionInstance> junction_instances;
    QVector<LinkPath> link_paths;
    MapNetworkRenderSymbology symbology;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    QHash<QUuid, quint64> entity_keys_by_uuid;
    QHash<quint64, QVector<int>> link_vertex_indices_by_entity;
    QHash<quint64, QVector<int>> node_vertex_indices_by_entity;
    quint64 geometry_revision = 0;
    MapGlobeRetainedOrigin retained_origin;
    MapGlobeVerticalTransform vertical_transform;
    double flow_direction_pixels_per_meter = 0.0;
    TerrainElevationResolver terrain_elevation_resolver;
    SurfaceHeightResolver surface_height_resolver;
    // Draped demand points of the current geometry (built with it): where
    // they are sampled, the surface height they were placed on, and their
    // icon marker (-1: none).
    struct DrapedDemandPoint
    {
        quint32 render_id = 0;
        CoordinateWGS84 render_coordinate;
        double surface_height_m = 0.0;
        int icon_marker_index = -1;
    };
    QVector<DrapedDemandPoint> draped_demand_points;
    struct DrapedPipeBend
    {
        CoordinateWGS84 coordinate;
        double surface_height_m = 0.0;
    };
    QVector<DrapedPipeBend> draped_pipe_bends;
    bool underground_xray_enabled = false;
    bool use_3d_icon_models = false;
    bool node_decluttering_enabled = true;
    QHash<quint32, QPointF> node_declutter_offsets_m;
    double fallback_elevation_m = 0.0;
    QVector<MapNetworkLinkVertex> underground_link_vertices;
    QVector<MapGlobeJunctionInstance> underground_junction_instances;
};

#endif // MAP_GLOBE_NETWORK_SCENE_H
