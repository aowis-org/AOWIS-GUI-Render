#ifndef MAP_BGFX_GLOBE_TERRAIN_GPU_H
#define MAP_BGFX_GLOBE_TERRAIN_GPU_H

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

// Production globe terrain GPU draw/resource owner. Only bgfx types cross the
// boundary: terrain selection, DEM, atlas lookup and heatmap baking stay in the
// host. All calls run on the bgfx submission thread, before shutdown.
class MapBgfxGlobeTerrainGpu final
{
public:
    using ProgramLoader = bgfx::ProgramHandle (*)(const char *, const char *);

    struct Frame
    {
        std::array<float, 16> view_projection = {};
        std::array<float, 4> background = {};
        std::array<float, 4> origin = {};
        std::array<float, 4> light = {};
        std::array<float, 4> shading = {};
    };

    // The heatmap atlas and shader remain owned by the host until the
    // heatmap-baking extraction. Render owns the actual composite submission.
    struct Heatmap
    {
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle tile_uniform = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle style_uniform = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        const float *tile_rect = nullptr; // vec4
        const float *style = nullptr;     // vec4
    };

    MapBgfxGlobeTerrainGpu() = default;
    MapBgfxGlobeTerrainGpu(const MapBgfxGlobeTerrainGpu &) = delete;
    MapBgfxGlobeTerrainGpu &operator=(const MapBgfxGlobeTerrainGpu &) = delete;

    bool initialize(ProgramLoader load_program,
                    const std::array<std::uint8_t, 4> &missing_rgba,
                    const std::array<std::uint8_t, 4> &cap_rgba);
    void release();
    bool ready() const;
    bgfx::TextureHandle missingTexture() const { return this->missing_texture; }

    bool drawCap(bgfx::ViewId view, bgfx::DynamicVertexBufferHandle vertices,
                 bgfx::DynamicIndexBufferHandle indices, std::uint32_t first_index,
                 std::uint32_t index_count, const Frame &frame) const;
    bool drawTile(bgfx::ViewId view, bgfx::DynamicVertexBufferHandle vertices,
                  std::uint32_t first_vertex, std::uint32_t vertex_count,
                  bgfx::IndexBufferHandle indices, bgfx::TextureHandle imagery,
                  const float *tile_rect, const Frame &frame,
                  const Heatmap *heatmap = nullptr) const;
    bool drawWireframe(bgfx::ViewId view, bgfx::DynamicVertexBufferHandle vertices,
                       std::uint32_t vertex_count, const Frame &frame) const;

private:
    void bindSurface(const Frame &frame, const float *tile_rect) const;

    bgfx::ProgramHandle surface_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle wireframe_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle view_projection_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tile_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle background_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle origin_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle light_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shading_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tile_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle missing_texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cap_texture = BGFX_INVALID_HANDLE;
};

#endif // MAP_BGFX_GLOBE_TERRAIN_GPU_H
