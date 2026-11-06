#include "map/bgfx/map_bgfx_layer_visibility.h"

#include <cstdio>

namespace
{
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}
}

int main()
{
    // Distribution Editor: network and both overlay builders may draw.
    const MapBgfxLayerVisibility editor = mapBgfxLayerVisibility(true, true, true);
    check(editor.distribution_network && editor.editor_overlay_2d && editor.editor_overlay_3d,
          "distribution editor keeps its network and overlays");

    // Map Management: hide distribution geometry, not its cache selection.
    const MapBgfxLayerVisibility management = mapBgfxLayerVisibility(false, true, true);
    check(!management.distribution_network && management.editor_overlay_2d
              && management.editor_overlay_3d,
          "map management keeps 2D and 3D cache overlays without the network");

    // Drainage placeholders: no distribution geometry and no editor overlay.
    const MapBgfxLayerVisibility drainage = mapBgfxLayerVisibility(false, false, false);
    check(!drainage.distribution_network && !drainage.editor_overlay_2d
              && !drainage.editor_overlay_3d,
          "drainage stays basemap-only");

    // Distribution Monitor: native network drawing, no editor overlay.
    const MapBgfxLayerVisibility monitor = mapBgfxLayerVisibility(true, false, false);
    check(monitor.distribution_network && !monitor.editor_overlay_2d
              && !monitor.editor_overlay_3d,
          "distribution monitor stays independent of editor overlays");

    return failures == 0 ? 0 : 1;
}
