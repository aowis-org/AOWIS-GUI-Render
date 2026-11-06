#include "map/bgfx/map_bgfx_globe_terrain_gpu.h"

namespace
{
constexpr std::uint64_t kTileSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint64_t kSurfaceState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_MSAA;
constexpr std::uint64_t kWireframeState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_PT_LINES | BGFX_STATE_MSAA;
constexpr float kWholeTexture[4] = {0.0f, 0.0f, 1.0f, 1.0f};

template <typename Handle>
void destroyHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
    {
        bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
}

bgfx::TextureHandle solidTexture(const std::array<std::uint8_t, 4> &rgba)
{
    return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                 kTileSamplerFlags, bgfx::copy(rgba.data(), 4));
}
}

bool MapBgfxGlobeTerrainGpu::initialize(ProgramLoader load_program,
                                       const std::array<std::uint8_t, 4> &missing_rgba,
                                       const std::array<std::uint8_t, 4> &cap_rgba)
{
    this->release();
    if (load_program == nullptr)
        return false;
    this->surface_program = load_program("vs_globe_surface", "fs_globe_surface");
    this->wireframe_program = load_program("vs_globe_wireframe", "fs_globe_wireframe");
    this->view_projection_uniform = bgfx::createUniform("u_globe_view_proj", bgfx::UniformType::Mat4);
    this->tile_uniform = bgfx::createUniform("u_globe_tile", bgfx::UniformType::Vec4);
    this->background_uniform = bgfx::createUniform("u_globe_background", bgfx::UniformType::Vec4);
    this->origin_uniform = bgfx::createUniform("u_globe_origin", bgfx::UniformType::Vec4);
    this->light_uniform = bgfx::createUniform("u_globe_light", bgfx::UniformType::Vec4);
    this->shading_uniform = bgfx::createUniform("u_globe_shading", bgfx::UniformType::Vec4);
    this->tile_sampler = bgfx::createUniform("s_globe_tile", bgfx::UniformType::Sampler);
    this->missing_texture = solidTexture(missing_rgba);
    this->cap_texture = solidTexture(cap_rgba);
    if (!this->ready())
    {
        this->release();
        return false;
    }
    return true;
}

bool MapBgfxGlobeTerrainGpu::ready() const
{
    return bgfx::isValid(this->surface_program) && bgfx::isValid(this->wireframe_program)
        && bgfx::isValid(this->view_projection_uniform) && bgfx::isValid(this->tile_uniform)
        && bgfx::isValid(this->background_uniform) && bgfx::isValid(this->origin_uniform)
        && bgfx::isValid(this->light_uniform) && bgfx::isValid(this->shading_uniform)
        && bgfx::isValid(this->tile_sampler) && bgfx::isValid(this->missing_texture)
        && bgfx::isValid(this->cap_texture);
}

void MapBgfxGlobeTerrainGpu::release()
{
    destroyHandle(&this->missing_texture);
    destroyHandle(&this->cap_texture);
    destroyHandle(&this->tile_sampler);
    destroyHandle(&this->shading_uniform);
    destroyHandle(&this->light_uniform);
    destroyHandle(&this->origin_uniform);
    destroyHandle(&this->background_uniform);
    destroyHandle(&this->tile_uniform);
    destroyHandle(&this->view_projection_uniform);
    destroyHandle(&this->wireframe_program);
    destroyHandle(&this->surface_program);
}

void MapBgfxGlobeTerrainGpu::bindSurface(const Frame &frame, const float *tile_rect) const
{
    bgfx::setUniform(this->view_projection_uniform, frame.view_projection.data());
    bgfx::setUniform(this->tile_uniform, tile_rect);
    bgfx::setUniform(this->background_uniform, frame.background.data());
    bgfx::setUniform(this->origin_uniform, frame.origin.data());
    bgfx::setUniform(this->light_uniform, frame.light.data());
    bgfx::setUniform(this->shading_uniform, frame.shading.data());
}

bool MapBgfxGlobeTerrainGpu::drawCap(bgfx::ViewId view,
                                     bgfx::DynamicVertexBufferHandle vertices,
                                     bgfx::DynamicIndexBufferHandle indices,
                                     std::uint32_t first_index, std::uint32_t index_count,
                                     const Frame &frame) const
{
    if (!this->ready() || !bgfx::isValid(vertices) || !bgfx::isValid(indices) || index_count == 0)
        return false;
    bgfx::setVertexBuffer(0, vertices);
    bgfx::setIndexBuffer(indices, first_index, index_count);
    bgfx::setTexture(0, this->tile_sampler, this->cap_texture, std::uint32_t(kTileSamplerFlags));
    bindSurface(frame, kWholeTexture);
    bgfx::setState(kSurfaceState);
    bgfx::submit(view, this->surface_program);
    return true;
}

bool MapBgfxGlobeTerrainGpu::drawTile(bgfx::ViewId view,
                                      bgfx::DynamicVertexBufferHandle vertices,
                                      std::uint32_t first_vertex, std::uint32_t vertex_count,
                                      bgfx::IndexBufferHandle indices,
                                      bgfx::TextureHandle imagery, const float *tile_rect,
                                      const Frame &frame, const Heatmap *heatmap) const
{
    if (!this->ready() || !bgfx::isValid(vertices) || !bgfx::isValid(indices)
        || !bgfx::isValid(imagery) || tile_rect == nullptr || vertex_count == 0)
        return false;
    if (heatmap != nullptr
        && (!bgfx::isValid(heatmap->program) || !bgfx::isValid(heatmap->sampler)
            || !bgfx::isValid(heatmap->tile_uniform) || !bgfx::isValid(heatmap->style_uniform)
            || !bgfx::isValid(heatmap->texture) || heatmap->tile_rect == nullptr
            || heatmap->style == nullptr))
        return false;
    bgfx::setVertexBuffer(0, vertices, first_vertex, vertex_count);
    bgfx::setIndexBuffer(indices);
    bgfx::setTexture(0, this->tile_sampler, imagery, std::uint32_t(kTileSamplerFlags));
    bindSurface(frame, tile_rect);
    bgfx::setState(kSurfaceState);
    if (heatmap != nullptr)
    {
        bgfx::setTexture(1, heatmap->sampler, heatmap->texture, std::uint32_t(kTileSamplerFlags));
        bgfx::setUniform(heatmap->tile_uniform, heatmap->tile_rect);
        bgfx::setUniform(heatmap->style_uniform, heatmap->style);
        bgfx::submit(view, heatmap->program);
    }
    else
    {
        bgfx::submit(view, this->surface_program);
    }
    return true;
}

bool MapBgfxGlobeTerrainGpu::drawWireframe(bgfx::ViewId view,
                                           bgfx::DynamicVertexBufferHandle vertices,
                                           std::uint32_t vertex_count, const Frame &frame) const
{
    if (!this->ready() || !bgfx::isValid(vertices) || vertex_count == 0)
        return false;
    bgfx::setVertexBuffer(0, vertices, 0, vertex_count);
    bgfx::setUniform(this->view_projection_uniform, frame.view_projection.data());
    bgfx::setState(kWireframeState);
    bgfx::submit(view, this->wireframe_program);
    return true;
}
