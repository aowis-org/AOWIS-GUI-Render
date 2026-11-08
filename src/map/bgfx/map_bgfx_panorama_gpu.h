#pragma once

#include "map/bgfx/map_bgfx_panorama_grid.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

// Device-bound GPU state of the Look-around panorama test mode
// (map_bgfx_panorama_grid.h): the imagery program, its sampler and
// background uniform, and the equirectangular texture. The host computes the
// grid; draw() submits it as one transient vertex buffer, opaque, no depth.
class MapBgfxPanoramaGpu final
{
public:
    using ProgramLoader = bgfx::ProgramHandle (*)(const char *, const char *);

    MapBgfxPanoramaGpu() = default;
    MapBgfxPanoramaGpu(const MapBgfxPanoramaGpu &) = delete;
    MapBgfxPanoramaGpu &operator=(const MapBgfxPanoramaGpu &) = delete;
    ~MapBgfxPanoramaGpu() = default;

    bool initialize(ProgramLoader load_program);
    bool ready() const;
    // Replaces the texture with tightly packed RGBA8 rows (top row first).
    bool setImage(int width, int height, const std::uint8_t *rgba);
    bool hasImage() const;
    bool draw(bgfx::ViewId view, const std::vector<MapPanoramaVertex> &vertices) const;
    void releaseImage();
    void release();

private:
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle background_uniform = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
};
