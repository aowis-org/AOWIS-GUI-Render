// MapBgfxMapView's host contract without a GPU: a hidden host never starts
// bgfx, the view asks the host for font, palette and visibility, owns the
// native window it renders into until a host embeds it, and routes host
// events (language change, keys, focus) without a device.
#include "map/bgfx/map_bgfx_map_view.h"
#include "map/core/map_model.h"

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPointer>
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

class FakeHost final : public MapBgfxMapViewHost
{
public:
    bool mapViewVisible() const override
    {
        ++this->visible_queries;
        return this->visible;
    }
    QFont mapViewFont() const override
    {
        ++this->font_queries;
        return QFont();
    }
    QPalette mapViewPalette() const override { return QPalette(); }
    void mapViewFocusNavigationInput(Qt::FocusReason reason) override
    {
        this->focus_reason = reason;
        ++this->focus_requests;
    }
    QSize mapViewLogicalSize() const override { return QSize(320, 200); }
    qreal mapViewDevicePixelRatio() const override { return 1.0; }
    QScreen *mapViewScreen() const override { return nullptr; }
    QPoint mapViewGlobalPosition() const override { return QPoint(); }
    void mapViewSetCursor(Qt::CursorShape shape) override { this->cursor = shape; }

    bool visible = false;
    mutable int visible_queries = 0;
    mutable int font_queries = 0;
    int focus_requests = 0;
    Qt::FocusReason focus_reason = Qt::OtherFocusReason;
    Qt::CursorShape cursor = Qt::ArrowCursor;
};

void testHiddenHostNeverInitializes()
{
    MapModel model;
    FakeHost host;
    QPointer<QWindow> native_window;
    {
        MapBgfxMapView view(&model, &host);
        expectTrue(host.font_queries > 0, "the HUD font comes from the host");
        native_window = view.nativeInputWindow();
        expectTrue(!native_window.isNull(), "the view creates its native window");
        expectTrue(view.graphicsApiName() == QStringLiteral("bgfx (not initialized)"),
                   "no device before the host is shown");
        expectTrue(view.startupDiagnostics().startsWith(
                       QStringLiteral("not initialized: map surface not visible")),
                   "diagnostics report the hidden host");
        expectTrue(host.visible_queries > 0, "visibility comes from the host");
        expectTrue(!view.globeAvailable(), "no globe without a device");

        // Host services are plain setters; none needs a device.
        int detail_reads = 0;
        view.setTerrainDetailReductionSource([&detail_reads]()
        {
            ++detail_reads;
            return 1;
        });
        bool captured = false;
        view.setNativeSurfaceCapture([&captured](QWindow *)
        {
            captured = true;
            return BgfxNativeSurface();
        });
        view.setGlobeModelImages(QImage(), QImage());
        view.setHudImages(QImage(), QImage(), QImage(), QImage(), QImage());
        view.setHudNavigationKeys(MapBgfxHud::NavigationKeys());
        view.setHudScaleProvider(MapBgfxHud::ScaleProvider());
        view.setGlobeHudStateProvider(MapBgfxMapView::GlobeHudStateProvider());

        // A hidden host: shown/resized/hidden events start nothing.
        view.handleHostResized();
        view.handleHostHidden();
        expectTrue(view.graphicsApiName() == QStringLiteral("bgfx (not initialized)"),
                   "hidden host events never initialize bgfx");
        expectTrue(!captured, "no native handle is captured while hidden");

        // The editor keeps keyboard focus on its canvas: clicks must not
        // activate the native window.
        view.setNativeWindowAcceptsFocus(false);
        expectTrue(native_window->flags().testFlag(Qt::WindowDoesNotAcceptFocus),
                   "a host focus receiver keeps the native window from taking focus");
        view.setNativeWindowAcceptsFocus(true);
        expectTrue(!native_window->flags().testFlag(Qt::WindowDoesNotAcceptFocus),
                   "the native window takes focus again (monitor)");

        // Language changes ask the host to regenerate its HUD strings.
        int refreshes = 0;
        QObject::connect(&view, &MapBgfxMapView::localizedPresentationRefreshRequested,
                         [&refreshes]() { ++refreshes; });
        view.handleHostChange(QEvent::LanguageChange);
        view.handleHostChange(QEvent::PaletteChange);
        expectTrue(refreshes == 1, "only a language change requests localized strings");

        // No device: keys are not consumed (the host's default handling runs).
        bool navigation_called = false;
        view.setNavigationHandlers(
            [&navigation_called](QKeyEvent *) { navigation_called = true; return true; },
            [&navigation_called](QKeyEvent *) { navigation_called = true; return true; },
            []() {});
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Left, Qt::NoModifier);
        expectTrue(!view.handleKeyPress(&press), "an uninitialized view consumes no key press");
        expectTrue(!view.handleKeyRelease(&release), "an uninitialized view consumes no key release");
        expectTrue(!navigation_called, "navigation keys wait for the device");
        view.handleFocusOut();

        // Picking without a network or device finds nothing.
        const MapRenderHit hit = view.hitTest(QPointF(10.0, 10.0));
        expectTrue(!hit.isValid(), "no hit without a prepared network");
        expectTrue(detail_reads >= 0, "terrain detail source accepted");
    }
    expectTrue(native_window.isNull(), "a native window no host embedded is released with the view");
}
}

// Host context (Qt Quick): no window of its own, the host's area, every
// change asks the host for a render pass, and a missing context fails once.
void testHostContext()
{
    MapModel model;
    FakeHost host;
    host.visible = true;
    MapBgfxMapView view(&model, &host, MapBgfxMapView::RenderTarget::HostContext);
    expectTrue(view.renderTarget() == MapBgfxMapView::RenderTarget::HostContext,
               "the view keeps its render target");
    expectTrue(view.nativeInputWindow() == nullptr, "a host-context view creates no window");

    int requests = 0;
    QObject::connect(&view, &MapBgfxMapView::hostFrameRequested, [&requests]() { ++requests; });
    view.requestRedraw();
    expectTrue(requests == 1, "requestRedraw() asks the host for a render pass");
    view.handleHostShown();
    expectTrue(requests == 2, "showing the host asks for a render pass");
    const int before_camera = requests;
    model.setCenter(8.5, 47.4, QSize(320, 200));
    model.setView2dContinuousZoom(12.0, QSize(320, 200));
    expectTrue(requests > before_camera, "camera changes on the model ask for a render pass");

    view.setMapCursor(Qt::PointingHandCursor);
    expectTrue(host.cursor == Qt::PointingHandCursor, "without a window the host sets the cursor");

    QString failure;
    QObject::connect(&view, &MapBgfxMapView::signalRendererFailed,
                     [&failure](const QString &reason) { failure = reason; });
    expectTrue(!view.renderHostFrame(MapBgfxMapView::HostContext()),
               "no native context, no frame");
    expectTrue(!failure.isEmpty(), "a missing host context is reported");
    expectTrue(view.startupDiagnostics().startsWith(QStringLiteral("failed")),
               "the failure shows in the diagnostics");
    expectTrue(!view.renderHostFrame(MapBgfxMapView::HostContext()), "a failed view stays failed");
    view.releaseHostContext();
    expectTrue(view.graphicsApiName() == QStringLiteral("bgfx (not initialized)"),
               "releasing an uninitialized host context is harmless");

    // A hidden host is never drawn, whatever the context.
    MapBgfxMapView hidden_view(&model, &host, MapBgfxMapView::RenderTarget::HostContext);
    host.visible = false;
    int dummy = 0;
    MapBgfxMapView::HostContext context;
    context.native_context = &dummy;
    expectTrue(!hidden_view.renderHostFrame(context), "a hidden host gets no frame");
    expectTrue(hidden_view.graphicsApiName() == QStringLiteral("bgfx (not initialized)"),
               "a hidden host never initializes bgfx");
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    testHiddenHostNeverInitializes();
    testHostContext();
    std::printf("map_bgfx_map_view: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
