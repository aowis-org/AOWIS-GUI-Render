#ifndef MAP_BGFX_2D_IMAGERY_GPU_H
#define MAP_BGFX_2D_IMAGERY_GPU_H

#include "map/bgfx/map_bgfx_2d_imagery_atlas.h"
#include "map/bgfx/map_bgfx_2d_imagery_layout.h"

#include <QColor>
#include <QImage>
#include <QVector>

#include <bgfx/bgfx.h>

// bgfx resources for 2D imagery: the tiles as cells of a few atlas textures
// (MapBgfx2dImageryAtlas: pages with edge gutters, least recently used tiles
// give up their cells) and one program. Every quad of a frame goes into one
// vertex buffer with atlas texture coordinates, and consecutive quads on the
// same page are one draw: normally one draw call for all imagery, where one
// texture per tile cost one draw (and its texture and uniform bindings, each
// a JavaScript round trip in WebGL) per tile. An upload is a texture update
// of a cell instead of a texture creation. Tile choice lives in
// MapBgfx2dImagery::buildLayout(); this class only uploads, draws and evicts.
class MapBgfx2dImageryGpu
{
public:
    // Tiles kept resident. ~256 KiB each for 256x256 RGBA8: a 4K viewport
    // needs ~190 target tiles plus fallbacks, so this keeps one extra level.
    static constexpr int TextureBudget = 384;
    // Kept beyond the tiles a frame draws when that exceeds the budget (the
    // globe at a tilted, wide view draws 400+): the coarser tiles that new
    // tiles show until their own image is uploaded. Trimming down to the
    // budget there evicted exactly those, and new tiles were drawn blank.
    static constexpr int FallbackReserve = 128;

    bool initialize();
    void release();
    // Provider switch / deleted tiles: the same tile id now means other pixels.
    void clearTextures();

    bool hasTexture(const MapBgfx2dImagery::TileId &tile) const;
    // The layout's tiles count as used in this frame: uploads before the
    // draw never evict a tile it draws.
    void markUsed(const MapBgfx2dImagery::Layout &layout);
    bool uploadTexture(const MapBgfx2dImagery::TileId &tile, const QImage &image);
    // The globe surface (map_bgfx_globe_surface.h) samples the same atlas:
    // the same provider tiles, uploaded once for both views. Page texture and
    // texture rectangle (u0, v0, u1, v1: exactly the tile's texels, inside
    // its gutter) of a resident tile, marked as used in this frame (uploads
    // never take its cell). false: not resident.
    bool atlasRect(const MapBgfx2dImagery::TileId &tile, bgfx::TextureHandle *page_texture,
                   float *rect);

    // Draws the layout's quads in order. viewport is in logical pixels, the
    // same units as the quads. background_opacity is 0..1.
    void draw(bgfx::ViewId view, const MapBgfx2dImagery::Layout &layout,
              double viewport_width, double viewport_height,
              const QColor &background, double background_opacity);

    // Evicts least recently drawn tiles above
    // max(TextureBudget, tiles drawn in this frame + FallbackReserve).
    void endFrame();

    int textureCount() const { return this->atlas.tileCount(); }
    quint64 uploadCount() const { return this->upload_count; }
    // Draw calls of the last draw() (one per page run).
    int drawCallCount() const { return this->draw_calls; }

private:
    static quint64 keyFor(const MapBgfx2dImagery::TileId &tile);
    void destroyPage(int page);

    MapBgfx2dImageryAtlas atlas;
    // Per atlas page index.
    QVector<bgfx::TextureHandle> page_textures;
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tile_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle background_uniform = BGFX_INVALID_HANDLE;
    quint64 frame = 1;
    quint64 upload_count = 0;
    int draw_calls = 0;
};

#endif // MAP_BGFX_2D_IMAGERY_GPU_H
