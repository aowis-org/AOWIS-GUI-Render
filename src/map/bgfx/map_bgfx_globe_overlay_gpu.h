#ifndef MAP_BGFX_GLOBE_OVERLAY_GPU_H
#define MAP_BGFX_GLOBE_OVERLAY_GPU_H

#include "map/bgfx/map_bgfx_globe_terrain_gpu.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

// Device-bound GPU state for the sky atmosphere. The host computes the
// camera/geodetic vectors; no application model, Qt, or terrain dependencies.
class MapBgfxGlobeSkyGpu final
{
public:
    using ProgramLoader = bgfx::ProgramHandle (*)(const char *, const char *);

    struct Frame
    {
        std::array<float, 4> forward = {};
        std::array<float, 4> right = {};
        std::array<float, 4> up = {};
        std::array<float, 4> vertical = {};
        std::array<float, 4> params = {};
    };

    MapBgfxGlobeSkyGpu() = default;
    MapBgfxGlobeSkyGpu(const MapBgfxGlobeSkyGpu &) = delete;
    MapBgfxGlobeSkyGpu &operator=(const MapBgfxGlobeSkyGpu &) = delete;

    bool initialize(ProgramLoader load_program);
    void release();
    bool ready() const;
    bool draw(bgfx::ViewId view, const Frame &frame) const;

private:
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle triangle = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle forward_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle right_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle up_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle vertical_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params_uniform = BGFX_INVALID_HANDLE;
};

// Owns the optional heatmap shader resources and four lazily allocated render
// targets. Tile assignment, stamp selection, LRU and per-frame budgets remain
// host policy. The host passes prepared stamp vertices, not scene/model types.
class MapBgfxGlobeHeatmapGpu final
{
public:
    using ProgramLoader = bgfx::ProgramHandle (*)(const char *, const char *);
    static constexpr int PageCount = 4;
    static constexpr int PageSize = 2048;
    static constexpr int CellSize = 256;

    struct BakeVertex
    {
        float corner_x = 0.0f;
        float corner_y = 0.0f;
        float center_x = 0.0f;
        float center_y = 0.0f;
        float radius = 0.0f;
        float solid_fraction = 0.0f;
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        float alpha = 1.0f;
        float cell_x = 0.0f;
        float cell_y = 0.0f;
    };

    MapBgfxGlobeHeatmapGpu() = default;
    MapBgfxGlobeHeatmapGpu(const MapBgfxGlobeHeatmapGpu &) = delete;
    MapBgfxGlobeHeatmapGpu &operator=(const MapBgfxGlobeHeatmapGpu &) = delete;

    bool initialize(ProgramLoader load_program);
    void release();
    bool ready() const;
    bool ensurePage(int page);
    bgfx::TextureHandle pageTexture(int page) const;
    MapBgfxGlobeTerrainGpu::Heatmap composite(int page, const float *tile_rect,
                                              const float *style) const;
    void orderViews(bgfx::ViewId map_view, bgfx::ViewId first_bake_view) const;
    bool bakePage(bgfx::ViewId bake_view, int page,
                  const BakeVertex *clears, std::uint32_t clear_count,
                  const BakeVertex *stamps, std::uint32_t stamp_count) const;

private:
    struct Page
    {
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle frame_buffer = BGFX_INVALID_HANDLE;
    };
    bgfx::ProgramHandle surface_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bake_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tile_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle style_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle bake_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout bake_layout;
    std::array<Page, PageCount> pages = {};
};

#endif // MAP_BGFX_GLOBE_OVERLAY_GPU_H
