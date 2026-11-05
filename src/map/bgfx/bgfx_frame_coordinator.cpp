#include "map/bgfx/bgfx_frame_coordinator.h"

#include <utility>

BgfxFrameCoordinator::BgfxFrameCoordinator(Submit submit, std::uint16_t capacity)
    : view_capacity(capacity), submit_api(std::move(submit))
{
}

BgfxFrameCoordinator::ViewRange BgfxFrameCoordinator::reserve(const void *client, std::uint16_t count)
{
    if (client == nullptr || count == 0 || count > this->view_capacity)
        return {};
    if (views(client).valid())
        return {};

    // First-fit contiguous allocation. Existing clients keep their IDs when
    // other views enter or leave; view 0 remains the first map's main view.
    for (std::uint32_t first = 0; first + count <= this->view_capacity; ++first)
    {
        bool available = true;
        for (const Reservation &reservation : this->reservations)
        {
            const std::uint32_t end = first + count;
            const std::uint32_t occupied_end = std::uint32_t(reservation.views.first)
                + reservation.views.count;
            if (first < occupied_end && end > reservation.views.first)
            {
                available = false;
                first = occupied_end - 1; // Next search starts just past it.
                break;
            }
        }
        if (!available)
            continue;
        const ViewRange range{std::uint16_t(first), count};
        this->reservations.push_back({client, range});
        return range;
    }
    return {};
}

bool BgfxFrameCoordinator::release(const void *client)
{
    for (std::vector<Reservation>::iterator it = this->reservations.begin();
         it != this->reservations.end(); ++it)
    {
        if (it->client == client)
        {
            this->reservations.erase(it);
            return true;
        }
    }
    return false;
}

BgfxFrameCoordinator::ViewRange BgfxFrameCoordinator::views(const void *client) const
{
    for (const Reservation &reservation : this->reservations)
    {
        if (reservation.client == client)
            return reservation.views;
    }
    return {};
}

bool BgfxFrameCoordinator::onlyClient(const void *client) const
{
    for (const Reservation &reservation : this->reservations)
    {
        if (reservation.client != client)
            return false;
    }
    return true;
}

bool BgfxFrameCoordinator::present(const void *client)
{
    // Preserve the current single-map path. When more clients are live,
    // callers MUST use renderAndPresent() so all draws precede one frame.
    if (this->reservations.size() != 1)
        return false;
    return renderAndPresent({RenderWork{client, {}}});
}

bool BgfxFrameCoordinator::renderAndPresent(const std::vector<RenderWork> &work)
{
    if (work.empty() || !this->submit_api || this->presenting)
        return false;
    // Preflight everything before executing any draw. Failed validation must
    // never produce a partially submitted frame.
    for (std::size_t index = 0; index < work.size(); ++index)
    {
        if (!views(work[index].client).valid())
            return false;
        for (std::size_t earlier = 0; earlier < index; ++earlier)
        {
            if (work[index].client == work[earlier].client)
                return false;
        }
    }
    this->presenting = true;
    try
    {
        for (const RenderWork &item : work)
        {
            if (item.draw)
                item.draw();
        }
        this->submit_api(); // Exactly one process-global frame for the batch.
    }
    catch (...)
    {
        this->presenting = false;
        throw;
    }
    this->presenting = false;
    return true;
}

void BgfxFrameCoordinator::reset()
{
    this->reservations.clear();
    this->presenting = false;
}
