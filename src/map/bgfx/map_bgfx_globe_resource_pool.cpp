#include "map/bgfx/map_bgfx_globe_resource_pool.h"

#include <array>
#include <cstdint>
#include <limits>

namespace
{
struct ShaderPair
{
    MapBgfxGlobeProgram slot;
    const char *vertex;
    const char *fragment;
};

constexpr std::array<ShaderPair, static_cast<std::size_t>(MapBgfxGlobeProgram::Count)> shaders = {{
    {MapBgfxGlobeProgram::Link, "vs_globe_link", "fs_globe_link"},
    {MapBgfxGlobeProgram::LinkXRay, "vs_globe_link", "fs_globe_link_xray"},
    {MapBgfxGlobeProgram::Flow, "vs_globe_flow", "fs_globe_link"},
    {MapBgfxGlobeProgram::Node, "vs_globe_node", "fs_globe_node"},
    {MapBgfxGlobeProgram::Junction, "vs_globe_junction", "fs_globe_junction"},
    {MapBgfxGlobeProgram::JunctionXRay, "vs_globe_junction", "fs_globe_junction_xray"},
    {MapBgfxGlobeProgram::Icon, "vs_globe_icon", "fs_globe_icon"},
    {MapBgfxGlobeProgram::Model, "vs_globe_model", "fs_globe_model"},
}};

constexpr std::uint64_t albedo_sampler_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

template <typename Handle>
void destroyHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
    {
        bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
}

bool validPixels(const MapBgfxGlobeMipTexture &image)
{
    if (image.width == 0 || image.height == 0 || image.pixels.empty()
        || image.pixels.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    // Each level is tightly packed RGBA8, including odd-sized downsamples.
    std::uint32_t width = image.width;
    std::uint32_t height = image.height;
    std::uint64_t expected = 0;
    while (true)
    {
        expected += std::uint64_t(width) * height * 4;
        if (width == 1 && height == 1)
            break;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return expected == image.pixels.size();
}

bgfx::TextureHandle createTexture(const MapBgfxGlobeMipTexture &image)
{
    return bgfx::createTexture2D(image.width, image.height, true, 1,
                                 bgfx::TextureFormat::RGBA8, albedo_sampler_flags,
                                 bgfx::copy(image.pixels.data(),
                                            static_cast<std::uint32_t>(image.pixels.size())));
}
}

MapBgfxGlobeResourcePool::MapBgfxGlobeResourcePool()
{
    const bgfx::ProgramHandle invalid = BGFX_INVALID_HANDLE;
    this->programs.fill(invalid);
}

bool MapBgfxGlobeResourcePool::initialize(ProgramLoader load_program,
                                          const MapBgfxGlobeMipTexture &tank,
                                          const MapBgfxGlobeMipTexture &reservoir)
{
    this->release();
    if (load_program == nullptr || !validPixels(tank) || !validPixels(reservoir))
        return false;
    for (const ShaderPair &shader : shaders)
    {
        this->programs[static_cast<std::size_t>(shader.slot)] =
            load_program(shader.vertex, shader.fragment);
        if (!bgfx::isValid(this->programs[static_cast<std::size_t>(shader.slot)]))
        {
            this->release();
            return false;
        }
    }
    this->uniforms.view_projection = bgfx::createUniform("u_net_view_proj", bgfx::UniformType::Mat4);
    this->uniforms.viewport = bgfx::createUniform("u_net_viewport", bgfx::UniformType::Vec4);
    this->uniforms.params = bgfx::createUniform("u_net_params", bgfx::UniformType::Vec4);
    this->uniforms.camera_right = bgfx::createUniform("u_net_camera_right", bgfx::UniformType::Vec4);
    this->uniforms.camera_up = bgfx::createUniform("u_net_camera_up", bgfx::UniformType::Vec4);
    this->uniforms.camera_eye = bgfx::createUniform("u_net_camera_eye", bgfx::UniformType::Vec4);
    this->icon_sampler = bgfx::createUniform("s_net_icons", bgfx::UniformType::Sampler);
    this->albedo_sampler = bgfx::createUniform("s_net_albedo", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(this->uniforms.view_projection)
        || !bgfx::isValid(this->uniforms.viewport)
        || !bgfx::isValid(this->uniforms.params)
        || !bgfx::isValid(this->uniforms.camera_right)
        || !bgfx::isValid(this->uniforms.camera_up)
        || !bgfx::isValid(this->uniforms.camera_eye)
        || !bgfx::isValid(this->icon_sampler) || !bgfx::isValid(this->albedo_sampler))
    {
        this->release();
        return false;
    }
    this->tank_texture = createTexture(tank);
    if (!bgfx::isValid(this->tank_texture))
    {
        this->release();
        return false;
    }
    this->reservoir_texture = createTexture(reservoir);
    if (!this->ready())
    {
        this->release();
        return false;
    }
    return true;
}

bool MapBgfxGlobeResourcePool::ready() const
{
    for (const bgfx::ProgramHandle program : this->programs)
    {
        if (!bgfx::isValid(program))
            return false;
    }
    return bgfx::isValid(this->uniforms.view_projection)
        && bgfx::isValid(this->uniforms.viewport)
        && bgfx::isValid(this->uniforms.params)
        && bgfx::isValid(this->uniforms.camera_right)
        && bgfx::isValid(this->uniforms.camera_up)
        && bgfx::isValid(this->uniforms.camera_eye)
        && bgfx::isValid(this->icon_sampler) && bgfx::isValid(this->albedo_sampler)
        && bgfx::isValid(this->tank_texture) && bgfx::isValid(this->reservoir_texture);
}

void MapBgfxGlobeResourcePool::populateInput(MapBgfxGlobeGpu::Input *input) const
{
    if (input == nullptr)
        return;
    input->programs = this->programs;
    input->uniforms = this->uniforms;
    input->icon_sampler = this->icon_sampler;
    input->albedo_sampler = this->albedo_sampler;
    input->tank_texture = this->tank_texture;
    input->reservoir_texture = this->reservoir_texture;
}

void MapBgfxGlobeResourcePool::release()
{
    destroyHandle(&this->reservoir_texture);
    destroyHandle(&this->tank_texture);
    destroyHandle(&this->albedo_sampler);
    destroyHandle(&this->icon_sampler);
    destroyHandle(&this->uniforms.camera_eye);
    destroyHandle(&this->uniforms.camera_up);
    destroyHandle(&this->uniforms.camera_right);
    destroyHandle(&this->uniforms.params);
    destroyHandle(&this->uniforms.viewport);
    destroyHandle(&this->uniforms.view_projection);
    for (bgfx::ProgramHandle &program : this->programs)
        destroyHandle(&program);
}
