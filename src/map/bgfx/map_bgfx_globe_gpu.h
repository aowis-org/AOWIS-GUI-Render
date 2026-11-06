#ifndef MAP_BGFX_GLOBE_GPU_H
#define MAP_BGFX_GLOBE_GPU_H

#include "map/bgfx/map_bgfx_2d_style_binding.h"
#include "map/bgfx/map_bgfx_globe_draw_plan.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>

// Host-neutral production globe network GPU submission. The GUI still owns
// scene/model adaptation, picking, and resource lifetime. Render owns the
// pass ordering, vertex/index binding, uniforms, style binding and submit.
class MapBgfxGlobeGpu
{
public:
    struct StreamBuffer
    {
        bgfx::DynamicVertexBufferHandle handle = BGFX_INVALID_HANDLE;
        std::uint32_t count = 0;
    };

    struct FrameUniforms
    {
        float view_projection[16] = {};
        float viewport[4] = {};
        float params[4] = {};
        float camera_right[4] = {};
        float camera_up[4] = {};
        float camera_eye[4] = {};
    };

    struct UniformHandles
    {
        bgfx::UniformHandle view_projection = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle viewport = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle params = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle camera_right = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle camera_up = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle camera_eye = BGFX_INVALID_HANDLE;
    };

    struct Input
    {
        bgfx::ViewId view = 0;
        std::array<StreamBuffer, static_cast<std::size_t>(MapBgfxGlobeStream::Count)> streams = {};
        std::array<bgfx::ProgramHandle, static_cast<std::size_t>(MapBgfxGlobeProgram::Count)> programs = {};
        UniformHandles uniforms;
        bgfx::TextureHandle icon_atlas = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle tank_texture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle reservoir_texture = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle icon_sampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle albedo_sampler = BGFX_INVALID_HANDLE;
        bgfx::DynamicIndexBufferHandle icon_indices = BGFX_INVALID_HANDLE;
        std::uint32_t icon_index_count = 0;
        MapBgfxGlobeUndergroundMode underground = MapBgfxGlobeUndergroundMode::XRay;
        bool show_junctions = true;

        void setStream(MapBgfxGlobeStream name, bgfx::DynamicVertexBufferHandle handle,
                       std::uint32_t count)
        {
            this->streams[static_cast<std::size_t>(name)] = {handle, count};
        }
        void setProgram(MapBgfxGlobeProgram name, bgfx::ProgramHandle handle)
        {
            this->programs[static_cast<std::size_t>(name)] = handle;
        }
    };

    // Number of submitted draws, not number of possible passes. Empty
    // streams are skipped; no GPU allocation or bgfx::frame() occurs here.
    static std::uint32_t draw(const Input &input, const FrameUniforms &frame,
                              const MapBgfx2dStyleBinding &entity_style);
};

#endif // MAP_BGFX_GLOBE_GPU_H
