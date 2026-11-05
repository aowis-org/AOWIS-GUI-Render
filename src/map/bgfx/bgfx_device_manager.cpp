#include "map/bgfx/bgfx_device_manager.h"

#include <limits>
#include <utility>

BgfxDeviceManager::BgfxDeviceManager(
    Initialize initialize, Shutdown shutdown,
    BgfxNativeTargetRegistry::Create create_target,
    BgfxNativeTargetRegistry::Bind bind_target,
    BgfxNativeTargetRegistry::Destroy destroy_target)
    : initialize_api(std::move(initialize)), shutdown_api(std::move(shutdown)),
      frame_coordinator([]() { bgfx::frame(); }),
      native_targets(std::move(create_target), std::move(bind_target),
                     std::move(destroy_target))
{
    if (!this->initialize_api)
        this->initialize_api = [](const bgfx::Init &settings) { return bgfx::init(settings); };
    if (!this->shutdown_api)
        this->shutdown_api = []() { bgfx::shutdown(); };
}

BgfxDeviceManager &BgfxDeviceManager::shared()
{
    static BgfxDeviceManager manager;
    return manager;
}

bool BgfxDeviceManager::prepareFor(const void *requester)
{
    if (requester == nullptr)
        return false;
    if (this->device_owner == nullptr || this->device_owner == requester)
        return true;
    if (!this->owner_visibility || !this->owner_retire || this->owner_visibility())
        return false;

    // Copy before invocation: the callback shuts down and clears the stored
    // callback, which must not destroy the std::function during its own call.
    const Retire retire = this->owner_retire;
    retire();
    return this->device_owner == nullptr;
}

bool BgfxDeviceManager::initialize(const void *requester, const bgfx::Init &settings,
                                   Visibility visibility, Retire retire)
{
    if (requester == nullptr || this->device_owner != nullptr || !visibility || !retire
        || this->shutting_down || this->retiring_clients || !this->clients.empty()
        || this->last_generation == std::numeric_limits<Generation>::max())
        return false;
    if (!this->initialize_api(settings))
        return false;

    this->native_targets.reset();
    this->frame_coordinator.reset();
    // A successful bgfx::init() begins a new generation. Never reuse an
    // identity token after a normal device recreation.
    this->active_generation = ++this->last_generation;
    this->device_owner = requester;
    this->owner_visibility = std::move(visibility);
    this->owner_retire = std::move(retire);
    return true;
}

bool BgfxDeviceManager::registerClient(const void *client, Generation generation, Retire retire)
{
    if (client == nullptr || client == this->device_owner || !retire
        || generation == 0 || generation != this->active_generation
        || this->shutting_down || this->retiring_clients || !this->hasOwner())
        return false;
    for (const Client &entry : this->clients)
    {
        if (entry.identity == client)
            return false;
    }
    this->clients.push_back({client, generation, std::move(retire)});
    return true;
}

bool BgfxDeviceManager::unregisterClient(const void *client, Generation generation)
{
    if (generation == 0 || generation != this->active_generation
        || this->frame_coordinator.views(client).valid()
        || this->native_targets.contains(client))
        return false;
    for (std::vector<Client>::iterator it = this->clients.begin();
         it != this->clients.end(); ++it)
    {
        if (it->identity == client && it->generation == generation)
        {
            this->clients.erase(it);
            return true;
        }
    }
    return false;
}

bool BgfxDeviceManager::isCurrent(const void *client, Generation generation) const
{
    if (client == nullptr || generation == 0 || generation != this->active_generation)
        return false;
    if (client == this->device_owner)
        return true;
    for (const Client &entry : this->clients)
    {
        if (entry.identity == client && entry.generation == generation)
            return true;
    }
    return false;
}

bool BgfxDeviceManager::retireClients(const void *requester)
{
    if (!owns(requester) || this->shutting_down || this->retiring_clients)
        return false;
    this->retiring_clients = true;
    // Callbacks can unregister themselves, invalidating the vector element.
    // Copy each callback, retire newest-first, and verify it unregistered and
    // released BOTH its view and render target before proceeding.
    bool completed = true;
    while (!this->clients.empty())
    {
        const Client current = this->clients.back();
        try
        {
            current.retire();
        }
        catch (...)
        {
            completed = false;
            break;
        }
        if (this->isCurrent(current.identity, current.generation)
            || this->frame_coordinator.views(current.identity).valid()
            || this->native_targets.contains(current.identity))
        {
            completed = false;
            break;
        }
    }
    this->retiring_clients = false;
    // Also reject unmanaged reservations. This guard catches consumers that
    // bypassed registerClient(), rather than destroying their handles.
    return completed && this->frame_coordinator.onlyClient(requester)
        && this->native_targets.onlyClient(requester);
}

bool BgfxDeviceManager::shutdown(const void *requester)
{
    if (!owns(requester) || this->shutting_down || !retireClients(requester))
        return false;
    this->shutting_down = true;
    // All secondary resources and callbacks are gone before the default
    // swapchain, view reservations and GPU device are destroyed.
    this->native_targets.reset();
    this->frame_coordinator.reset();
    this->shutdown_api();
    this->device_owner = nullptr;
    this->active_generation = 0;
    this->owner_visibility = {};
    this->owner_retire = {};
    this->shutting_down = false;
    return true;
}

bool BgfxDeviceManager::owns(const void *requester) const
{
    return requester != nullptr && this->device_owner == requester;
}

bool BgfxDeviceManager::hasOwner() const
{
    return this->device_owner != nullptr;
}
