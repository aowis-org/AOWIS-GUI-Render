#ifndef AOWIS_BGFX_NATIVE_TARGET_REGISTRY_H
#define AOWIS_BGFX_NATIVE_TARGET_REGISTRY_H

#include "map/bgfx/bgfx_native_surface.h"

#include <cstdint>
#include <functional>
#include <vector>

// Device-generation-scoped view-to-native-target ownership. The default
// swapchain belongs to bgfx::init/bgfx::reset and MUST NOT be destroyed here.
// Additional native windows use bgfx swap-chain framebuffers, when supported.
// No QWidget, QWindow or platform-handle discovery belongs to this class.
// All operations are on the single bgfx submission thread.
class BgfxNativeTargetRegistry final
{
public:
    using Create = std::function<bgfx::FrameBufferHandle(const BgfxNativeSurface &)>;
    using Bind = std::function<void(std::uint16_t, bgfx::FrameBufferHandle)>;
    using Destroy = std::function<void(bgfx::FrameBufferHandle)>;

    explicit BgfxNativeTargetRegistry(Create create = {}, Bind bind = {}, Destroy destroy = {});

    BgfxNativeTargetRegistry(const BgfxNativeTargetRegistry &) = delete;
    BgfxNativeTargetRegistry &operator=(const BgfxNativeTargetRegistry &) = delete;

    // The initial bgfx window is rendered into the default backbuffer.
    // Call once after bgfx::init(), before drawing this client's view.
    bool attachDefault(const void *client, std::uint16_t view_id);

    // A second *native* window requires BGFX_CAPS_SWAP_CHAIN. The caller
    // supplies a complete native-surface descriptor (display, window, type,
    // physical size). No global reset is performed for secondary windows.
    bool attachWindow(const void *client, std::uint16_t view_id,
                      const BgfxNativeSurface &surface, bool swap_chain_supported);
    // Resize replaces only the secondary framebuffer. If a toolkit has
    // replaced the native surface, release first and attach a new target.
    bool resizeWindow(const void *client, const BgfxNativeSurface &surface);
    bool release(const void *client);
    bool contains(const void *client) const;
    bool isDefault(const void *client) const;
    bool onlyClient(const void *client) const;
    std::size_t count() const { return this->targets.size(); }

    // Must be called BEFORE bgfx::shutdown(), including on failed startup.
    void reset();

private:
    struct Target
    {
        const void *client = nullptr;
        std::uint16_t view_id = 0;
        BgfxNativeSurface surface;
        bgfx::FrameBufferHandle framebuffer = {bgfx::kInvalidHandle};
        bool primary = false;
    };

    Create create_api;
    Bind bind_api;
    Destroy destroy_api;
    std::vector<Target> targets;
};

#endif // AOWIS_BGFX_NATIVE_TARGET_REGISTRY_H
