#ifndef MAP_GLOBE_RENDER_FRAME_H
#define MAP_GLOBE_RENDER_FRAME_H

#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_camera.h"
#include "map/render/map_globe_render_instances.h"
#include "map/render/map_network_render_data.h"
#include "network/network_symbology.h"

#include <QMatrix4x4>
#include <QSize>
#include <QVector>
#include <QtGlobal>

// Stable backend-neutral view of all retained Globe network geometry needed
// by a renderer for one frame. The pointers intentionally reference the
// owning MapGlobeNetworkScene instead of copying potentially large vectors;
// the scene must outlive the frame packet.
struct MapGlobeRenderResources
{
    const QVector<MapNetworkLinkVertex> *link_vertices = nullptr;
    const QVector<MapNetworkNodeVertex> *node_vertices = nullptr;
    const QVector<MapNetworkLinkVertex> *selected_link_vertices = nullptr;
    const QVector<MapNetworkNodeVertex> *selected_node_vertices = nullptr;
    const QVector<MapNetworkLinkVertex> *diagnostic_link_vertices = nullptr;
    const QVector<MapNetworkNodeVertex> *diagnostic_node_vertices = nullptr;
    const QVector<MapNetworkLinkVertex> *flow_direction_vertices = nullptr;
    const QVector<MapNetworkIconVertex> *icon_vertices = nullptr;
    const QVector<MapNetworkLinkVertex> *underground_link_vertices = nullptr;
    const QVector<MapGlobeJunctionInstance> *underground_junction_instances = nullptr;
    const QVector<MapGlobeJunctionInstance> *junction_instances = nullptr;
    const QVector<MapGlobeTankInstance> *tank_instances = nullptr;
    const QVector<MapGlobeReservoirInstance> *reservoir_instances = nullptr;

    NetworkSymbologySizeUnit node_size_unit = NetworkSymbologySizeUnit::Pixels;
    int node_size_px = 0;
    double node_size_m = 0.0;
    NetworkSymbologySizeUnit icon_size_unit = NetworkSymbologySizeUnit::Pixels;
    int icon_size_px = 0;
    double icon_size_m = 0.0;
    NetworkSymbologySizeUnit link_thickness_unit = NetworkSymbologySizeUnit::Pixels;
    int link_thickness_px = 0;
    double link_thickness_m = 0.0;
    bool show_junctions = true;
    quint64 geometry_revision = 0;

    bool isValid() const;
};

// Per-frame backend handoff. The matrix is deliberately canonical OpenGL-
// style clip space as produced by MapGlobeCamera; a concrete renderer applies
// its own API clip-space correction after receiving this packet.
struct MapGlobeRenderFrame
{
    QSize logical_viewport_size;
    QSize output_size;
    QMatrix4x4 canonical_view_projection;
    MapGlobeCameraBasis camera_basis;
    GeoWgs84Ellipsoid::EcefPositionD render_origin_ecef;
    double orbit_distance_m = 0.0;
    float output_pixels_per_logical_pixel_x = 1.0f;
    float output_pixels_per_logical_pixel_y = 1.0f;
    MapGlobeRenderResources resources;

    bool isValid() const;
};

class MapGlobeCamera;
class MapGlobeNetworkScene;
struct MapNetworkRenderSymbology;

MapGlobeRenderFrame mapGlobeBuildRenderFrame(
    const MapGlobeCamera &camera,
    const MapGlobeNetworkScene &scene,
    const MapNetworkRenderSymbology &symbology,
    const QSize &logical_viewport_size,
    const QSize &output_size);

#endif // MAP_GLOBE_RENDER_FRAME_H
