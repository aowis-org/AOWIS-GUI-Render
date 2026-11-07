// BgfxFrameCoordinator::flush(): a host-context client's frame (Qt Quick
// presents, bgfx must not swap) goes through the flush submission, with the
// same single-client checks as present().
#include "map/bgfx/bgfx_frame_coordinator.h"

#include <cstdio>

namespace
{
int g_checks = 0;
int g_failures = 0;

void expectTrue(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}
}

int main()
{
    int presents = 0;
    int flushes = 0;
    BgfxFrameCoordinator frames([&presents]() { ++presents; });
    const int map = 0;
    const int other = 1;
    expectTrue(frames.reserve(&map, 2).valid(), "the map reserves its views");
    expectTrue(!frames.flush(&map), "no flush submission configured, no frame");
    frames.setFlushSubmit([&flushes]() { ++flushes; });
    expectTrue(frames.flush(&map), "a reserved single client flushes");
    expectTrue(flushes == 1 && presents == 0, "flush never presents");
    expectTrue(frames.present(&map), "present() is unchanged");
    expectTrue(flushes == 1 && presents == 1, "present() never flushes");
    expectTrue(!frames.flush(&other), "an unreserved client cannot flush");
    expectTrue(frames.reserve(&other, 1).valid(), "a second client reserves");
    expectTrue(!frames.flush(&map), "with two clients a single flush is refused");
    std::printf("bgfx_frame_coordinator_flush: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
