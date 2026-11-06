#include "map/bgfx/map_bgfx_globe_gpu.h"

namespace
{
constexpr std::uint64_t kBlendState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
constexpr std::uint64_t kDepthState = kBlendState | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL;
constexpr std::uint64_t kDepthTestState = kBlendState | BGFX_STATE_DEPTH_TEST_LEQUAL;
constexpr std::uint64_t kModelState = kDepthState | BGFX_STATE_CULL_CW;

std::uint64_t stateFor(MapBgfxGlobeState state)
{
    switch (state)
    {
    case MapBgfxGlobeState::Overlay: return kBlendState;
    case MapBgfxGlobeState::DepthWrite: return kDepthState;
    case MapBgfxGlobeState::DepthTest: return kDepthTestState;
    case MapBgfxGlobeState::Model: return kModelState;
    }
    return kBlendState;
}
}

std::uint32_t MapBgfxGlobeGpu::draw(const Input &input, const FrameUniforms &frame,
                                    const MapBgfx2dStyleBinding &entity_style)
{
    if (!entity_style.ready())
        return 0;

    std::uint32_t draw_calls = 0;
    mapBgfxGlobeVisitPasses(input.underground, input.show_junctions, bgfx::isValid(input.icon_atlas),
                            [&input, &frame, &entity_style, &draw_calls](const MapBgfxGlobeDrawPass &pass)
    {
        const StreamBuffer &stream = input.streams[static_cast<std::size_t>(pass.stream)];
        if (stream.count == 0 || !bgfx::isValid(stream.handle))
            return;
        const bgfx::ProgramHandle program = input.programs[static_cast<std::size_t>(pass.program)];
        if (!bgfx::isValid(program))
            return;

        bgfx::setVertexBuffer(0, stream.handle, 0, stream.count);
        if (pass.ordered_icons && input.icon_index_count != 0 && bgfx::isValid(input.icon_indices))
            bgfx::setIndexBuffer(input.icon_indices, 0, input.icon_index_count);

        entity_style.bind(pass.vertex_colors ? MapBgfx2dStyleBinding::Mode::VertexColor
                                             : MapBgfx2dStyleBinding::Mode::Symbology,
                          MapBgfx2dStyleBinding::Table::Nodes, true);
        bgfx::setUniform(input.uniforms.view_projection, frame.view_projection);
        bgfx::setUniform(input.uniforms.viewport, frame.viewport);
        bgfx::setUniform(input.uniforms.params, frame.params);
        bgfx::setUniform(input.uniforms.camera_right, frame.camera_right);
        bgfx::setUniform(input.uniforms.camera_up, frame.camera_up);
        bgfx::setUniform(input.uniforms.camera_eye, frame.camera_eye);

        if (pass.texture != MapBgfxGlobeTexture::None)
        {
            bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
            bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
            switch (pass.texture)
            {
            case MapBgfxGlobeTexture::Tank:
                texture = input.tank_texture;
                sampler = input.albedo_sampler;
                break;
            case MapBgfxGlobeTexture::Reservoir:
                texture = input.reservoir_texture;
                sampler = input.albedo_sampler;
                break;
            case MapBgfxGlobeTexture::Icon:
                texture = input.icon_atlas;
                sampler = input.icon_sampler;
                break;
            case MapBgfxGlobeTexture::None: break;
            }
            if (bgfx::isValid(texture) && bgfx::isValid(sampler))
                bgfx::setTexture(0, sampler, texture);
        }
        bgfx::setState(stateFor(pass.state));
        bgfx::submit(input.view, program);
        ++draw_calls;
    });
    return draw_calls;
}
