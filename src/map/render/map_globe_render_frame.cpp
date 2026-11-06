#include "map/render/map_globe_render_frame.h"

#include "map/render/map_globe_network_scene.h"
#include "map/render/map_network_render_symbology.h"

#include <QtGlobal>

bool MapGlobeRenderResources::isValid() const
{
    return this->link_vertices != nullptr
        && this->node_vertices != nullptr
        && this->selected_link_vertices != nullptr
        && this->selected_node_vertices != nullptr
        && this->diagnostic_link_vertices != nullptr
        && this->diagnostic_node_vertices != nullptr
        && this->flow_direction_vertices != nullptr
        && this->icon_vertices != nullptr
        && this->underground_link_vertices != nullptr
        && this->underground_junction_instances != nullptr
        && this->junction_instances != nullptr
        && this->tank_instances != nullptr
        && this->reservoir_instances != nullptr;
}

bool MapGlobeRenderFrame::isValid() const
{
    return this->logical_viewport_size.isValid()
        && this->output_size.isValid()
        && this->orbit_distance_m > 0.0
        && this->resources.isValid();
}

MapGlobeRenderFrame mapGlobeBuildRenderFrame(
    const MapGlobeCamera &camera,
    const MapGlobeNetworkScene &scene,
    const MapNetworkRenderSymbology &symbology,
    const QSize &logical_viewport_size,
    const QSize &output_size)
{
    MapGlobeRenderFrame frame;
    frame.logical_viewport_size = logical_viewport_size;
    frame.output_size = output_size;
    frame.render_origin_ecef = scene.renderOriginEcef();
    frame.canonical_view_projection = camera.viewProjectionMatrixForOrigin(
        frame.render_origin_ecef, &frame.camera_basis);
    frame.orbit_distance_m = camera.orbitDistanceM();
    frame.output_pixels_per_logical_pixel_x = qMax(
        1.0f,
        float(output_size.width())
            / float(qMax(1, logical_viewport_size.width())));
    frame.output_pixels_per_logical_pixel_y = qMax(
        1.0f,
        float(output_size.height())
            / float(qMax(1, logical_viewport_size.height())));

    frame.resources.link_vertices = &scene.linkVertices();
    frame.resources.node_vertices = &scene.nodeVertices();
    frame.resources.selected_link_vertices = &scene.selectedLinkVertices();
    frame.resources.selected_node_vertices = &scene.selectedNodeVertices();
    frame.resources.diagnostic_link_vertices = &scene.diagnosticLinkVertices();
    frame.resources.diagnostic_node_vertices = &scene.diagnosticNodeVertices();
    frame.resources.flow_direction_vertices = &scene.flowDirectionVertices();
    frame.resources.icon_vertices = &scene.iconVertices();
    frame.resources.underground_link_vertices = &scene.undergroundLinkVertices();
    frame.resources.underground_junction_instances =
        &scene.undergroundJunctionInstances();
    frame.resources.junction_instances = &scene.junctionInstances();
    frame.resources.tank_instances = &scene.tankInstances();
    frame.resources.reservoir_instances = &scene.reservoirInstances();

    frame.resources.node_size_unit = scene.nodeSizeUnit();
    frame.resources.node_size_px = scene.nodeSizePx();
    frame.resources.node_size_m = scene.nodeSizeM();
    frame.resources.icon_size_unit = scene.iconSizeUnit();
    frame.resources.icon_size_px = scene.iconSizePx();
    frame.resources.icon_size_m = scene.iconSizeM();
    frame.resources.link_thickness_unit = scene.linkThicknessUnit();
    frame.resources.link_thickness_px = scene.linkThicknessPx();
    frame.resources.link_thickness_m = scene.linkThicknessM();
    frame.resources.show_junctions = symbology.show_junctions;
    frame.resources.geometry_revision = scene.geometryRevision();
    return frame;
}
