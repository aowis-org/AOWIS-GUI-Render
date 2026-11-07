#include "map/bgfx/bgfx_qt_native_surface.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QString>
#include <QtMath>
#include <QWindow>
#include <QtGlobal>

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
#include <qpa/qplatformnativeinterface.h>
#endif

#include <algorithm>
#include <limits>

BgfxNativeSurface BgfxQtNativeSurface::capture(QWindow *window)
{
    BgfxNativeSurface result;
    if (window == nullptr)
        return result;

    const qreal ratio = window->devicePixelRatio();
    result.width = std::uint16_t(std::clamp(qRound(window->width() * ratio), 1,
                                                 int(std::numeric_limits<std::uint16_t>::max())));
    result.height = std::uint16_t(std::clamp(qRound(window->height() * ratio), 1,
                                                  int(std::numeric_limits<std::uint16_t>::max())));

    // Android defines Q_OS_LINUX too, but has no Wayland/XCB QPA: it takes
    // the plain winId() path (unverified; Mobile renders through Qt Quick's
    // EGL context instead).
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
    const QString platform = QGuiApplication::platformName();
    if (platform.startsWith(QStringLiteral("wayland")))
    {
#if defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
        QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
        if (native == nullptr)
            return {};
        result.display = native->nativeResourceForIntegration(QByteArrayLiteral("display"));
        result.window = native->nativeResourceForWindow(QByteArrayLiteral("surface"), window);
        result.handle_type = bgfx::NativeWindowHandleType::Wayland;
#else
        return {};
#endif
    }
    else if (platform == QStringLiteral("xcb"))
    {
#if defined(AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE)
        QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
        if (native == nullptr)
            return {};
        result.display = native->nativeResourceForIntegration(QByteArrayLiteral("display"));
        result.window = reinterpret_cast<void *>(window->winId());
#else
        return {};
#endif
    }
    else
        return {}; // Unsupported native Linux QPA backend (including offscreen).
#else
    result.window = reinterpret_cast<void *>(window->winId());
#endif

    return result.valid() ? result : BgfxNativeSurface{};
}
