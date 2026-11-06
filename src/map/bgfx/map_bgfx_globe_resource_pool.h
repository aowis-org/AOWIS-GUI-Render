#ifndef MAP_BGFX_GLOBE_RESOURCE_POOL_H
#define MAP_BGFX_GLOBE_RESOURCE_POOL_H

#include "map/bgfx/map_bgfx_globe_gpu.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class QImage;

// Uncompressed RGBA8 premultiplied pixels, containing every mip level in
// descending size order. Produced from a host-provided image by Render; no
// GUI resource path or hydraulic model enters this API.
struct MapBgfxGlobeMipTexture
{
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> pixels;
};

// Qt is used only for CPU image preparation; GPU resource ownership below is
// independent of Qt, and can be verified against mock bgfx.
MapBgfxGlobeMipTexture mapBgfxGlobeMipTexture(const QImage &image);

// Owns the eight globe-network programs, six frame uniforms, two samplers,
// and two GPU model textures. Programs are loaded from Render's embedded
// shader families by a host-selected loader (normally MapBgfxBackend).
// Must be used on the owning bgfx submission thread, and released BEFORE
// bgfx::shutdown. The icon atlas is shared and is never owned here.
class MapBgfxGlobeResourcePool final
{
public:
    using ProgramLoader = bgfx::ProgramHandle (*)(const char *, const char *);

    MapBgfxGlobeResourcePool();
    MapBgfxGlobeResourcePool(const MapBgfxGlobeResourcePool &) = delete;
    MapBgfxGlobeResourcePool &operator=(const MapBgfxGlobeResourcePool &) = delete;

    // Reinitialization releases previous GPU handles. On ANY failure all
    // partially created resources are destroyed and ready() remains false.
    bool initialize(ProgramLoader load_program, const MapBgfxGlobeMipTexture &tank,
                    const MapBgfxGlobeMipTexture &reservoir);
    bool ready() const;
    void populateInput(MapBgfxGlobeGpu::Input *input) const;
    void release();

private:
    std::array<bgfx::ProgramHandle,
               static_cast<std::size_t>(MapBgfxGlobeProgram::Count)> programs;
    MapBgfxGlobeGpu::UniformHandles uniforms;
    bgfx::UniformHandle icon_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle albedo_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle tank_texture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle reservoir_texture = BGFX_INVALID_HANDLE;
};

#endif // MAP_BGFX_GLOBE_RESOURCE_POOL_H
