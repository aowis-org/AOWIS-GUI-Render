#include "map/bgfx/bgfx_native_target_registry.h"

#include <utility>

BgfxNativeTargetRegistry::BgfxNativeTargetRegistry(Create create, Bind bind, Destroy destroy)
    : create_api(std::move(create)), bind_api(std::move(bind)), destroy_api(std::move(destroy))
{
    if (!this->create_api)
    {
        this->create_api = [](const BgfxNativeSurface &surface)
        {
            // The pinned bgfx API takes a SwapChain descriptor for native
            // windows; the old (nwh, width, height) overload no longer exists.
            // Keep the native display as well as the surface; this matters
            // for multiple X11/Wayland targets and future host adapters.
            return bgfx::createFrameBuffer(surface.swapChain());
        };
    }
    if (!this->bind_api)
    {
        this->bind_api = [](std::uint16_t view, bgfx::FrameBufferHandle framebuffer)
        {
            bgfx::setViewFrameBuffer(view, framebuffer);
        };
    }
    if (!this->destroy_api)
    {
        this->destroy_api = [](bgfx::FrameBufferHandle framebuffer)
        {
            bgfx::destroy(framebuffer);
        };
    }
}

bool BgfxNativeTargetRegistry::contains(const void *client) const
{
    for (const Target &target : this->targets)
    {
        if (target.client == client)
            return true;
    }
    return false;
}

bool BgfxNativeTargetRegistry::isDefault(const void *client) const
{
    for (const Target &target : this->targets)
    {
        if (target.client == client)
            return target.primary;
    }
    return false;
}

bool BgfxNativeTargetRegistry::onlyClient(const void *client) const
{
    for (const Target &target : this->targets)
    {
        if (target.client != client)
            return false;
    }
    return true;
}

bool BgfxNativeTargetRegistry::attachDefault(const void *client, std::uint16_t view_id)
{
    if (client == nullptr || contains(client))
        return false;
    for (const Target &target : this->targets)
    {
        if (target.view_id == view_id || target.primary)
            return false;
    }
    this->bind_api(view_id, bgfx::FrameBufferHandle{bgfx::kInvalidHandle});
    Target target;
    target.client = client;
    target.view_id = view_id;
    target.primary = true;
    this->targets.push_back(target);
    return true;
}

bool BgfxNativeTargetRegistry::attachWindow(const void *client, std::uint16_t view_id,
                                            const BgfxNativeSurface &surface,
                                            bool swap_chain_supported)
{
    if (client == nullptr || contains(client) || !surface.valid()
        || !swap_chain_supported)
        return false;
    // bgfx must have a primary window from bgfx::init().
    bool has_primary = false;
    for (const Target &target : this->targets)
    {
        if (target.view_id == view_id
            || (target.surface.window != nullptr && target.surface.sameWindow(surface)))
            return false;
        has_primary = has_primary || target.primary;
    }
    if (!has_primary)
        return false;
    const bgfx::FrameBufferHandle framebuffer = this->create_api(surface);
    if (!bgfx::isValid(framebuffer))
        return false;
    this->bind_api(view_id, framebuffer);
    Target target;
    target.client = client;
    target.view_id = view_id;
    target.surface = surface;
    target.framebuffer = framebuffer;
    this->targets.push_back(target);
    return true;
}

bool BgfxNativeTargetRegistry::resizeWindow(const void *client,
                                            const BgfxNativeSurface &surface)
{
    if (!surface.valid())
        return false;
    for (Target &target : this->targets)
    {
        if (target.client != client)
            continue;
        if (target.primary)
            return false; // The default backbuffer is resized via bgfx::reset().
        // A changed native display/handle invalidates the old swapchain.
        // The owner must release it while the old surface still exists,
        // then attach the new surface after Qt has created it.
        if (!target.surface.sameWindow(surface))
            return false;
        if (target.surface.width == surface.width && target.surface.height == surface.height)
            return true;
        // Allocate before retiring the old target: an allocation failure
        // must leave the existing visible window drawable.
        const bgfx::FrameBufferHandle replacement =
            this->create_api(surface);
        if (!bgfx::isValid(replacement))
            return false;
        this->bind_api(target.view_id, replacement);
        this->destroy_api(target.framebuffer);
        target.framebuffer = replacement;
        target.surface = surface;
        return true;
    }
    return false;
}

bool BgfxNativeTargetRegistry::release(const void *client)
{
    for (std::vector<Target>::iterator it = this->targets.begin();
         it != this->targets.end(); ++it)
    {
        if (it->client != client)
            continue;
        // bgfx keeps framebuffer bindings per view. Clear that reference
        // *before* destroying the backing swapchain.
        this->bind_api(it->view_id, bgfx::FrameBufferHandle{bgfx::kInvalidHandle});
        if (!it->primary && bgfx::isValid(it->framebuffer))
            this->destroy_api(it->framebuffer);
        this->targets.erase(it);
        return true;
    }
    return false;
}

void BgfxNativeTargetRegistry::reset()
{
    // Destroy secondary swapchains before the primary bgfx device goes away.
    while (!this->targets.empty())
        release(this->targets.back().client);
}
