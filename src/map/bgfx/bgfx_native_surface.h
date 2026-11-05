#ifndef AOWIS_BGFX_NATIVE_SURFACE_H
#define AOWIS_BGFX_NATIVE_SURFACE_H

#include <bgfx/bgfx.h>

#include <cstdint>

// Platform-independent description of an already-created native window.
// The hosting toolkit owns the window/display: bgfx must release its native
// framebuffer BEFORE the toolkit destroys or replaces either handle.
// Values are device pixels, not Qt logical/widget dimensions. This type has
// no dependency on Qt, QWidget, QWindow, or a specific window system.
struct BgfxNativeSurface final
{
    void *window = nullptr;
    void *display = nullptr;
    bgfx::NativeWindowHandleType::Enum handle_type = bgfx::NativeWindowHandleType::Default;
    std::uint16_t width = 0;
    std::uint16_t height = 0;

    bool valid() const
    {
        return this->window != nullptr && this->width != 0 && this->height != 0
            && (this->handle_type != bgfx::NativeWindowHandleType::Wayland
                || this->display != nullptr);
    }

    bool sameWindow(const BgfxNativeSurface &other) const
    {
        return this->window == other.window && this->display == other.display
            && this->handle_type == other.handle_type;
    }

    // A default-constructed bgfx::SwapChain has NO depth attachment (its
    // formatDepthStencil is TextureFormat::Count). Unlike bgfx::Init, which
    // defaults to D24S8, constructing one for a window or reset must request
    // depth explicitly. Otherwise globe tile skirts draw over nearer terrain
    // despite BGFX_STATE_DEPTH_TEST_LEQUAL and BGFX_STATE_WRITE_Z.
    static bgfx::SwapChain resizeSwapChain(std::uint32_t width, std::uint32_t height)
    {
        bgfx::SwapChain chain{};
        chain.width = width;
        chain.height = height;
        chain.formatColor = bgfx::TextureFormat::BGRA8;
        chain.formatDepthStencil = bgfx::TextureFormat::D24S8;
        chain.numBackBuffers = 2;
        return chain;
    }

    bgfx::SwapChain swapChain() const
    {
        bgfx::SwapChain chain = resizeSwapChain(this->width, this->height);
        chain.nwh = this->window;
        chain.ndt = this->display;
        return chain;
    }

    void configurePrimary(bgfx::Init *settings) const
    {
        if (settings == nullptr)
            return;
        // Preserve the Init defaults (including the D24S8 depth attachment)
        // and any host-selected formats or backbuffer counts. Replacing the
        // entire descriptor with SwapChain{} silently discards those defaults.
        settings->swapChain.nwh = this->window;
        settings->swapChain.ndt = this->display;
        settings->swapChain.width = this->width;
        settings->swapChain.height = this->height;
        settings->platformData.type = this->handle_type;
    }
};

#endif // AOWIS_BGFX_NATIVE_SURFACE_H
