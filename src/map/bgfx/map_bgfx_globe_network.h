#ifndef MAP_BGFX_GLOBE_NETWORK_H
#define MAP_BGFX_GLOBE_NETWORK_H

#include "map/bgfx/map_bgfx_entity_style.h"
#include "map/bgfx/map_bgfx_globe_gpu.h"
#include "map/bgfx/map_bgfx_globe_buffer_pool.h"
#include "map/bgfx/map_bgfx_globe_resource_pool.h"
#include "map/bgfx/map_bgfx_globe_vertex_layouts.h"
#include "map/render/map_globe_network_scene.h"
#include "map/render/map_globe_reservoir_model.h"
#include "map/render/map_globe_tank_model.h"
#include "map/render/map_network_render_symbology.h"
#include "map/render/map_render_types.h"
#include "network/network_render_snapshot.h"

#include <QImage>
#include <QHash>
#include <QSet>
#include <QSize>
#include <QUuid>
#include <QVector>
#include <QVector3D>

#include <bgfx/bgfx.h>

class MapGlobeCamera;

// The EPANET network on the bgfx globe (roadmap Phase 3, slice 3): pipes,
// generic node markers, junction sphere impostors, tank and reservoir 3D
// models, pump/valve/tank/reservoir icons, flow-direction chevrons,
// selection and simulation-diagnostic highlights, and the underground modes
// (Solid: the whole network through the terrain; X-Ray: buried runs dashed
// and dithered through the terrain; Hide: the terrain occludes them).
//
// All geometry is the backend-neutral MapGlobeNetworkScene the QRhi globe
// uses (WGS84 placement relative to a retained network origin, metre and
// pixel sizing, adaptive subdivision, declutter, X-Ray classification); the
// shaders are the QRhi globe shaders in bgfx form (vs/fs_globe_link, ...).
// Differences:
//
// - inputs are applied to the scene lazily, on the next globe frame
//   (synchronize()): the 2D map never pays for globe geometry;
// - no colours in the vertex streams: links, nodes, junction impostors,
//   icons, chevrons and the X-Ray runs reference their entity
//   (mapNetworkSetEntityReference()) and the vertex shaders colour them from
//   MapBgfxEntityStyle's data textures (symbology, selection, diagnostics,
//   visibility, flow direction). Time steps, metrics, palettes and ranges
//   never rebuild or re-upload a stream; only the outlines of the selection
//   and diagnostics keep vertex colours;
// - junction impostors are expanded to six vertices each instead of
//   instancing (WebGL-friendly);
// - flow chevrons exist in both orientations per link; the link's current
//   direction shows one set (no rebuild per time step);
// - flow chevrons keep their 8 px pull towards the eye but have no depth
//   bias (bgfx has no depth bias state).
//
// Not yet: the globe heatmap (per-tile bake into the terrain imagery).
class MapBgfxGlobeNetwork
{
public:
    MapBgfxGlobeNetwork();
    ~MapBgfxGlobeNetwork();

    // Programs, uniforms, the tank/reservoir textures. false: no network on
    // the globe on this device.
    // Albedo textures of the tank and reservoir 3D models; host assets (the
    // GUI's Qt resources). Set before initialize(); without them the models
    // are drawn untextured (1 x 1 white).
    void setModelImages(const QImage &tank_albedo, const QImage &reservoir_albedo);
    bool initialize();
    void release();
    bool ready() const;

    // --- Inputs (stored; applied by the next globe frame) -----------------
    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    void setHiddenEntityUuids(const QSet<QUuid> &uuids);
    // Already themed by the caller (icon fill colour).
    void setSymbology(const MapNetworkRenderSymbology &symbology);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(const QHash<QUuid, InfrastructureEntity> &error_entities,
                                    const QSet<QUuid> &stale_entity_uuids);
    void setUse3dIconModels(bool enabled);
    void setNodeDeclutteringEnabled(bool enabled);
    void setGroundOffsetM(double offset_m);
    void setVerticalExaggeration(double exaggeration);
    // DEM lookup for the X-Ray classification (MapGlobeTerrainNavigation).
    void setTerrainElevationResolver(MapGlobeNetworkScene::TerrainElevationResolver resolver);
    // Height of the drawn terrain surface (MapGlobeTerrainNavigation): the
    // demand points, which have no elevation, are draped on it.
    void setSurfaceHeightResolver(MapGlobeNetworkScene::SurfaceHeightResolver resolver);
    // Revision of the drawn terrain (MapBgfxGlobeSurface::surfaceRevision()),
    // set before render(): when it changed, render() moves the demand points
    // onto the terrain as it is drawn in that same frame.
    void setTerrainSurfaceRevision(quint64 revision) { this->terrain_surface_revision = revision; }
    void setUndergroundMode(MapUndergroundMode mode);
    MapUndergroundMode undergroundMode() const { return this->underground_mode; }
    // X-Ray: classifies the network against the DEM cached now (after
    // terrain arrived or the camera settled). true when it ran.
    bool refreshUndergroundXRay();

    // Applies stored inputs to the scene (geometry rebuilt as needed).
    void synchronize();
    const MapGlobeNetworkScene &scene() const { return this->network_scene; }

    struct FrameResult
    {
        int draw_calls = 0;
        quint64 upload_bytes = 0;
    };
    // Draws the network into view after the terrain surface. camera: the
    // frame's camera (viewport and origin already synchronized);
    // icon_atlas: the shared network icon atlas (MapBgfx2dGpu);
    // entity_style: the network's colours, synchronized for this frame.
    FrameResult render(bgfx::ViewId view, const MapGlobeCamera &camera, const QSize &logical_viewport,
                       bgfx::TextureHandle icon_atlas, const MapBgfxEntityStyle &entity_style);

    // Globe picking (slice 4), the QRhi globe's rules: the screen position
    // (logical px) against the network as the camera shows it now - tank /
    // reservoir models by ray-triangle test, then junction spheres, icons,
    // generic nodes and pipes by projected distance with the globe's larger
    // hit radii. camera: viewport set and synced with the model. Projects
    // with the camera's matrix for the network origin, i.e. what is drawn.
    // Deliberately no occlusion test against the terrain (as the QRhi
    // globe): entities behind or under it stay selectable.
    MapRenderHit hitTest(const QPointF &screen_position, const MapGlobeCamera &camera,
                         const QSize &logical_viewport);

private:
    // GPU streams that are out of date, as the QRhi backend's invalidations.
    enum Stream : quint32
    {
        GeometryStream = 1u << 0,     // links, nodes
        HighlightStream = 1u << 1,    // selected / diagnostic links and nodes
        FlowStream = 1u << 2,         // chevrons
        IconStream = 1u << 3,         // icons, tank and reservoir models
        UndergroundStream = 1u << 4,  // X-Ray links and junctions
        JunctionStream = 1u << 5,     // junction impostors (instances, colours)
        AllStreams = 0x3fu
    };
    // A junction impostor corner: centre, corner, entity reference
    // (mapNetworkSetEntityReference()), radius.
    struct JunctionVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float corner_x = 0.0f;
        float corner_y = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 0.0f;
        float radius = 0.0f;
    };

    static quint64 entityKey(InfrastructureEntity entity_type, quint32 render_id);
    // Marks GPU streams out of date (the icon stream also the CPU models).
    void markDirty(quint32 streams);
    void ensureModelVertices();
    // The hit for an entity, unless it is unknown or hidden.
    MapRenderHit entityHit(InfrastructureEntity entity_type, quint32 render_id) const;
    void uploadStreams(FrameResult *result);
    void updateIconDrawOrder(const QVector3D &eye, FrameResult *result);
    QVector<JunctionVertex> junctionVertices(const QVector<MapGlobeJunctionInstance> &instances) const;
    // Whether a junction is drawn (junctions shown, not hidden): picking.
    bool junctionDrawn(quint32 render_id) const;

    MapGlobeNetworkScene network_scene;

    // Stored inputs and what still has to reach the scene.
    NetworkRenderSnapshot snapshot;
    bool snapshot_pending = false;
    QSet<QUuid> hidden_entity_uuids;
    bool hidden_pending = false;
    MapNetworkRenderSymbology symbology;
    bool symbology_pending = false;
    // What the scene has: the change detection of the next symbology.
    MapNetworkRenderSymbology applied_symbology;
    bool symbology_applied = false;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    bool selection_pending = false;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    bool errors_pending = false;
    bool use_3d_icon_models = false;
    bool icon_models_pending = false;
    bool node_decluttering_enabled = true;
    bool declutter_pending = false;
    double ground_offset_m = 0.0;
    bool ground_offset_pending = false;
    double vertical_exaggeration = 1.0;
    bool exaggeration_pending = false;
    MapUndergroundMode underground_mode = MapUndergroundMode::XRay;
    bool underground_pending = true;
    // Entity (type, render id) -> uuid: junction colours (selection,
    // diagnostics, hidden entities) and picking.
    QHash<quint64, QUuid> entity_uuids;
    // Tank/reservoir model triangles as uploaded (also picked by ray).
    QVector<MapGlobeTankModelVertex> tank_model_vertices;
    QVector<MapGlobeReservoirModelVertex> reservoir_model_vertices;
    bool model_vertices_stale = true;
    // A link thickness change rebuilds the scene's geometry when the network
    // has demand point attachments.
    bool has_attachments = false;
    // The drawn terrain the demand points were last draped on.
    quint64 terrain_surface_revision = 0;
    quint64 draped_surface_revision = 0;
    quint32 dirty_streams = AllStreams;
    // Camera-dependent icon index order is cached by the GUI scene adapter.
    // Reset when the icon vertex stream changes or the GPU device is released.
    bool icon_order_dirty = true;
    QVector3D icon_order_eye;

    MapBgfxGlobeVertexLayouts gpu_layouts;
    MapBgfxGlobeResourcePool gpu_resources;
    QImage tank_albedo_image;
    QImage reservoir_albedo_image;
    MapBgfxGlobeBufferPool gpu_buffers;
};

#endif // MAP_BGFX_GLOBE_NETWORK_H
