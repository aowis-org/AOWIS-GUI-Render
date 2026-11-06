#include "map/bgfx/map_bgfx_globe_overlay_gpu.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace
{
constexpr std::uint64_t kSkyState = BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ADD | BGFX_STATE_MSAA;
constexpr float kSkyTriangle[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
static_assert(sizeof(MapBgfxGlobeHeatmapGpu::BakeVertex) == 48,
              "Heatmap shader vertex ABI must remain unchanged");
constexpr std::uint64_t kHeatmapSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint64_t kHeatmapClearState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
constexpr std::uint64_t kHeatmapStampState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

template <typename Handle>
void releaseHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
        bgfx::destroy(*handle);
    *handle = BGFX_INVALID_HANDLE;
}
}

bool MapBgfxGlobeSkyGpu::initialize(ProgramLoader load_program)
{
    release();
    if (load_program == nullptr)
        return false;
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float).end();
    this->program = load_program("vs_globe_sky", "fs_globe_sky");
    if (!bgfx::isValid(this->program))
    {
        release();
        return false;
    }
    this->triangle = bgfx::createVertexBuffer(bgfx::copy(kSkyTriangle, sizeof(kSkyTriangle)), layout);
    this->forward_uniform = bgfx::createUniform("u_sky_forward", bgfx::UniformType::Vec4);
    this->right_uniform = bgfx::createUniform("u_sky_right", bgfx::UniformType::Vec4);
    this->up_uniform = bgfx::createUniform("u_sky_up", bgfx::UniformType::Vec4);
    this->vertical_uniform = bgfx::createUniform("u_sky_vertical", bgfx::UniformType::Vec4);
    this->params_uniform = bgfx::createUniform("u_sky_params", bgfx::UniformType::Vec4);
    if (!ready())
    {
        release();
        return false;
    }
    return true;
}

bool MapBgfxGlobeSkyGpu::ready() const
{
    return bgfx::isValid(this->program) && bgfx::isValid(this->triangle)
        && bgfx::isValid(this->forward_uniform) && bgfx::isValid(this->right_uniform)
        && bgfx::isValid(this->up_uniform) && bgfx::isValid(this->vertical_uniform)
        && bgfx::isValid(this->params_uniform);
}

bool MapBgfxGlobeSkyGpu::draw(bgfx::ViewId view, const Frame &frame) const
{
    if (!ready())
        return false;
    bgfx::setVertexBuffer(0, this->triangle);
    bgfx::setUniform(this->forward_uniform, frame.forward.data());
    bgfx::setUniform(this->right_uniform, frame.right.data());
    bgfx::setUniform(this->up_uniform, frame.up.data());
    bgfx::setUniform(this->vertical_uniform, frame.vertical.data());
    bgfx::setUniform(this->params_uniform, frame.params.data());
    bgfx::setState(kSkyState);
    bgfx::submit(view, this->program);
    return true;
}

void MapBgfxGlobeSkyGpu::release()
{
    releaseHandle(&this->params_uniform);
    releaseHandle(&this->vertical_uniform);
    releaseHandle(&this->up_uniform);
    releaseHandle(&this->right_uniform);
    releaseHandle(&this->forward_uniform);
    releaseHandle(&this->triangle);
    releaseHandle(&this->program);
}

bool MapBgfxGlobeHeatmapGpu::initialize(ProgramLoader load_program)
{
    release();
    if (load_program == nullptr)
        return false;
    this->bake_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .end();
    if (this->bake_layout.getStride() != sizeof(BakeVertex))
        return false;
    this->surface_program = load_program("vs_globe_surface_heatmap", "fs_globe_surface_heatmap");
    this->bake_program = load_program("vs_globe_heatmap_bake", "fs_globe_heatmap_bake");
    this->tile_uniform = bgfx::createUniform("u_globe_heatmap", bgfx::UniformType::Vec4);
    this->style_uniform = bgfx::createUniform("u_globe_heatmap_style", bgfx::UniformType::Vec4);
    this->bake_uniform = bgfx::createUniform("u_heat_bake", bgfx::UniformType::Vec4);
    this->sampler = bgfx::createUniform("s_globe_heatmap", bgfx::UniformType::Sampler);
    if (!ready())
    {
        release();
        return false;
    }
    return true;
}

bool MapBgfxGlobeHeatmapGpu::ready() const
{
    return bgfx::isValid(this->surface_program) && bgfx::isValid(this->bake_program)
        && bgfx::isValid(this->tile_uniform) && bgfx::isValid(this->style_uniform)
        && bgfx::isValid(this->bake_uniform) && bgfx::isValid(this->sampler);
}

bool MapBgfxGlobeHeatmapGpu::ensurePage(int page)
{
    if (!ready() || page < 0 || page >= PageCount)
        return false;
    Page &target = this->pages[std::size_t(page)];
    if (bgfx::isValid(target.frame_buffer))
        return true;
    target.texture = bgfx::createTexture2D(PageSize, PageSize, false, 1,
                                           bgfx::TextureFormat::RGBA8,
                                           BGFX_TEXTURE_RT | kHeatmapSamplerFlags);
    if (bgfx::isValid(target.texture))
        target.frame_buffer = bgfx::createFrameBuffer(1, &target.texture, false);
    if (!bgfx::isValid(target.frame_buffer))
    {
        releaseHandle(&target.texture);
        return false;
    }
    return true;
}

bgfx::TextureHandle MapBgfxGlobeHeatmapGpu::pageTexture(int page) const
{
    if (page < 0 || page >= PageCount)
        return BGFX_INVALID_HANDLE;
    return this->pages[std::size_t(page)].texture;
}

MapBgfxGlobeTerrainGpu::Heatmap MapBgfxGlobeHeatmapGpu::composite(
    int page, const float *tile_rect, const float *style) const
{
    MapBgfxGlobeTerrainGpu::Heatmap result;
    if (!ready() || page < 0 || page >= PageCount || !bgfx::isValid(pageTexture(page)))
        return result;
    result.program = this->surface_program;
    result.sampler = this->sampler;
    result.tile_uniform = this->tile_uniform;
    result.style_uniform = this->style_uniform;
    result.texture = pageTexture(page);
    result.tile_rect = tile_rect;
    result.style = style;
    return result;
}

void MapBgfxGlobeHeatmapGpu::orderViews(bgfx::ViewId map_view,
                                         bgfx::ViewId first_bake_view) const
{
    if (!ready())
        return;
    bgfx::ViewId order[PageCount + 1];
    for (int page = 0; page < PageCount; ++page)
        order[page] = bgfx::ViewId(first_bake_view + page);
    order[PageCount] = map_view;
    bgfx::setViewOrder(map_view, PageCount + 1, order);
}

bool MapBgfxGlobeHeatmapGpu::bakePage(bgfx::ViewId bake_view, int page,
                                       const BakeVertex *clears, std::uint32_t clear_count,
                                       const BakeVertex *stamps, std::uint32_t stamp_count) const
{
    if (!ready() || page < 0 || page >= PageCount || !bgfx::isValid(this->pages[std::size_t(page)].frame_buffer)
        || clears == nullptr || clear_count == 0 || (stamp_count > 0 && stamps == nullptr)
        || clear_count > std::numeric_limits<std::uint32_t>::max() - stamp_count)
        return false;
    if (bgfx::getAvailTransientVertexBuffer(clear_count + stamp_count, this->bake_layout)
        < clear_count + stamp_count)
        return false;

    const bgfx::Caps *caps = bgfx::getCaps();
    const float bake_parameters[4] = {float(PageSize),
                                      caps != nullptr && caps->originBottomLeft ? -1.0f : 1.0f,
                                      0.0f, 0.0f};
    bgfx::setViewFrameBuffer(bake_view, this->pages[std::size_t(page)].frame_buffer);
    bgfx::setViewRect(bake_view, 0, 0, PageSize, PageSize);
    bgfx::setViewClear(bake_view, BGFX_CLEAR_NONE);
    bgfx::setViewMode(bake_view, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(bake_view, nullptr, nullptr);
    bgfx::TransientVertexBuffer clear_buffer;
    bgfx::allocTransientVertexBuffer(&clear_buffer, clear_count, this->bake_layout);
    std::copy_n(clears, clear_count, reinterpret_cast<BakeVertex *>(clear_buffer.data));
    bgfx::setVertexBuffer(0, &clear_buffer);
    bgfx::setUniform(this->bake_uniform, bake_parameters);
    bgfx::setState(kHeatmapClearState);
    bgfx::submit(bake_view, this->bake_program);
    if (stamp_count > 0)
    {
        bgfx::TransientVertexBuffer stamp_buffer;
        bgfx::allocTransientVertexBuffer(&stamp_buffer, stamp_count, this->bake_layout);
        std::copy_n(stamps, stamp_count, reinterpret_cast<BakeVertex *>(stamp_buffer.data));
        bgfx::setVertexBuffer(0, &stamp_buffer);
        bgfx::setUniform(this->bake_uniform, bake_parameters);
        bgfx::setState(kHeatmapStampState);
        bgfx::submit(bake_view, this->bake_program);
    }
    return true;
}

void MapBgfxGlobeHeatmapGpu::release()
{
    for (Page &page : this->pages)
    {
        releaseHandle(&page.frame_buffer);
        releaseHandle(&page.texture);
    }
    releaseHandle(&this->sampler);
    releaseHandle(&this->bake_uniform);
    releaseHandle(&this->style_uniform);
    releaseHandle(&this->tile_uniform);
    releaseHandle(&this->bake_program);
    releaseHandle(&this->surface_program);
}
