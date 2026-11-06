#pragma once

// A map mode can hide distribution geometry while retaining independent
// interaction overlays. Map Management relies on that distinction to show
// map-tile and terrain-tile selection rectangles over an otherwise bare map.
struct MapBgfxLayerVisibility
{
    bool distribution_network = false;
    bool editor_overlay_2d = false;
    bool editor_overlay_3d = false;
};

constexpr MapBgfxLayerVisibility mapBgfxLayerVisibility(
    bool distribution_network_visible, bool has_editor_overlay_2d,
    bool has_editor_overlay_3d)
{
    return {distribution_network_visible, has_editor_overlay_2d, has_editor_overlay_3d};
}
