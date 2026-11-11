#ifndef MAP_BGFX_LABEL_GPU_H
#define MAP_BGFX_LABEL_GPU_H

#include "map/bgfx/map_bgfx_glyph_atlas.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"

#include <QPointF>
#include <QSizeF>

#include <bgfx/bgfx.h>

// bgfx side of the map labels (map_bgfx_labels.h): vs_hud + fs_label_sdf,
// the R8 distance atlas of MapBgfxGlyphAtlas (linear filtering, updated only
// where it changed) and one transient indexed draw of the label canvas per
// frame (one quad per glyph, the text colour and fade in the vertex tint).
// Fill and halo are thresholds of the same distance: the halo colour and
// width are per-draw uniforms.
class MapBgfxLabelGpu
{
public:
    bool initialize();
    void release();
    bool ready() const;

    // Uploads the atlas' dirty region (or creates the texture).
    void uploadAtlas(MapBgfxGlyphAtlas *atlas);
    // halo_px: logical px; device_pixel_ratio converts it to the physical
    // pixels the shader thresholds in. offset as MapBgfxHudGpu::draw().
    void draw(bgfx::ViewId view, const MapBgfxHudCanvas &canvas, const QSizeF &logical_viewport,
              quint32 halo_argb, double halo_px, double device_pixel_ratio,
              const QPointF &offset = QPointF());

    // Physical px the fill is grown by: small SDF text otherwise reads a
    // little lighter than hinted text of the same size.
    static constexpr double FillBiasPx = 0.12;

private:
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewport_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle halo_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlas_size_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlas_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle atlas_texture = BGFX_INVALID_HANDLE;
    int atlas_width = 0;
    int atlas_height = 0;
    bool budget_warning_shown = false;
};

#endif // MAP_BGFX_LABEL_GPU_H
