#ifndef NETWORK_SYMBOLOGY_H
#define NETWORK_SYMBOLOGY_H

#include <QtGlobal>

#include "map/core/map_network_types.h"

enum class HeatmapRadiusUnit
{
    Meters,
    Pixels
};

enum class NetworkSymbologySizeUnit
{
    Meters,
    Pixels
};

enum class NetworkSymbologyPalette
{
    Viridis,
    Plasma,
    Inferno,
    Turbo,
    CoolWarm,
    Cividis,
    Magma,
    Batlow,
    RedBlue
};

constexpr NetworkSymbologyPalette NetworkSymbologyDefaultNodePalette =
    NetworkSymbologyPalette::CoolWarm;
constexpr NetworkSymbologyPalette NetworkSymbologyDefaultLinkPalette =
    NetworkSymbologyPalette::Batlow;
constexpr NetworkSymbologyPalette NetworkSymbologyDefaultHeatmapPalette =
    NetworkSymbologyPalette::Viridis;

constexpr int NetworkSymbologyDefaultNodeSizePx = 12;
constexpr double NetworkSymbologyMinimumNodeSizeM = 0.1;
constexpr double NetworkSymbologyMaximumNodeSizeM = 100.0;
constexpr double NetworkSymbologyDefaultNodeSizeM = 25.0;
constexpr int NetworkSymbologyMinimumIconSizePx = 5;
constexpr int NetworkSymbologyMaximumIconSizePx = 90;
constexpr int NetworkSymbologyDefaultIconSizePx = 20;
constexpr double NetworkSymbologyMinimumIconSizeM = 25.0;
constexpr double NetworkSymbologyMaximumIconSizeM = 450.0;
constexpr double NetworkSymbologyDefaultIconSizeM = 150.0;
constexpr int NetworkSymbologyDefaultLinkThicknessPx = 3;
constexpr double NetworkSymbologyMinimumLinkThicknessM = 0.01;
constexpr double NetworkSymbologyMaximumLinkThicknessM = 50.0;
constexpr double NetworkSymbologyDefaultLinkThicknessM = 12.5;

struct NetworkSymbologySettings
{
    VisualNode visual_node = VisualNode::None;
    NetworkSymbologyPalette node_palette = NetworkSymbologyDefaultNodePalette;
    bool node_palette_flipped = false;
    NetworkSymbologySizeUnit node_size_unit = NetworkSymbologySizeUnit::Pixels;
    bool show_junctions = true;
    int node_size_px = NetworkSymbologyDefaultNodeSizePx;
    double node_size_m = NetworkSymbologyDefaultNodeSizeM;
    NetworkSymbologySizeUnit icon_size_unit = NetworkSymbologySizeUnit::Pixels;
    int icon_size_px = NetworkSymbologyDefaultIconSizePx;
    double icon_size_m = NetworkSymbologyDefaultIconSizeM;
    VisualLink visual_link = VisualLink::None;
    NetworkSymbologyPalette link_palette = NetworkSymbologyDefaultLinkPalette;
    bool link_palette_flipped = false;
    NetworkSymbologySizeUnit link_thickness_unit = NetworkSymbologySizeUnit::Pixels;
    int link_thickness_px = NetworkSymbologyDefaultLinkThicknessPx;
    double link_thickness_m = NetworkSymbologyDefaultLinkThicknessM;
    bool show_flow_direction = true;
    int flow_direction_size_px = 10;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    NetworkSymbologyPalette heatmap_palette = NetworkSymbologyDefaultHeatmapPalette;
    bool heatmap_palette_flipped = false;
    int heatmap_opacity = 75;
    HeatmapRadiusUnit heatmap_radius_unit = HeatmapRadiusUnit::Meters;
    int heatmap_radius_m = 400;
    int heatmap_radius_px = 50;
    int heatmap_solid_center_percent = 70;

    NetworkSymbologySettings bounded() const
    {
        NetworkSymbologySettings result = *this;
        result.node_size_px = qBound(4, this->node_size_px, 64);
        result.node_size_m = qBound(NetworkSymbologyMinimumNodeSizeM, this->node_size_m,
                                    NetworkSymbologyMaximumNodeSizeM);
        result.icon_size_px = qBound(NetworkSymbologyMinimumIconSizePx, this->icon_size_px,
                                     NetworkSymbologyMaximumIconSizePx);
        result.icon_size_m = qBound(NetworkSymbologyMinimumIconSizeM, this->icon_size_m,
                                    NetworkSymbologyMaximumIconSizeM);
        result.link_thickness_px = qBound(1, this->link_thickness_px, 24);
        result.link_thickness_m = qBound(NetworkSymbologyMinimumLinkThicknessM,
                                         this->link_thickness_m,
                                         NetworkSymbologyMaximumLinkThicknessM);
        if (this->flow_direction_size_px <= 0)
        {
            result.flow_direction_size_px = 0;
            result.show_flow_direction = false;
        }
        else
        {
            result.flow_direction_size_px = qBound(6, this->flow_direction_size_px, 24);
        }
        result.heatmap_opacity = qBound(0, this->heatmap_opacity, 100);
        result.heatmap_radius_m = qBound(10, this->heatmap_radius_m, 1000);
        result.heatmap_radius_px = qBound(5, this->heatmap_radius_px, 250);
        result.heatmap_solid_center_percent =
            qBound(0, this->heatmap_solid_center_percent, 100);
        return result;
    }
};

struct NetworkSymbologyRanges
{
    double node_minimum = 0.0;
    double node_maximum = 0.0;
    double link_minimum = 0.0;
    double link_maximum = 0.0;
    double heatmap_minimum = 0.0;
    double heatmap_maximum = 0.0;
};

#endif // NETWORK_SYMBOLOGY_H
