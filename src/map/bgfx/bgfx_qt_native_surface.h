#ifndef AOWIS_BGFX_QT_NATIVE_SURFACE_H
#define AOWIS_BGFX_QT_NATIVE_SURFACE_H

#include "map/bgfx/bgfx_native_surface.h"

class QWindow;

// Qt hosting adapter (AOWIS::RenderQtSurface). The sole native handle
// discovery path for every Qt host's bgfx windows: MapBgfxMapView's map and
// probe windows, and the hosts' own bgfx windows (the GUI's Projects graph).
// Kept out of the Qt-free device/target classes (AOWIS::RenderDevice).
class BgfxQtNativeSurface final
{
public:
    // Capture only while Qt owns a live native window. On Wayland, callers
    // should retry after Expose if the wl_surface is not available yet.
    static BgfxNativeSurface capture(QWindow *window);
};

#endif // AOWIS_BGFX_QT_NATIVE_SURFACE_H
