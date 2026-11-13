#ifndef MAP_BGFX_MAP_VIEW_H
#define MAP_BGFX_MAP_VIEW_H

#include "map/bgfx/bgfx_native_surface.h"
#include "map/bgfx/map_bgfx_2d_scene_variants.h"
#include "map/bgfx/map_bgfx_three_view_camera.h"
#include "map/bgfx/map_bgfx_2d_gpu.h"
#include "map/bgfx/map_bgfx_2d_imagery_gpu.h"
#include "map/bgfx/map_bgfx_2d_symbology.h"
#include "map/bgfx/map_bgfx_globe_network.h"
#include "map/bgfx/map_bgfx_globe_icon_index.h"
#include "map/bgfx/map_bgfx_globe_height_cache.h"
#include "map/bgfx/map_bgfx_globe_surface.h"
#include "map/bgfx/map_bgfx_hud.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_gpu.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"
#include "map/bgfx/map_bgfx_label_gpu.h"
#include "map/bgfx/map_bgfx_labels.h"
#include "map/bgfx/map_bgfx_pan_reuse.h"
#include "map/bgfx/map_bgfx_panorama_gpu.h"
#include "map/core/map_frame_pacer.h"
#include "map/render/map_camera_view_state.h"
#include "map/render/map_globe_camera.h"
#include "map/render/map_globe_terrain_navigation.h"
#include "map/render/map_network_render_symbology.h"
#include "map/render/map_render_types.h"
#include "map/render/map_retained_network_scene.h"

#include <bgfx/bgfx.h>

#include <QElapsedTimer>
#include <QEvent>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPalette>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QTimer>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#ifdef Q_OS_WASM
#include <emscripten/html5.h>
#endif

class MapModel;
class QKeyEvent;
class QScreen;
class QMouseEvent;
class QWheelEvent;
class QWindow;

// What the map view needs from the toolkit that hosts it (a Qt Widgets
// surface on desktop and WASM, a Qt Quick item on mobile). The view owns the
// native QWindow it renders into (desktop); the host embeds it, decides
// visibility and keyboard focus, and supplies font and palette. WASM renders
// into a browser canvas laid over the host's area instead.
class MapBgfxMapViewHost
{
public:
    virtual ~MapBgfxMapViewHost() = default;

    // The host's area is shown (a hidden page/tab pauses rendering).
    virtual bool mapViewVisible() const = 0;
    // Hosts that supply their own map controls can omit the bgfx HUD.
    virtual bool mapViewHudEnabled() const { return true; }
    // HUD font and theme colours (window background, HUD palette).
    virtual QFont mapViewFont() const = 0;
    virtual QPalette mapViewPalette() const = 0;
    // The host may override the application direction (for example a QWidget).
    // Default keeps older and HUD-free Qt Quick hosts source-compatible.
    virtual Qt::LayoutDirection mapViewLayoutDirection() const { return Qt::LeftToRight; }
    // Gives the map keyboard focus (a click on the native window); the host
    // activates its window and its key receiver.
    virtual void mapViewFocusNavigationInput(Qt::FocusReason reason) = 0;
    // Without a native window of the view's own (WASM canvas, host
    // context): the host area's logical size, device pixel ratio, screen and
    // global position (WASM), and the pointer shape over it
    // (Qt::ArrowCursor resets it).
    virtual QSize mapViewLogicalSize() const = 0;
    virtual qreal mapViewDevicePixelRatio() const = 0;
    virtual QScreen *mapViewScreen() const = 0;
    virtual QPoint mapViewGlobalPosition() const = 0;
    virtual void mapViewSetCursor(Qt::CursorShape shape) = 0;
};

// The map renderer (Map Monitor and Map Editor), with continuous 2D
// projection and zoom-independent CPU staging: GPU network, imagery, heatmap,
// 2D picking
// (click selection and hover), selection/error highlights and the native HUD
// (crosshair, GPS, scale, downloads, view switch, fullscreen, map legend) and
// map labels (entity IDs / values, map_bgfx_labels.h).
// The same persistent surface renders Distribution Monitor and Editor. The
// workspace selects which mode supplies input handlers, symbology, overlays,
// and HUD callbacks; changing modes never changes the native window.
// bgfx remains process-global: a genuinely lost/recreated native surface may
// still require device reinitialization, but mode switches must not.
// Toolkit-neutral: the host (MapBgfxMapViewHost) forwards its show, hide,
// resize, change, key and focus events to the handle*() methods and injects
// its settings and assets (setNativeSurfaceCapture() ... setHudImages()).
class MapBgfxMapView final : public QObject
{
    Q_OBJECT
public:
    // Where the view renders.
    // - NativeWindow: into its own QWindow (desktop; the host embeds
    //   nativeInputWindow()) or the browser canvas (WASM), with its own
    //   device, swapchain and frame loop.
    // - HostContext: inside a graphics context the host owns and presents,
    //   e.g. Qt Quick's OpenGL / OpenGL ES scene graph: no window, no swap,
    //   no frame loop. The host calls renderHostFrame() from its render pass
    //   (context current, on the view's thread: Qt Quick's basic render
    //   loop) and schedules a pass on hostFrameRequested().
    enum class RenderTarget
    {
        NativeWindow,
        HostContext
    };
    MapBgfxMapView(MapModel *model, MapBgfxMapViewHost *view_host,
                   RenderTarget target = RenderTarget::NativeWindow,
                   QObject *parent = nullptr);
    ~MapBgfxMapView() override;
    RenderTarget renderTarget() const { return this->render_target; }

    // --- Host context ---------------------------------------------------------------
    // The host's current native context (EGLContext / GLXContext / ...) and
    // its API. bgfx is initialized in it on the first frame.
    struct HostContext
    {
        void *native_context = nullptr;
        bool opengl_es = false;
    };
    // Draws one complete frame into the host's bound framebuffer (the host
    // fenced its own state, e.g. QQuickWindow::beginExternalCommands()).
    // False when nothing was drawn (hidden, failed, device busy).
    bool renderHostFrame(const HostContext &context);
    // The host's context is about to go away (scene graph invalidated):
    // releases every GPU object and bgfx itself, with that context current.
    // The next renderHostFrame() initializes again.
    void releaseHostContext();

    // --- Host integration ---------------------------------------------------------
    // Native handle discovery for a QWindow (desktop). Default:
    // BgfxQtNativeSurface::capture() (AOWIS::RenderQtSurface); a host
    // overrides it only for a toolkit or platform that adapter lacks.
    using NativeSurfaceCapture = std::function<BgfxNativeSurface(QWindow *window)>;
    void setNativeSurfaceCapture(NativeSurfaceCapture capture);
    // The user's terrain detail setting, read live by the globe's surface
    // preparation and the terrain navigation.
    void setTerrainDetailReductionSource(std::function<int()> source);
    // Tank and reservoir model textures (host assets).
    void setGlobeModelImages(const QImage &tank_albedo, const QImage &reservoir_albedo);
    // HUD assets and host settings: button images, the navigation keys shown
    // in tooltips, the scale bar in the user's distance unit, and the globe
    // HUD's slider/compass state in the user's units.
    void setHudImages(const QImage &crosshair, const QImage &fullscreen,
                      const QImage &fullscreen_exit, const QImage &zoom_in,
                      const QImage &zoom_out);
    void setHudNavigationKeys(const MapBgfxHud::NavigationKeys &keys);
    void setHudScaleProvider(MapBgfxHud::ScaleProvider provider);
    using GlobeHudStateProvider = std::function<void(const MapModel &model, const QSize &viewport,
                                                     MapBgfxHud::Globe *globe)>;
    void setGlobeHudStateProvider(GlobeHudStateProvider provider);
    // The host's events. Key handlers return true when consumed.
    void handleHostShown();
    void handleHostHidden();
    void handleHostResized();
    // Palette, font and language changes restyle the HUD: present a frame.
    void handleHostChange(QEvent::Type type);
    bool handleKeyPress(QKeyEvent *event);
    bool handleKeyRelease(QKeyEvent *event);
    void handleFocusOut();
    // false: clicks do not activate the native window (the host keeps
    // keyboard focus on another receiver, e.g. the editor canvas).
    void setNativeWindowAcceptsFocus(bool accepts);

    QString graphicsApiName() const;
    // Picking with the QRhi rules at the model's current camera: 2D
    // (map_bgfx_2d_picking.h) and the globe (MapBgfxGlobeNetwork::hitTest()).
    MapRenderHit hitTest(const QPointF &screen_position) const;
    // Terrain-aware navigation (Phase 3, slice 2; MapGlobeTerrainNavigation):
    // pointer coordinates with DEM altitude (2D: cached DEM only), rays
    // against the drawn terrain meshes, terrain-anchored globe panning. The
    // camera-height follow runs inside this surface.
    bool terrainCoordinateAtScreen(const QPointF &screen_position, CoordinateWGS84 *coordinate,
                                   bool request_missing_tile = true);
    // The same drawn-terrain projection used for the last globe overlay
    // frame. No independent 3D editing camera or terrain calculation.
    bool globeCoordinateToScreen(const CoordinateWGS84 &coordinate,
                                 QPointF *screen_position) const;
    bool cachedTerrainElevationAtCoordinate(const CoordinateWGS84 &coordinate,
                                            double *elevation_m) const;
    bool globeSurfaceRayHitAtScreen(const QPointF &screen_position,
                                    MapGlobeSurfaceHit *hit) const;
    // The drawn globe surface (terrain mesh, else ellipsoid) under a screen
    // position, with the camera as the model has it now: the crosshair's
    // sets the sphere a pointer pan turns the globe on.
    bool globeSurfacePointAtScreen(const QPointF &screen_position,
                                   GeoWgs84Ellipsoid::EcefPositionD *point);

    // Keyboard events originate from the embedded QWindow on native desktop.
    // Forward them to the shared map-navigation owner; bgfx must not implement
    // a second camera controller or independently quantize zoom input.
    using NavigationKeyHandler = std::function<bool(QKeyEvent *)>;
    using NavigationWheelHandler = std::function<void(QWheelEvent *)>;
    using NavigationFrameHandler = std::function<void(double)>;
    void setNavigationHandlers(NavigationKeyHandler press,
                               NavigationKeyHandler release,
                               std::function<void()> cancel);
    // Wheel events on the embedded native window (desktop) go to the same
    // shared navigation owner as keys; they share one zoom motion.
    void setNavigationWheelHandler(NavigationWheelHandler wheel);
    // Called once per presented frame with the paced elapsed time, *before*
    // the camera is sampled. This surface is then the only navigation clock.
    void setNavigationFrameHandler(NavigationFrameHandler advance);
    // Look-around panorama test mode (map_bgfx_panorama_grid.h): while a
    // globe eye pose is set, this equirectangular 360-degree image (u = 0.5
    // facing north) is drawn instead of the map, the network, overlays and
    // the HUD, through the same camera, frame pacing and presentation. Only
    // the map's rendering work is left out, so it tells a slow map apart
    // from uneven motion. A null image ends it.
    void setPanorama(const QImage &equirectangular);
    bool panoramaShown() const;
    // Mouse press/release/move on the embedded native window (desktop).
    // Returns true when consumed, like MapWidget::handleMouse*Event().
    using NavigationMouseHandler = std::function<bool(QMouseEvent *)>;
    void setNavigationMouseHandlers(NavigationMouseHandler press,
                                    NavigationMouseHandler release,
                                    NavigationMouseHandler move);
    // The window that actually has pointer focus over the map (desktop), so
    // a Wayland pointer lock can target its own wl_surface; the host embeds
    // it. nullptr on WASM.
    QWindow *nativeInputWindow() const;
    // One line on where startup stands (initialization, frame loop, frames),
    // for the startup watchdog and failure messages.
    QString startupDiagnostics() const;

    void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot);
    // Hide distribution network/heatmaps/labels/picking in drainage modes.
    // Retains CPU/GPU geometry, imagery, terrain and navigation unchanged.
    void setDistributionNetworkVisible(bool visible);
    bool distributionNetworkVisible() const { return this->distribution_network_visible; }
    // Editor move preview: these entities are drawn transparent (colour
    // stream only, no geometry upload) while the overlay draws them moving.
    void setHiddenEntityUuids(const QSet<QUuid> &uuids);
    void setNodeDeclutteringEnabled(bool enabled);
    // Editor full-network move: the network (not the imagery) is drawn
    // shifted by this many logical pixels (camera offset, no upload).
    void setNetworkScreenTranslation(const QPointF &translation);
    void setSymbology(const MapNetworkRenderSymbology &symbology);
    void setVisualControlSettings(const NetworkSymbologySettings &settings);
    void setTileRepository(MapTileRepository *tile_repository);
    void setTerrainRepository(MapTerrainRepository *terrain_repository);
    void setBackgroundOpacity(int opacity);
    void setSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid);
    void setSimulationErrorEntities(const QHash<QUuid, InfrastructureEntity> &error_entities,
                                    const QSet<QUuid> &stale_entity_uuids);
    // Globe network (slice 3): tank/reservoir as 3D models instead of icons;
    // underground pipes Solid (through the terrain), X-Ray (buried runs
    // dashed through the terrain) or Hide.
    void setGlobe3dIconsEnabled(bool enabled);
    // Full-colour overlay icons on the globe replace only its generic GPU
    // atlas symbols; the 2D monitor symbology is unchanged.
    void setGlobeFlatEntityIconsEnabled(bool enabled);
    void setUndergroundMode(MapUndergroundMode mode);
    MapUndergroundMode undergroundMode() const { return this->underground_mode; }
    void setTerrainWireframeVisible(bool visible);
    void setMapTilesVisible(bool visible);
    // Current state of the globe HUD's "wireframe" / "map" toggles.
    bool terrainWireframeVisible() const;
    // The globe HUD's "hillshade" toggle.
    void setTerrainHillshadeVisible(bool visible);
    bool terrainHillshadeVisible() const;
    bool mapTilesVisible() const;

    // --- Globe on bgfx (roadmap Phase 3) ------------------------------------------
    // Terrain surface (imagery, DEM relief, caps, wireframe),
    // terrain-aware navigation, the network and its heatmap, picking and
    // the globe HUD (compass, camera sliders, underground mode, toggles,
    // terrain-mesh progress, scale).
    // The globe surface initialized on this device (the HUD offers 3D).
    bool globeAvailable() const { return this->globe_ready; }
    // Surfaces that never show the globe (the editor, 2D only) skip its GPU
    // setup and offer no 3D. Call before the surface is first shown.
    void setGlobeOffered(bool offered) { this->globe_offered = offered; }

    // Planned native-window changes (reparenting, desktop; map fullscreen
    // no longer reparents, MainWindow::setMapFullscreen()): shut
    // bgfx down while the old native surface still exists, restart once the
    // window is exposed again. Qt Wayland destroys the wl_surface on reparent
    // without a QPlatformSurfaceEvent, so this must not rely on events.
    void suspendForNativeWindowChange();
    void resumeAfterNativeWindowChange();

    // --- Native HUD (slices 6a, 6b) ---------------------------------------------
    // Called (queued, outside the native window's event dispatch) when a HUD
    // button or legend entry is clicked.
    void setHudActionHandler(std::function<void(const MapBgfxHud::Command &)> handler);
    void setHudGpsPosition(bool valid, double longitude_deg, double latitude_deg);
    void setHudDownloadActivity(int tiles_active, int tiles_queued,
                                int terrain_active, int terrain_queued);
    void setHudFullscreen(bool active);
    // The pan joystick's knob (MapWidget::joystickChanged()).
    void setHudJoystick(const QPointF &deflection, bool active);
    // Held keyboard pan keys (MapWidget::keyboardPanChanged()): the
    // joystick's arrows glow.
    void setHudKeyboardPan(const QPointF &direction);
    // Map legend content (map_bgfx_legend_content.h); visible=false hides it.
    void setHudLegend(const MapBgfxHud::Legend &legend);
    // Closes an open HUD popup (combo list, palette menu).
    void closeHudPopup();
    // The 2D/3D switch (shown by default; 3D is offered once the bgfx globe
    // exists).
    void setHudViewSwitchVisible(bool visible);

    // --- Map labels (map_label_settings.h) ----------------------------------------
    // Which labels are drawn; all off (default) costs nothing.
    void setLabelSettings(const MapLabelSettings &settings);
    // Canonical -> displayed value of the node / link colouring metric
    // (mapBgfxLabelValueFormat()); the values come with setSymbology().
    void setLabelValueFormats(const MapBgfxLabelValueFormat &node_format,
                              const MapBgfxLabelValueFormat &link_format);
    // Symbol boxes the labels are placed around and keep clear of, relative
    // to the anchor (logical px). Unset: the GPU network's own symbols (dots,
    // icons at the symbology's sizes). The editor sets its icon boxes (its
    // icons are drawn by the overlay, the GPU network only has dots).
    void setLabelSymbolRects(MapBgfxLabels::SymbolRect node_symbol,
                             MapBgfxLabels::SymbolRect link_symbol);

    // --- On-demand rendering ------------------------------------------------------
    // The surface presents a frame only when its camera or size changed, one
    // of its setters was called, or something animates (HUD busy bars or
    // tooltip delay, budgeted imagery uploads, deferred labels). Anything
    // else that changes what it draws - the editor overlay's content - calls
    // this.
    void requestRedraw();

    // --- Map editor (Phase 2) ---------------------------------------------------
    // Called every 2D frame after the network and before the HUD with the
    // frame's continuous camera; draws into its own canvas and atlas, which
    // the surface uploads and submits with the HUD program. Returns the
    // translation (logical px) to draw the canvas with: a builder may keep
    // the canvas of an earlier frame and only move it (pure pans,
    // map_bgfx_pan_reuse.h); QPointF() for a canvas built for this camera.
    using OverlayBuilder = std::function<QPointF(MapBgfxHudCanvas *canvas,
                                              MapBgfxHudTextCache *cache,
                                              const MapBgfx2d::Camera &camera,
                                              const QSizeF &viewport)>;
    void setOverlayBuilder(OverlayBuilder builder);
    // Globe mode: where a WGS84 coordinate on the drawn surface (terrain
    // relief where it is drawn, else the ellipsoid) lies on screen this
    // frame, in logical px. False behind the camera or beyond the horizon
    // (relief does not occlude: the overlay is drawn on top of the globe).
    // Optional distance_m returns eye-to-draped-anchor distance for correct
    // painter ordering of transparent icons in the 3D editor overlay.
    using GlobeProjector = std::function<bool(double longitude_deg, double latitude_deg,
                                              QPointF *screen_position, double *distance_m)>;
    // Network elevations are hydraulic metres, transformed into the same
    // globe-space height used by the retained 3D network (including its lift).
    // The surface projector above remains terrain-draped for map selections,
    // demand points, reservoirs and intermediate pipe bends.
    using GlobeElevatedProjector = std::function<bool(double longitude_deg, double latitude_deg,
                                                      double elevation_m,
                                                      QPointF *screen_position,
                                                      double *distance_m)>;
    // Called every globe frame after the terrain and the network and before
    // the HUD; draws into the overlay's canvas and atlas (the 2D builder's,
    // the two never run in the same frame), submitted untranslated.
    using GlobeOverlayBuilder = std::function<void(MapBgfxHudCanvas *canvas,
                                                   MapBgfxHudTextCache *cache,
                                                   const GlobeProjector &projector,
                                                   const GlobeElevatedProjector &elevated_projector,
                                                   const QSizeF &viewport,
                                                   const MapBgfxGlobeIconIndex::Frustum &frustum)>;
    void setGlobeOverlayBuilder(GlobeOverlayBuilder builder);
    // Routes a mouse event (logical px of the map) to the HUD first.
    // consumed: the event belongs to the HUD and must not reach navigation or
    // picking. cursor: the HUD element's pointer shape (Qt::ArrowCursor when
    // the HUD has no opinion). over_panel: hovering a blocking HUD panel or
    // popup (no map hover there).
    struct HudPointer
    {
        bool consumed = false;
        Qt::CursorShape cursor = Qt::ArrowCursor;
        bool over_panel = false;
    };
    HudPointer handleHudMouseEvent(QMouseEvent *event);
    // Wheel over a HUD panel scrolls an open combo list and never zooms the
    // map underneath; true when consumed. On desktop the native window's
    // wheel events are routed here internally; WASM routes MapWidget's.
    bool handleHudWheelEvent(QWheelEvent *event);
    // Escape closes an open HUD popup; true when consumed (same routing).
    bool handleHudKeyPress(QKeyEvent *event);
    // Map pointer shape: pointing hand over pickable entities and HUD
    // buttons, cross over legend ramps, Qt::ArrowCursor resets it. On
    // desktop the cursor must be set on the embedded native window; the
    // widget's own cursor is never visible there.
    void setMapCursor(Qt::CursorShape shape);

signals:
    // Host context: the view wants a render pass (QQuickWindow::update()).
    void hostFrameRequested();
    void signalRendererReady();
    void signalRendererFailed(const QString &reason);
    // Host-produced HUD strings (legend, units, tooltips) need regeneration
    // after Qt installs/removes a translator. No device or camera reset.
    void localizedPresentationRefreshRequested();

protected:
    // The native window's (and the probe windows') events.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
#ifndef Q_OS_WASM
    BgfxNativeSurface captureNativeSurface(QWindow *window) const;
#endif
    bool initializeBackend();
    // After bgfx::init(): shader check, views, default target, GPU parts.
    bool adoptInitializedDevice();
    bool initializeHostContext(const HostContext &context);
#ifndef Q_OS_WASM
    void createNativeWindow();
    // true when a focus/activation loss is only the map's own click focus
    // routing (see the definition); navigation must then not be cancelled.
    bool focusLossIsInternalPointerRouting() const;
#endif
    // The view's own native window, else the host's area.
    QSize viewLogicalPixels() const;
    qreal viewDevicePixelRatio() const;
    QScreen *viewScreen() const;
    void startFrameLoop();
    void stopFrameLoop();
    double refreshPeriodSeconds() const;
    void renderFrame();
    void renderFrameAt(double elapsed_seconds);
    // 2D imagery: layout from the continuous camera, budgeted uploads,
    // repository requests on tile-set changes, then draw.
    void renderImagery(const QSizeF &logical, bool camera_moving);
    void synchronizeGeometry();
    // Rebuilds the selection/error highlight meshes when the selection, the
    // errors, the prepared geometry or the junction visibility changed.
    void synchronizeHighlights();
    // Builds the HUD, uploads its atlas changes and submits it on top.
    void renderHud(const QSizeF &logical, const MapBgfx2d::Camera &camera);
    // Hands the HUD's continuous-interaction commands (globe sliders,
    // compass) to the action handler, deferred like the button actions.
    void dispatchHudCommands();
    // bgfx's text statistics overlay (AOWIS_BGFX_DEBUG=1).
    void renderDebugOverlay();
    // Releases every bgfx object owned by this surface (before
    // bgfx::shutdown()) and marks all GPU state for re-creation.
    void releaseGpuResources();
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
    // Opt-in native secondary-window probe. Uses one bgfx device and frame;
    // this is deliberately NOT an independent map editor or application tab.
    void startMultiViewProbe();
    void tryAttachMultiViewProbe();
    void detachMultiViewProbe();
    void stopMultiViewProbe();
    void drawMultiViewProbe();
    // Step 5 diagnostic: two additional, independently navigable 2D maps
    // backed by the existing GPU network scene and imagery cache.
    void startThreeViewProbe();
    void attachThreeViewProbe(int index);
    void detachThreeViewProbe(int index);
    void stopThreeViewProbe();
    void drawThreeViewProbe(int index);
#endif
    // bgfx::shutdown() for a native window that is about to be destroyed;
    // the backend restarts once the platform surface exists again.
    void shutdownBackend();
    void scheduleBackendRestart();
    // First initialization after the first show (retried while another
    // visible surface still holds the device).
    void beginInitialization();
    // Draws the editor overlay (when a builder is set) for this frame.
    void renderOverlay(const QSizeF &logical, const MapBgfx2d::Camera &camera);
    // Globe mode: terrain surface with the frame's camera (slice 1).
    void renderGlobe(const QSizeF &logical, bool camera_moving);
    void renderPanorama(const QSizeF &logical);
    // Draws the globe overlay (when a builder is set) after renderGlobe().
    void renderGlobeOverlay(const QSizeF &logical);
    // The globe's viewport: logical pixels of the drawn surface, rounded.
    QSize globeViewportSize() const;
    // One camera-height step of the terrain follow while the bgfx globe is
    // shown (slice 2); see MapGlobeTerrainNavigation::syncCameraHeight().
    void syncGlobeCameraHeight(bool request_missing_tile);
    // The symbology for the globe network: the icon fill themed like the
    // QRhi monitor (light theme: soft pink).
    MapNetworkRenderSymbology globeSymbology() const;
    // Brings entity_style up to date (classes, hidden / selected /
    // diagnostic entities) and uploads what changed; before any network draw.
    void synchronizeEntityStyle();
    // The globe heatmap's markers (node positions, ramp colours of the
    // heatmap metric), radius and solid centre from the symbology (as the
    // former QRhi globe did): a pixel radius is converted
    // at the current orbit distance when the heatmap data or style change.
    void syncGlobeHeatmap();
    // X-Ray classification against the cached DEM, 500 ms after the last
    // camera change or terrain arrival and only once no terrain mesh is
    // being built (as the QRhi globe): never on the pan/zoom hot path.
    void scheduleGlobeUndergroundRefresh();
    // Map labels with the network's camera (the editor's full-network move
    // included); rebuilt only when the camera or the content changed.
    void renderLabels(const QSizeF &logical, const MapBgfx2d::Camera &network_camera,
                      const MapBgfx2dSymbology::Sizes &sizes);
    QSize physicalSize() const;
    QSizeF logicalSize() const;
    void fail(const QString &reason);
#ifdef Q_OS_WASM
    static EM_BOOL animationFrameCallback(double time_ms, void *user_data);
#endif

    NavigationKeyHandler navigation_key_press;
    NavigationKeyHandler navigation_key_release;
    std::function<void()> navigation_cancel;
    NavigationWheelHandler navigation_wheel;
    NavigationFrameHandler navigation_frame;
    NavigationMouseHandler navigation_mouse_press;
    NavigationMouseHandler navigation_mouse_release;
    NavigationMouseHandler navigation_mouse_move;
    RenderTarget render_target = RenderTarget::NativeWindow;
    MapBgfxMapViewHost *host = nullptr;
    MapModel *map_model = nullptr;
    NativeSurfaceCapture native_surface_capture;
    GlobeHudStateProvider globe_hud_state_provider;
    MapRetainedNetworkScene retained_scene;
    bool distribution_network_visible = true;
    MapBgfx2dSceneVariants prepared_2d_scenes;
    MapBgfx2dGpu gpu_2d_scene;
    MapBgfx2dGpu::FrameStats network_frame_stats;
    // Resolved values and sizes; the GPU colours the network from them.
    MapNetworkRenderSymbology symbology;
    // Per-entity GPU colouring shared by the 2D map and the globe: values,
    // visibility, selection and diagnostics as data textures.
    MapBgfxEntityStyle entity_style;
    // Entity classes (geometry) / hidden, selected, diagnostic entities out
    // of date in entity_style.
    bool entity_classes_dirty = true;
    bool entity_state_dirty = true;
    // Entity identity for picking and highlights (render ids are only unique
    // per entity type, see MapBgfx2dScene::entityKey()).
    QHash<quint64, QUuid> uuid_by_entity_key;
    QHash<QUuid, quint64> entity_key_by_uuid;
    InfrastructureEntity selected_entity_type = InfrastructureEntity::Unknown;
    QUuid selected_entity_uuid;
    QHash<QUuid, InfrastructureEntity> simulation_error_entities;
    QSet<QUuid> simulation_stale_entity_uuids;
    bool highlights_dirty = true;
    quint64 highlight_preparation_id = 0;
    bool highlight_show_junctions = true;
    MapBgfx2dImageryGpu imagery_gpu;
    MapTileRepository *tile_repository = nullptr;
    // Globe (Phase 3): camera (sticky render origin) and terrain surface;
    // the surface shares imagery_gpu's atlas with the 2D map.
    MapGlobeCamera globe_camera;
    std::unique_ptr<MapBgfxGlobeSurface> globe_surface;
    // Panorama test mode (setPanorama()).
    MapBgfxPanoramaGpu panorama_gpu;
    QImage panorama_image;
    bool panorama_upload_pending = false;
    std::vector<MapPanoramaVertex> panorama_vertices;
    MapTerrainRepository *terrain_repository = nullptr;
    // Terrain-aware navigation over globe_surface's retained DEM meshes.
    MapGlobeTerrainNavigation terrain_navigation;
    // Cache stable drawn-terrain heights, never screen-space icon positions.
    MapBgfxGlobeHeightCache globe_overlay_height_cache;
    // The network on the globe (slice 3); its inputs are applied lazily on
    // globe frames. Ready: its programs exist on this device. Mutable:
    // hitTest() (const) applies pending inputs before picking (slice 4).
    mutable MapBgfxGlobeNetwork globe_network;
    bool globe_network_ready = false;
    // X-Ray refresh wanted, and the generation of the pending quiet timer.
    bool globe_underground_refresh_requested = false;
    quint64 globe_underground_refresh_generation = 0;
    // A network arrived in Globe mode: the next fitted camera loads its
    // visible DEM tiles at once (one shot, like the QRhi globe).
    bool globe_terrain_prime_pending = false;
    bool globe_ready = false;
    bool globe_offered = true;
    // Imagery uploads or terrain meshes outstanding: keep presenting.
    bool globe_pending = false;
    // View mode of the last presented frame: a switch starts new tile
    // request batches for the mode now shown.
    MapViewMode presented_view_mode = MapViewMode::TwoD;
    quint64 imagery_layout_key = 0;
    quint64 imagery_request_batch = 0;
    bool imagery_requests_dirty = true;
    int imagery_zoom = 0;
    int imagery_quad_count = 0;
    int background_opacity = 0;
    MapCameraViewState camera_state;
    // Native HUD: state/layout (CPU), its text/image atlas, the per-frame
    // draw list and the bgfx objects.
    MapBgfxHud hud;
    MapBgfxHudTextCache hud_text_cache;
    MapBgfxHudCanvas hud_canvas;
    MapBgfxHudGpu hud_gpu;
    std::function<void(const MapBgfxHud::Command &)> hud_action_handler;
    QElapsedTimer hud_clock;
    // Editor overlay: builder, its atlas (icons with glows; larger than the
    // HUD's), draw list and bgfx objects.
    OverlayBuilder overlay_builder;
    GlobeOverlayBuilder globe_overlay_builder;
    // The projector owns copies of the last drawn camera values, never
    // references to renderGlobeOverlay() stack locals.
    GlobeProjector globe_interaction_projector;
    bool globe_flat_entity_icons_enabled = false;
    MapBgfxHudTextCache overlay_text_cache{2048};
    MapBgfxHudCanvas overlay_canvas;
    MapBgfxHudGpu overlay_gpu;
    // Map labels: settings, value formats, optional symbol boxes, the
    // builder, its glyph distance atlas (created on first use: 1 MiB) and
    // bgfx objects. The canvas is rebuilt only when label_build_key changes,
    // labels were deferred (glyph budget) or fade in, or a pan left
    // label_pan's margin.
    MapLabelSettings label_settings;
    MapBgfxLabelValueFormat label_node_format;
    MapBgfxLabelValueFormat label_link_format;
    MapBgfxLabels::SymbolRect label_node_symbol;
    MapBgfxLabels::SymbolRect label_link_symbol;
    MapBgfxLabels labels;
    std::unique_ptr<MapBgfxGlyphAtlas> label_glyphs;
    MapBgfxHudCanvas label_canvas;
    MapBgfxLabelGpu label_gpu;
    // Everything but the camera centre: pans reuse the canvas translated
    // (label_pan) while the key is unchanged.
    struct LabelBuildKey
    {
        double world_pixels = 0.0;
        double viewport_width = 0.0;
        double viewport_height = 0.0;
        double node_half_px = 0.0;
        double icon_size_px = 0.0;
        double device_pixel_ratio = 0.0;
        quint64 preparation_id = 0;
        quint64 atlas_generation = 0;
        bool operator==(const LabelBuildKey &other) const
        {
            return this->world_pixels == other.world_pixels
                && this->viewport_width == other.viewport_width
                && this->viewport_height == other.viewport_height
                && this->node_half_px == other.node_half_px
                && this->icon_size_px == other.icon_size_px
                && this->device_pixel_ratio == other.device_pixel_ratio
                && this->preparation_id == other.preparation_id
                && this->atlas_generation == other.atlas_generation;
        }
    };
    LabelBuildKey label_build_key;
    // Built camera / margin of label_canvas and its draw offset this frame.
    MapBgfxPanReuse label_pan;
    // Content changed (settings, values, symbols, hidden entities, font):
    // rebuild on the next frame.
    bool labels_dirty = true;
    // On-demand rendering (requestRedraw()): what the last presented frame
    // showed, and whether a frame is wanted regardless.
    bool redraw_requested = true;
    bool presented_valid = false;
    MapCameraViewState presented_camera;
    QSize presented_pixels;
    QSizeF presented_logical;
    // Tiles in the repository that did not fit into this frame's upload
    // budget: keep presenting until all are resident.
    bool imagery_uploads_pending = false;
    // The last pointer move involved the HUD (hover feedback to redraw).
    bool hud_pointer_involved = false;
    // Last setHudDownloadActivity() numbers.
    int hud_download_activity[4] = {0, 0, 0, 0};
    // Editor move preview.
    QSet<QUuid> hidden_entity_uuids;
    QPointF network_screen_translation;
    bool debug_overlay_enabled = false;
    // Desktop: one PreciseTimer slightly faster than the display; the
    // vsync-blocking bgfx::frame() then locks presentation to the refresh.
    // WASM: requestAnimationFrame (browser vsync), never a setTimeout timer.
    QTimer frame_timer;
    QElapsedTimer frame_clock;
    MapFramePacer frame_pacer;
    bool frame_loop_running = false;
    // The last frame's animation state (host context: ask for another pass).
    bool frame_animating = false;
    double last_frame_time_ms = -1.0;
    QSize last_physical_size;
    MapUndergroundMode underground_mode = MapUndergroundMode::XRay;
    bgfx::ViewId map_view_id = 0;
    bool initialized = false;
    bool initialization_attempted = false;
    bool failed = false;
    bool ready_reported = false;
    // The native window lost its platform surface while bgfx was running.
    bool reinitialize_on_surface = false;
    // initializeBackend() found the device held by another visible surface.
    bool device_busy = false;
    quint64 frame_count = 0;
    // renderFrameAt() calls, including skipped ones (startupDiagnostics()).
    quint64 frame_callback_count = 0;
    // Current initialization step, reported when startup stalls.
    const char *startup_step = "not started";
#ifndef Q_OS_WASM
    QWindow *native_window = nullptr;
#if defined(Q_OS_LINUX)
    QWindow *multi_view_probe_window = nullptr;
    bgfx::ViewId multi_view_probe_view_id = 0;
    std::uint64_t multi_view_probe_generation = 0;
    QSize multi_view_probe_pixels;
    bool multi_view_probe_attached = false;
    bool multi_view_probe_unavailable = false;
    bool multi_view_probe_resize_warning = false;
    struct ThreeViewProbe
    {
        QWindow *window = nullptr;
        bgfx::ViewId view_id = 0;
        std::uint64_t generation = 0;
        QSize pixels;
        MapBgfxThreeViewCamera camera;
        QPointF last_pointer;
        quint64 imagery_layout_key = 0;
        quint64 imagery_request_batch = 0;
        bool attached = false;
        bool unavailable = false;
        bool dragging = false;
    };
    std::array<ThreeViewProbe, 2> three_view_probes;
#endif
#endif
};

#endif // MAP_BGFX_MAP_VIEW_H
