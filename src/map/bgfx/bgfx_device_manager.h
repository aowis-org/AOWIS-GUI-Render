#ifndef AOWIS_BGFX_DEVICE_MANAGER_H
#define AOWIS_BGFX_DEVICE_MANAGER_H

#include <bgfx/bgfx.h>
#include "map/bgfx/bgfx_frame_coordinator.h"
#include "map/bgfx/bgfx_native_target_registry.h"

#include <cstdint>
#include <functional>
#include <vector>

// Process-wide bgfx device ownership. The current map UI still owns one
// visible device lease; a hidden map may relinquish it as before.
// This manager also owns the generation-scoped native target registry, which
// prepares for additional swapchains without activating a second map yet.
// All clients must share one bgfx submission thread.
class BgfxDeviceManager final
{
public:
    using Visibility = std::function<bool()>;
    using Retire = std::function<void()>;
    using Initialize = std::function<bool(const bgfx::Init &)>;
    using Shutdown = std::function<void()>;

    // Injectable API functions permit GPU-free ownership/lifecycle tests.
    explicit BgfxDeviceManager(Initialize initialize = {}, Shutdown shutdown = {},
                               BgfxNativeTargetRegistry::Create create_target = {},
                               BgfxNativeTargetRegistry::Bind bind_target = {},
                               BgfxNativeTargetRegistry::Destroy destroy_target = {});

    BgfxDeviceManager(const BgfxDeviceManager &) = delete;
    BgfxDeviceManager &operator=(const BgfxDeviceManager &) = delete;

    static BgfxDeviceManager &shared();

    // If another surface owns bgfx, reject takeover while it is visible;
    // otherwise ask it to release its GPU resources and the device first.
    bool prepareFor(const void *requester);
    bool initialize(const void *requester, const bgfx::Init &settings,
                    Visibility visibility, Retire retire);
    bool shutdown(const void *requester);

    // The primary initialize() lease is implicit. Independent GPU clients
    // must register an on-device retirement callback before allocating views,
    // swapchains or GPU objects. The token prevents callbacks from a previous
    // device generation from releasing resources on a recreated device.
    using Generation = std::uint64_t;
    Generation generation() const { return this->active_generation; }
    bool registerClient(const void *client, Generation generation, Retire retire);
    bool unregisterClient(const void *client, Generation generation);
    bool isCurrent(const void *client, Generation generation) const;

    // Retire all secondary GPU clients before the primary releases its own
    // resources. shutdown() also performs this barrier. If a client cannot
    // retire, the device remains alive; stale GPU handles are never discarded.
    bool retireClients(const void *requester);
    std::size_t secondaryClientCount() const { return this->clients.size(); }

    bool owns(const void *requester) const;
    bool hasOwner() const;
    BgfxNativeTargetRegistry &targets() { return this->native_targets; }
    const BgfxNativeTargetRegistry &targets() const { return this->native_targets; }
    BgfxFrameCoordinator &frames() { return this->frame_coordinator; }
    const BgfxFrameCoordinator &frames() const { return this->frame_coordinator; }

private:
    Initialize initialize_api;
    Shutdown shutdown_api;
    BgfxFrameCoordinator frame_coordinator;
    BgfxNativeTargetRegistry native_targets;
    struct Client
    {
        const void *identity = nullptr;
        Generation generation = 0;
        Retire retire;
    };
    std::vector<Client> clients;
    Generation active_generation = 0;
    Generation last_generation = 0;
    bool retiring_clients = false;
    bool shutting_down = false;
    const void *device_owner = nullptr;
    Visibility owner_visibility;
    Retire owner_retire;
};

#endif // AOWIS_BGFX_DEVICE_MANAGER_H
