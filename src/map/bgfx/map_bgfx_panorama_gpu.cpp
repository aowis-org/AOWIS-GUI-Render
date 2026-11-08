#include "map/bgfx/map_bgfx_panorama_gpu.h"

#include <algorithm>
#include <cstring>

namespace
{
// The panorama wraps around horizontally; vertically it ends at the poles.
constexpr std::uint32_t kPanoramaSamplerFlags = BGFX_SAMPLER_V_CLAMP;
static_assert(sizeof(MapPanoramaVertex) == 16, "Imagery program vertex ABI: x, y, u, v floats");

template <typename Handle>
void releaseHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
        bgfx::destroy(*handle);
    *handle = BGFX_INVALID_HANDLE;
}
}

bool MapBgfxPanoramaGpu::initialize(ProgramLoader load_program)
{
    release();
    if (load_program == nullptr)
        return false;
    // The imagery program's vertex: clip-space position, texture coordinate.
    this->layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    this->program = load_program("vs_imagery_2d", "fs_imagery_2d");
    if (!bgfx::isValid(this->program))
    {
        release();
        return false;
    }
    this->sampler = bgfx::createUniform("s_tile", bgfx::UniformType::Sampler);
    this->background_uniform = bgfx::createUniform("u_imagery_background", bgfx::UniformType::Vec4);
    if (!ready())
    {
        release();
        return false;
    }
    return true;
}

bool MapBgfxPanoramaGpu::ready() const
{
    return bgfx::isValid(this->program) && bgfx::isValid(this->sampler)
        && bgfx::isValid(this->background_uniform);
}

bool MapBgfxPanoramaGpu::setImage(int width, int height, const std::uint8_t *rgba)
{
    releaseImage();
    if (!ready() || rgba == nullptr || width < 1 || height < 1 || width > 65535 || height > 65535)
        return false;
    const std::uint32_t bytes = std::uint32_t(width) * std::uint32_t(height) * 4u;
    this->texture = bgfx::createTexture2D(std::uint16_t(width), std::uint16_t(height), false, 1,
                                          bgfx::TextureFormat::RGBA8, kPanoramaSamplerFlags,
                                          bgfx::copy(rgba, bytes));
    return bgfx::isValid(this->texture);
}

bool MapBgfxPanoramaGpu::hasImage() const
{
    return bgfx::isValid(this->texture);
}

bool MapBgfxPanoramaGpu::draw(bgfx::ViewId view, const std::vector<MapPanoramaVertex> &vertices) const
{
    if (!ready() || !hasImage() || vertices.empty())
        return false;
    const std::uint32_t count = std::uint32_t(vertices.size());
    if (bgfx::getAvailTransientVertexBuffer(count, this->layout) < count)
        return false;
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, count, this->layout);
    std::memcpy(buffer.data, vertices.data(), std::size_t(count) * sizeof(MapPanoramaVertex));
    // The image as it is: no background blend.
    const float background[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::setTexture(0, this->sampler, this->texture, kPanoramaSamplerFlags);
    bgfx::setUniform(this->background_uniform, background);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(view, this->program);
    return true;
}

void MapBgfxPanoramaGpu::releaseImage()
{
    releaseHandle(&this->texture);
}

void MapBgfxPanoramaGpu::release()
{
    releaseImage();
    releaseHandle(&this->background_uniform);
    releaseHandle(&this->sampler);
    releaseHandle(&this->program);
}
