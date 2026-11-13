#ifndef BGFX_ANTIALIASING_H
#define BGFX_ANTIALIASING_H

#include <bgfx/bgfx.h>

#include <cstdint>

// Multisample antialiasing of a bgfx back buffer (BGFX_RESET_MSAA_*).
// Sample counts: 0 = off, 2, 4, 8, 16. bgfx clamps a count the GPU does not
// support to the highest supported one below it when the back buffer is
// (re)created, so any of these values is safe to request.
constexpr int BgfxAntialiasingDefaultSamples = 4;
constexpr int BgfxAntialiasingMaximumSamples = 16;

inline bool bgfxAntialiasingSamplesValid(int samples)
{
    return samples == 0 || samples == 2 || samples == 4 || samples == 8 || samples == 16;
}

inline std::uint32_t bgfxMsaaResetFlag(int samples)
{
    if (samples >= 16)
        return BGFX_RESET_MSAA_X16;
    if (samples >= 8)
        return BGFX_RESET_MSAA_X8;
    if (samples >= 4)
        return BGFX_RESET_MSAA_X4;
    if (samples >= 2)
        return BGFX_RESET_MSAA_X2;
    return 0;
}

#endif // BGFX_ANTIALIASING_H
