#include "map/bgfx/map_bgfx_hud_gpu.h"

#include "map/bgfx/map_bgfx_backend.h"

#include <QDebug>
#include <QImage>
#include <QRect>

#include <cstring>

namespace
{
// Premultiplied "over"; the HUD needs no depth and no MSAA resolve tricks.
constexpr uint64_t kHudState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)
    | BGFX_STATE_MSAA;
// Text runs and images are rasterized at the device pixel ratio and placed
// on the physical pixel grid: sample texels exactly, never blur them.
constexpr uint64_t kAtlasSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
    | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

template <typename Handle>
void destroyHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
    {
        bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
}
}

bool MapBgfxHudGpu::initialize()
{
    this->layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    this->program = MapBgfxBackend::loadProgram("vs_hud", "fs_hud");
    this->viewport_uniform = bgfx::createUniform("u_hud_viewport", bgfx::UniformType::Vec4);
    this->atlas_sampler = bgfx::createUniform("s_hud_atlas", bgfx::UniformType::Sampler);
    this->atlas_generation = 0;
    if (!ready())
    {
        qWarning() << "bgfx HUD: program" << bgfx::isValid(this->program)
                   << "uniforms" << bgfx::isValid(this->viewport_uniform)
                   << bgfx::isValid(this->atlas_sampler);
        return false;
    }
    return true;
}

bool MapBgfxHudGpu::ready() const
{
    return bgfx::isValid(this->program) && bgfx::isValid(this->viewport_uniform)
        && bgfx::isValid(this->atlas_sampler);
}

void MapBgfxHudGpu::release()
{
    destroyHandle(&this->atlas_texture);
    destroyHandle(&this->atlas_sampler);
    destroyHandle(&this->viewport_uniform);
    destroyHandle(&this->program);
    this->atlas_width = 0;
    this->atlas_height = 0;
}

void MapBgfxHudGpu::uploadAtlas(MapBgfxHudTextCache *cache)
{
    const QImage &atlas = cache->atlas();
    if (atlas.isNull())
        return;
    if (!bgfx::isValid(this->atlas_texture) || this->atlas_width != atlas.width()
        || this->atlas_height != atlas.height())
    {
        destroyHandle(&this->atlas_texture);
        this->atlas_texture = bgfx::createTexture2D(
            uint16_t(atlas.width()), uint16_t(atlas.height()), false, 1,
            bgfx::TextureFormat::RGBA8, kAtlasSamplerFlags, nullptr);
        this->atlas_width = atlas.width();
        this->atlas_height = atlas.height();
        // A fresh texture has undefined content: upload everything.
        const QRect full = atlas.rect();
        const uint32_t row_bytes = uint32_t(full.width()) * 4u;
        const bgfx::Memory *memory = bgfx::alloc(row_bytes * uint32_t(full.height()));
        for (int row = 0; row < full.height(); ++row)
            std::memcpy(memory->data + row_bytes * uint32_t(row), atlas.constScanLine(row), row_bytes);
        bgfx::updateTexture2D(this->atlas_texture, 0, 0, 0, 0, uint16_t(full.width()),
                              uint16_t(full.height()), memory);
        cache->markUploaded();
        return;
    }
    if (!cache->dirty())
        return;
    // Only the rows/columns that changed (new text runs).
    const QRect region = cache->dirtyRect().intersected(atlas.rect());
    if (region.isEmpty())
    {
        cache->markUploaded();
        return;
    }
    const uint32_t row_bytes = uint32_t(region.width()) * 4u;
    const bgfx::Memory *memory = bgfx::alloc(row_bytes * uint32_t(region.height()));
    for (int row = 0; row < region.height(); ++row)
    {
        const uchar *source = atlas.constScanLine(region.y() + row) + region.x() * 4;
        std::memcpy(memory->data + row_bytes * uint32_t(row), source, row_bytes);
    }
    bgfx::updateTexture2D(this->atlas_texture, 0, 0, uint16_t(region.x()), uint16_t(region.y()),
                          uint16_t(region.width()), uint16_t(region.height()), memory);
    cache->markUploaded();
}

void MapBgfxHudGpu::draw(bgfx::ViewId view, const MapBgfxHudCanvas &canvas,
                         const QSizeF &logical_viewport, const QPointF &offset)
{
    if (!ready() || !bgfx::isValid(this->atlas_texture) || canvas.indices().isEmpty()
        || canvas.overflowed())
        return;
    const uint32_t vertex_count = uint32_t(canvas.vertices().size());
    const uint32_t index_count = uint32_t(canvas.indices().size());
    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    // 32-bit indices (MapBgfxHudCanvas): the editor overlay exceeds 65535
    // vertices on large networks. Skip the frame's draw (with one warning)
    // rather than drawing a truncated list when the transient budget is out.
    if (bgfx::getAvailTransientVertexBuffer(vertex_count, this->layout) < vertex_count
        || bgfx::getAvailTransientIndexBuffer(index_count, true) < index_count
        || !bgfx::allocTransientBuffers(&vertices, this->layout, vertex_count,
                                        &indices, index_count, true))
    {
        if (!this->budget_warning_shown)
        {
            qWarning() << "bgfx HUD/overlay draw list exceeds the transient buffers:"
                       << vertex_count << "vertices" << index_count << "indices";
            this->budget_warning_shown = true;
        }
        return;
    }
    if (offset.isNull())
    {
        std::memcpy(vertices.data, canvas.vertices().constData(),
                    vertex_count * sizeof(MapBgfxHudVertex));
    }
    else
    {
        // Translated copy: same cost class as the memcpy, instead of
        // rebuilding the list for a panned camera.
        const float offset_x = float(offset.x());
        const float offset_y = float(offset.y());
        const MapBgfxHudVertex *source = canvas.vertices().constData();
        MapBgfxHudVertex *target = reinterpret_cast<MapBgfxHudVertex *>(vertices.data);
        for (uint32_t index = 0; index < vertex_count; ++index)
        {
            target[index] = source[index];
            target[index].x += offset_x;
            target[index].y += offset_y;
        }
    }
    std::memcpy(indices.data, canvas.indices().constData(), index_count * sizeof(quint32));
    const float viewport[4] = {float(logical_viewport.width()), float(logical_viewport.height()),
                               0.0f, 0.0f};
    bgfx::setUniform(this->viewport_uniform, viewport);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    bgfx::setTexture(0, this->atlas_sampler, this->atlas_texture, uint32_t(kAtlasSamplerFlags));
    bgfx::setState(kHudState);
    bgfx::submit(view, this->program);
}
