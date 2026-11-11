#include "map/bgfx/map_bgfx_label_gpu.h"

#include "map/bgfx/map_bgfx_backend.h"

#include <QDebug>
#include <QImage>
#include <QRect>

#include <algorithm>
#include <cstring>

namespace
{
// Premultiplied "over", like the HUD.
constexpr uint64_t kLabelState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)
    | BGFX_STATE_MSAA;
// Distances are interpolated: linear filtering is what makes an SDF scale.
constexpr uint64_t kAtlasSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
    | BGFX_SAMPLER_MIP_POINT;

template <typename Handle>
void destroyHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
    {
        bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
}

// Rows of region of an 8-bit image, tightly packed for bgfx.
const bgfx::Memory *copyRegion(const QImage &image, const QRect &region)
{
    const uint32_t row_bytes = uint32_t(region.width());
    const bgfx::Memory *memory = bgfx::alloc(row_bytes * uint32_t(region.height()));
    for (int row = 0; row < region.height(); ++row)
    {
        const uchar *source = image.constScanLine(region.y() + row) + region.x();
        std::memcpy(memory->data + row_bytes * uint32_t(row), source, row_bytes);
    }
    return memory;
}
}

bool MapBgfxLabelGpu::initialize()
{
    this->layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    this->program = MapBgfxBackend::loadProgram("vs_hud", "fs_label_sdf");
    // Same name as the HUD's: bgfx shares the uniform (reference counted).
    this->viewport_uniform = bgfx::createUniform("u_hud_viewport", bgfx::UniformType::Vec4);
    this->halo_uniform = bgfx::createUniform("u_label_halo", bgfx::UniformType::Vec4);
    this->params_uniform = bgfx::createUniform("u_label_params", bgfx::UniformType::Vec4);
    this->atlas_size_uniform = bgfx::createUniform("u_label_atlas", bgfx::UniformType::Vec4);
    this->atlas_sampler = bgfx::createUniform("s_label_sdf", bgfx::UniformType::Sampler);
    if (!ready())
    {
        qWarning() << "bgfx labels: program" << bgfx::isValid(this->program)
                   << "uniforms" << bgfx::isValid(this->viewport_uniform)
                   << bgfx::isValid(this->halo_uniform) << bgfx::isValid(this->params_uniform)
                   << bgfx::isValid(this->atlas_size_uniform) << bgfx::isValid(this->atlas_sampler);
        return false;
    }
    return true;
}

bool MapBgfxLabelGpu::ready() const
{
    return bgfx::isValid(this->program) && bgfx::isValid(this->viewport_uniform)
        && bgfx::isValid(this->halo_uniform) && bgfx::isValid(this->params_uniform)
        && bgfx::isValid(this->atlas_size_uniform) && bgfx::isValid(this->atlas_sampler);
}

void MapBgfxLabelGpu::release()
{
    destroyHandle(&this->atlas_texture);
    destroyHandle(&this->atlas_sampler);
    destroyHandle(&this->atlas_size_uniform);
    destroyHandle(&this->params_uniform);
    destroyHandle(&this->halo_uniform);
    destroyHandle(&this->viewport_uniform);
    destroyHandle(&this->program);
    this->atlas_width = 0;
    this->atlas_height = 0;
}

void MapBgfxLabelGpu::uploadAtlas(MapBgfxGlyphAtlas *atlas)
{
    const QImage &image = atlas->atlas();
    if (image.isNull())
        return;
    if (!bgfx::isValid(this->atlas_texture) || this->atlas_width != image.width()
        || this->atlas_height != image.height())
    {
        destroyHandle(&this->atlas_texture);
        this->atlas_texture = bgfx::createTexture2D(
            uint16_t(image.width()), uint16_t(image.height()), false, 1,
            bgfx::TextureFormat::R8, kAtlasSamplerFlags, nullptr);
        this->atlas_width = image.width();
        this->atlas_height = image.height();
        // A fresh texture has undefined content: upload everything.
        const QRect full = image.rect();
        bgfx::updateTexture2D(this->atlas_texture, 0, 0, 0, 0, uint16_t(full.width()),
                              uint16_t(full.height()), copyRegion(image, full));
        atlas->markUploaded();
        return;
    }
    if (!atlas->dirty())
        return;
    const QRect region = atlas->dirtyRect().intersected(image.rect());
    if (!region.isEmpty())
    {
        bgfx::updateTexture2D(this->atlas_texture, 0, 0, uint16_t(region.x()), uint16_t(region.y()),
                              uint16_t(region.width()), uint16_t(region.height()),
                              copyRegion(image, region));
    }
    atlas->markUploaded();
}

void MapBgfxLabelGpu::draw(bgfx::ViewId view, const MapBgfxHudCanvas &canvas,
                           const QSizeF &logical_viewport, quint32 halo_argb, double halo_px,
                           double device_pixel_ratio, const QPointF &offset)
{
    if (!ready() || !bgfx::isValid(this->atlas_texture) || canvas.indices().isEmpty()
        || canvas.overflowed())
        return;
    const uint32_t vertex_count = uint32_t(canvas.vertices().size());
    const uint32_t index_count = uint32_t(canvas.indices().size());
    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    if (bgfx::getAvailTransientVertexBuffer(vertex_count, this->layout) < vertex_count
        || bgfx::getAvailTransientIndexBuffer(index_count, true) < index_count
        || !bgfx::allocTransientBuffers(&vertices, this->layout, vertex_count,
                                        &indices, index_count, true))
    {
        if (!this->budget_warning_shown)
        {
            qWarning() << "bgfx label draw list exceeds the transient buffers:"
                       << vertex_count << "vertices" << index_count << "indices";
            this->budget_warning_shown = true;
        }
        return;
    }
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
    std::memcpy(indices.data, canvas.indices().constData(), index_count * sizeof(quint32));

    const double ratio = device_pixel_ratio > 0.0 ? device_pixel_ratio : 1.0;
    const float alpha = float((halo_argb >> 24) & 0xffu) / 255.0f;
    const float halo[4] = {float((halo_argb >> 16) & 0xffu) / 255.0f * alpha,
                           float((halo_argb >> 8) & 0xffu) / 255.0f * alpha,
                           float(halo_argb & 0xffu) / 255.0f * alpha, alpha};
    const float params[4] = {float(std::max(0.0, halo_px) * ratio),
                             float(MapBgfxGlyphAtlas::SpreadPx), float(FillBiasPx), 0.0f};
    const float atlas_size[4] = {float(this->atlas_width), float(this->atlas_height), 0.0f, 0.0f};
    const float viewport[4] = {float(logical_viewport.width()), float(logical_viewport.height()),
                               0.0f, 0.0f};
    bgfx::setUniform(this->viewport_uniform, viewport);
    bgfx::setUniform(this->halo_uniform, halo);
    bgfx::setUniform(this->params_uniform, params);
    bgfx::setUniform(this->atlas_size_uniform, atlas_size);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    bgfx::setTexture(0, this->atlas_sampler, this->atlas_texture, uint32_t(kAtlasSamplerFlags));
    bgfx::setState(kLabelState);
    bgfx::submit(view, this->program);
}
