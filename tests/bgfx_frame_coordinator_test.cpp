#include "map/bgfx/bgfx_frame_coordinator.h"

#include <cstdio>
#include <stdexcept>

namespace
{
int failures = 0;
void check(bool condition, const char *message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
}

int main()
{
    int frames = 0;
    int first = 1;
    int second = 2;
    int third = 3;
    int fourth = 4;
    BgfxFrameCoordinator coordinator([&frames]() { ++frames; }, 12);
    const BgfxFrameCoordinator::ViewRange first_views = coordinator.reserve(&first, 5);
    const BgfxFrameCoordinator::ViewRange second_views = coordinator.reserve(&second, 5);
    check(first_views.valid() && first_views.first == 0 && first_views.count == 5,
          "initial map reserves 0..4 (map plus four bake views)");
    check(second_views.valid() && second_views.first == 5 && second_views.count == 5,
          "second map reserves distinct 5..9");
    check(!coordinator.reserve(nullptr, 1).valid(), "null client refused");
    check(!coordinator.reserve(&first, 1).valid(), "same client may not double reserve");
    check(!coordinator.reserve(&third, 0).valid(), "zero views refused");
    check(!coordinator.reserve(&third, 3).valid(), "contiguous overflow refused");
    check(coordinator.views(&second).at(4) == 9, "relative bake view IDs valid");
    check(!coordinator.onlyClient(&first),
          "multi-client coordinator identifies foreign reservations");
    check(coordinator.reserve(&third, 2).first == 10, "remaining two view IDs allocated");
    check(!coordinator.reserve(&fourth, 1).valid(), "capacity fully reserved");
    check(!coordinator.present(&first) && frames == 0,
          "single-client presentation refuses multiple active clients");
    check(!coordinator.present(&fourth) && frames == 0,
          "unregistered client cannot submit a frame");
    // The default map keeps its view IDs while hidden. A Projects graph
    // submits its own queued draws without waking that map. This must work
    // even though more than one client is registered on the device.
    check(coordinator.renderAndPresent({{&second, {}}}) && frames == 1,
          "Projects view can submit while hidden map retains its reservation");
    check(!coordinator.renderAndPresent({{&fourth, {}}}) && frames == 1,
          "unregistered Projects client cannot submit pending GPU work");
    int draws = 0;
    check(coordinator.renderAndPresent({{&first, [&draws]() { ++draws; }},
                                        {&second, [&draws]() { ++draws; }}})
          && draws == 2 && frames == 2,
          "two independent clients draw before one shared global frame");
    check(!coordinator.renderAndPresent({{&first, [&draws]() { ++draws; }},
                                         {&first, [&draws]() { ++draws; }}})
          && draws == 2 && frames == 2,
          "duplicate client is rejected without drawing or framing");
    check(!coordinator.renderAndPresent({{&first, [&draws]() { ++draws; }},
                                         {&fourth, [&draws]() { ++draws; }}})
          && draws == 2 && frames == 2,
          "unknown client rejected before any draw is submitted");
    check(coordinator.release(&second), "second client releases range");
    check(!coordinator.present(&second), "released client cannot submit");
    check(coordinator.reserve(&fourth, 5).first == 5,
          "a released view range is recycled without changing other clients");
    check(coordinator.views(&first).first == 0 && coordinator.views(&third).first == 10,
          "other clients retained their original ranges");
    check(!coordinator.release(&second), "duplicate release rejected");
    coordinator.reset();
    check(!coordinator.views(&first).valid() && !coordinator.views(&fourth).valid(),
          "device reset invalidates all ranges");
    check(coordinator.reserve(&first, 5).first == 0, "first map reclaims view 0 after reset");
    check(coordinator.present(&first) && frames == 3,
          "single-map presentation still runs exactly one global frame");
    check(coordinator.onlyClient(&first),
          "single-owner coordinator has no outstanding foreign leases");

    BgfxFrameCoordinator *recursive = nullptr;
    bool nested_rejected = false;
    BgfxFrameCoordinator nested(
        [&recursive, &first, &nested_rejected]()
        {
            nested_rejected = !recursive->present(&first);
        }, 5);
    recursive = &nested;
    check(nested.reserve(&first, 5).valid(), "nested test range acquired");
    check(nested.present(&first) && nested_rejected, "recursive global frame rejected");

    int throws = 0;
    BgfxFrameCoordinator throwing([&throws]()
    {
        if (++throws == 1)
            throw std::runtime_error("test");
    }, 5);
    check(throwing.reserve(&first, 5).valid(), "throwing client range acquired");
    try { throwing.present(&first); }
    catch (const std::runtime_error &) {}
    check(throwing.present(&first) && throws == 2,
          "failed frame submission clears reentrancy flag");

    if (failures == 0)
        std::puts("bgfx view range / frame coordinator tests passed");
    return failures == 0 ? 0 : 1;
}
