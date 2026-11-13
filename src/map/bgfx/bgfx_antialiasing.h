#ifndef BGFX_ANTIALIASING_H
#define BGFX_ANTIALIASING_H

#include <bgfx/bgfx.h>

#include <cstdint>

// Multisample antialiasing of a bgfx window back buffer. The pinned bgfx
// takes it per surface, in bgfx::SwapChain::flags (BGFX_SWAP_CHAIN_MSAA_*),
// for bgfx::init (Init::swapChain) and bgfx::reset(flags, &swapChain).
// BGFX_RESET_MSAA_* in the reset flags is masked out by bgfx::reset() ("per-
// surface flags passed to reset are ignored"), so it never had any effect.
// Sample counts: 0 = off, 2, 4, 8, 16; the renderer clamps a count the GPU
// does not support to its maximum.
constexpr int BgfxAntialiasingDefaultSamples = 4;
constexpr int BgfxAntialiasingMaximumSamples = 16;

inline bool bgfxAntialiasingSamplesValid(int samples)
{
    return samples == 0 || samples == 2 || samples == 4 || samples == 8 || samples == 16;
}

inline std::uint32_t bgfxMsaaSwapChainFlag(int samples)
{
    if (samples >= 16)
        return BGFX_SWAP_CHAIN_MSAA_X16;
    if (samples >= 8)
        return BGFX_SWAP_CHAIN_MSAA_X8;
    if (samples >= 4)
        return BGFX_SWAP_CHAIN_MSAA_X4;
    if (samples >= 2)
        return BGFX_SWAP_CHAIN_MSAA_X2;
    return BGFX_SWAP_CHAIN_NONE;
}

#endif // BGFX_ANTIALIASING_H
