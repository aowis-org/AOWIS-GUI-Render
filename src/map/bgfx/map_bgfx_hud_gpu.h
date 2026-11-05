#ifndef MAP_BGFX_HUD_GPU_H
#define MAP_BGFX_HUD_GPU_H

#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"

#include <QPointF>
#include <QSizeF>

#include <bgfx/bgfx.h>

// bgfx side of the native HUD: one program, the HUD atlas texture (updated
// only where MapBgfxHudTextCache changed) and one transient indexed draw per
// frame, submitted last in the map view (Sequential), so the HUD is on top.
// The editor overlay (Phase 2) uses a second instance with its own atlas,
// drawn between the network and the HUD.
class MapBgfxHudGpu
{
public:
    bool initialize();
    void release();
    bool ready() const;

    // Uploads the atlas' dirty region (or creates the texture).
    void uploadAtlas(MapBgfxHudTextCache *cache);
    // offset: logical px added to every vertex while it is copied into the
    // transient buffer (a draw list reused across a pan, map_bgfx_pan_reuse.h).
    void draw(bgfx::ViewId view, const MapBgfxHudCanvas &canvas, const QSizeF &logical_viewport,
              const QPointF &offset = QPointF());

private:
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewport_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlas_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle atlas_texture = BGFX_INVALID_HANDLE;
    int atlas_width = 0;
    int atlas_height = 0;
    quint64 atlas_generation = 0;
    bool budget_warning_shown = false;
};

#endif // MAP_BGFX_HUD_GPU_H
