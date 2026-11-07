// BgfxQtNativeSurface without a display: nothing to capture for a missing
// window, and on Linux the offscreen QPA has no native handles, so no surface is
// reported valid (bgfx must never be initialized on such a handle).
#include "map/bgfx/bgfx_qt_native_surface.h"

#include <QGuiApplication>
#include <QWindow>

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

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);

    const BgfxNativeSurface missing = BgfxQtNativeSurface::capture(nullptr);
    expectTrue(!missing.valid() && missing.window == nullptr,
               "no window, no native surface");

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Linux captures only Wayland/XCB handles; elsewhere winId() is the handle.
    QWindow window;
    window.resize(320, 200);
    const BgfxNativeSurface hidden = BgfxQtNativeSurface::capture(&window);
    expectTrue(!hidden.valid(), "a window without native handles is never a valid surface");
    expectTrue(hidden.width == 0 && hidden.height == 0,
               "an invalid capture carries no stale dimensions");
#endif

    std::printf("bgfx_qt_native_surface: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
