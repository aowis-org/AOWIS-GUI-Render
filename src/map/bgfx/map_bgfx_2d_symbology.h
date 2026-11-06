#ifndef MAP_BGFX_2D_SYMBOLOGY_H
#define MAP_BGFX_2D_SYMBOLOGY_H

#include "map/bgfx/map_bgfx_2d_mesh.h"
#include "map/render/map_network_render_symbology.h"

#include <algorithm>
#include <cmath>

// Symbol sizes of the backend-neutral MapNetworkRenderSymbology as bgfx 2D
// uniforms. Colours are not resolved here: the shaders colour every entity
// from its data (MapBgfxEntityStyle, shaders/entity_style.sh).
namespace MapBgfx2dSymbology
{
// Demand-point attachment lines: fixed hairline width, like QRhi (0.75 px
// half width).
constexpr double AttachmentWidthPixels = 1.5;
// Flow chevron stroke width relative to its length (QRhi ratio).
constexpr double FlowStrokeWidthRatio = 0.2;

struct Sizes
{
    double link_width_px = 3.0;
    double attachment_width_px = AttachmentWidthPixels;
    double node_half_px = 6.0;
    double icon_size_px = 24.0;
    double flow_length_px = 10.0;
    double flow_half_stroke_px = 1.0;
    // 0 disables the heatmap pass (no heatmap selected).
    double heatmap_radius_px = 0.0;
    double heatmap_opacity = 0.75;
    // Radius fraction drawn at full strength (QRhi clamps it to 0.9).
    double heatmap_solid_fraction = 0.7;
};

// Metre-based sizes are converted with the continuous zoom, so they scale
// smoothly, never per imagery level.
inline Sizes sizes(const MapNetworkRenderSymbology &symbology,
                   double latitude_deg, double continuous_zoom)
{
    const std::function<double(NetworkSymbologySizeUnit, double, double)> size_px =
        [latitude_deg, continuous_zoom](NetworkSymbologySizeUnit unit, double pixels, double meters)
    {
        return unit == NetworkSymbologySizeUnit::Meters
            ? MapBgfx2dChunking::pixelsForMeters(meters, latitude_deg, continuous_zoom)
            : pixels;
    };
    Sizes result;
    // Keep sub-pixel symbols visible as a hairline / dot when zoomed out.
    result.link_width_px = std::max(1.0, size_px(symbology.link_thickness_unit,
        double(symbology.link_thickness_px), symbology.link_thickness_m));
    result.node_half_px = std::max(1.0, 0.5 * size_px(symbology.node_size_unit,
        double(symbology.node_size_px), symbology.node_size_m));
    result.icon_size_px = std::max(1.0, size_px(symbology.icon_size_unit,
        double(symbology.icon_size_px), symbology.icon_size_m));
    result.flow_length_px = std::max(0.0, double(symbology.flow_direction_size_px));
    result.flow_half_stroke_px =
        0.5 * std::max(1.0, result.flow_length_px * FlowStrokeWidthRatio);
    // Heatmap radius (QRhi heatmapRadiusPixels(), but from the continuous
    // zoom, so a metre radius grows smoothly instead of per imagery level).
    if (symbology.heatmapActive())
    {
        result.heatmap_radius_px = symbology.heatmap_radius_unit == HeatmapRadiusUnit::Pixels
            ? std::max(1.0, double(symbology.heatmap_radius_px))
            : std::max(1.0, MapBgfx2dChunking::pixelsForMeters(
                  double(symbology.heatmap_radius_m), latitude_deg, continuous_zoom));
    }
    result.heatmap_opacity = std::clamp(double(symbology.heatmap_opacity) / 100.0, 0.0, 1.0);
    result.heatmap_solid_fraction =
        std::clamp(double(symbology.heatmap_solid_center_percent) / 100.0, 0.0, 0.9);
    return result;
}
}

#endif // MAP_BGFX_2D_SYMBOLOGY_H
