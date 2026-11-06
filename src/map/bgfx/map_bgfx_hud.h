#ifndef MAP_BGFX_HUD_H
#define MAP_BGFX_HUD_H

#include "map/bgfx/map_bgfx_2d_projection.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"

#include <QCoreApplication>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPalette>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <array>
#include <functional>

// Native bgfx HUD of the Map Monitor (Phase 1 slices 6a and 6b, globe
// controls Phase 3 slice 5).
//
// Replaces the QWidget HUD that cannot sit on top of bgfx's native window:
// crosshair, GPS position, scale bar, download activity (map tiles, terrain)
// with cancel buttons, the 2D/3D view switch with the fullscreen button (6a),
// the pan joystick top right with the zoom buttons under it and
// the map legend (6b: node / link / heatmap metric combos, colour ramps with
// value labels, hover value swatch, palette menu with flip and reset,
// tooltips). On the globe (Phase 3 slice 5, setGlobe()): the wireframe /
// map toggles next to the view switch, the terrain-mesh progress next to the
// downloads, the camera sliders (network height, tilt, distance, vertical
// exaggeration), the underground mode (X-Ray / Hide / Solid), the compass
// with its heading and the globe scale, bottom left like the QWidget globe
// HUD (MapMonitorVerticalControlsHudWidget, MapMonitorCompassHudWidget).
//
// Same content, positions and sizes as the QWidget HUD of the removed QRhi
// map (MapMonitorContainer::positionDesktopHudWidgets(),
// map_monitor_hud_controls, EntityMapLegendHud; the QWidget class names in
// the HUD comments refer to it, see the version history), drawn as one batch
// per frame into MapBgfxHudCanvas
// with text runs from MapBgfxHudTextCache. Layout is rebuilt every frame
// from the current state (a handful of rectangles); the hit boxes of the
// last build answer mouse input, so what is clickable is exactly what was
// drawn. Popups (combo lists, palette menu) and tooltips are HUD state too:
// they are drawn last, on top, and take input first.
//
// Pure Qt Gui + math: no bgfx, no MapModel, no HydraulicData, testable
// without a GPU. The legend's content (items, ranges, units, palette
// colours) is computed by the application (map_bgfx_legend_content.h) and
// pushed in with setLegend().
class MapBgfxHud
{
    // Explicit, stable Qt Linguist context (also when this class moves into
    // AOWIS-RENDER). Does not require QObject ownership or the GUI.
    Q_DECLARE_TR_FUNCTIONS(MapBgfxHud)
public:
    enum class Action
    {
        None,
        CancelMapTiles,
        CancelTerrain,
        ViewMode2d,
        ViewMode3d,
        ToggleFullscreen,
        // Zoom buttons under the joystick (one smooth octave each).
        ZoomIn,
        ZoomOut,
        // Legend: Command::group selects node (0) / link (1) / heatmap (2).
        // LegendVisual: Command::value is the chosen metric (VisualNode /
        // VisualLink / VisualHeatmap as int).
        LegendVisual,
        // LegendPalette: Command::value is the NetworkSymbologyPalette as
        // int, Command::flipped its direction.
        LegendPalette,
        // --- Globe (slice 5) ---------------------------------------------------
        ToggleWireframe,
        ToggleMapTiles,
        ToggleHillshade,
        // Command::value: MapUndergroundMode as int (X-Ray 0, Hide 1, Solid 2).
        UndergroundMode,
        // A camera slider dragged: Command::group the GlobeSlider, number its
        // position 0..1 (bottom to top), phase Begin (press) / Move / End.
        GlobeSlider,
        // Right press on a slider: back to its default (QWidget HUD).
        GlobeSliderReset,
        // Compass drag: phase Begin (press), Move (number: horizontal pointer
        // delta in px), End (value 1: it was a click, turn north).
        CompassDrag,
        // Wheel over the compass: number = notches (angle delta / 120).
        CompassWheel,
        // Press on the pan joystick (phase Begin, position: the press). The
        // drag itself belongs to the map's pointer capture (MapWidget): the
        // HUD does not hold the press, moves and the release go on to the
        // map, which reports the deflection back (setJoystick()).
        JoystickDrag
    };

    // Continuous interactions (sliders, compass) report their course.
    enum class Phase
    {
        None,
        Begin,
        Move,
        End
    };

    // An action plus its arguments (unused fields stay default).
    struct Command
    {
        Action action = Action::None;
        int group = -1;
        int value = 0;
        bool flipped = false;
        double number = 0.0;
        Phase phase = Phase::None;
        QPointF position;
    };

    // The globe's camera sliders, left to right (QWidget HUD order).
    enum GlobeSliderIndex
    {
        NetworkOffsetSlider = 0,
        TiltSlider = 1,
        DistanceSlider = 2,
        ExaggerationSlider = 3,
        GlobeSliderCount = 4
    };

    // Pointer shape the map should show for the HUD element under it.
    enum class Cursor
    {
        Arrow,
        PointingHand,
        Cross
    };

    // Colours of the QWidget HUD (configureHudFrame(): window colour at
    // alpha 220, the palette's text/button roles; QMenu/QComboBox popups and
    // QToolTip use their own roles).
    struct Palette
    {
        QRgb panel = qRgba(239, 239, 239, 220);
        QRgb border = qRgb(160, 160, 160);
        QRgb text = qRgb(0, 0, 0);
        QRgb button = qRgb(239, 239, 239);
        QRgb button_text = qRgb(0, 0, 0);
        QRgb disabled_text = qRgb(150, 150, 150);
        QRgb highlight = qRgb(48, 140, 198);
        QRgb highlighted_text = qRgb(255, 255, 255);
        // Opaque popup backgrounds (QMenu: Window, combo list: Base).
        QRgb window = qRgb(239, 239, 239);
        QRgb base = qRgb(255, 255, 255);
        // Ramp without finite values (QPalette::AlternateBase).
        QRgb alternate_base = qRgb(247, 247, 247);
        QRgb tooltip = qRgb(255, 255, 220);
        QRgb tooltip_text = qRgb(0, 0, 0);

        static Palette fromQPalette(const QPalette &palette);
    };

    // Scale bar (2D): for a horizontal sample of bar_px pixels from
    // (lon0, lat0) to (lon1, lat1), the nice bar length in pixels and its
    // label in the user's distance unit. valid=false hides the bar.
    struct ScaleBar
    {
        bool valid = false;
        double width_px = 0.0;
        QString label;
    };
    using ScaleProvider = std::function<ScaleBar(double lon0, double lat0, double lon1,
                                                 double lat1, double bar_px)>;

    // --- Legend content (slice 6b) ----------------------------------------------
    // One colour ramp as EntityMapLegendHud's MapSymbologyRampWidget shows
    // it: displayed range and unit (already converted to the user's unit),
    // the active palette's stops with the flip applied, and the palette
    // selection for the menu's checked states and its Reset.
    struct LegendRamp
    {
        // A metric is chosen: the ramp row is shown under the combo.
        bool visible = false;
        double minimum = 0.0;
        double maximum = 0.0;
        QString unit;
        // Evenly spaced stops (0xAARRGGBB), flip already applied.
        QVector<QRgb> colors;
        int palette = 0;
        bool flipped = false;
        int default_palette = 0;
        bool default_flipped = false;
    };
    struct LegendGroup
    {
        // Combo entries (text, metric value), in display order.
        QVector<QString> items;
        QVector<int> values;
        int current = 0;
        LegendRamp ramp;
    };
    struct LegendPaletteChoice
    {
        int value = 0;
        QString name;
        // Unflipped stops (0xAARRGGBB) for the menu preview.
        QVector<QRgb> colors;
        // Diverging palettes get their own menu section.
        bool diverging = false;
    };
    struct Legend
    {
        bool visible = false;
        // Node, link, heatmap.
        std::array<LegendGroup, 3> groups;
        QVector<LegendPaletteChoice> palettes;
    };
    static constexpr int LegendGroupCount = 3;

    // --- Globe content (slice 5) ---------------------------------------------------
    // One camera slider: position 0..1 (bottom to top) and its labels in the
    // user's units (formatted by the application).
    struct GlobeSliderState
    {
        double fraction = 0.0;
        QString maximum_label;
        QString minimum_label;
        QString value_label;
        QString tooltip;
    };
    struct Globe
    {
        // The globe is shown: draw its controls (and its scale instead of
        // the 2D one).
        bool visible = false;
        double yaw_deg = 0.0;
        std::array<GlobeSliderState, GlobeSliderCount> sliders;
        // MapUndergroundMode as int.
        int underground_mode = 0;
        bool wireframe = false;
        bool map_tiles = true;
        bool hillshade = true;
        // Globe scale bar (MapMonitorScaleHudWidget's globe formula).
        ScaleBar scale;
        // Terrain meshes being built: completed / total (0: unknown yet).
        // The panel shows only for sustained work (MeshPanelShowDelayS).
        bool mesh_active = false;
        int mesh_completed = 0;
        int mesh_total = 0;
    };

    struct FrameInputs
    {
        QSizeF viewport;
        bool two_d = true;
        // 2D camera (continuous zoom), used for the GPS dot and the scale.
        MapBgfx2d::Camera camera;
        // Monotonic seconds, for the busy indicators and the tooltip delay.
        double time_s = 0.0;
    };

    // --- State ----------------------------------------------------------------
    void setFont(const QFont &font);
    void setScaleProvider(ScaleProvider provider) { this->scale_provider = std::move(provider); }
    void setImages(const QImage &crosshair, const QImage &fullscreen, const QImage &fullscreen_exit,
                   const QImage &zoom_in, const QImage &zoom_out);
    void setGps(bool valid, double longitude_deg, double latitude_deg);
    void setDownloadActivity(int tiles_active, int tiles_queued,
                             int terrain_active, int terrain_queued);
    void setViewMode(bool two_d, bool globe_available);
    // The 2D/3D switch (default shown). The map editor has none.
    void setViewSwitchVisible(bool visible) { this->view_switch_visible = visible; }
    // Globe controls and their state (call per frame while the globe is
    // shown; visible=false hides them).
    void setGlobe(const Globe &globe);
    // The HUD changes with time alone (download busy indicators, a tooltip
    // waiting for its delay): the surface must keep presenting frames while
    // this is true, otherwise it renders only on changes.
    bool animating() const
    {
        return this->tiles_active > 0 || this->tiles_queued > 0 || this->terrain_active > 0
            || this->terrain_queued > 0 || this->tooltip_waiting
            || (this->globe.visible && this->globe.mesh_active) || this->mesh_panel_shown;
    }
    // The map is shown fullscreen: the button shows "leave fullscreen". The
    // button is always there (first in the top-left panel).
    void setFullscreen(bool active);
    // Pan joystick: the knob's deflection (|deflection| <= 1, screen
    // directions) and whether a joystick drag is running.
    void setJoystick(const QPointF &deflection, bool active);
    // The configured navigation keys (shortcut texts, empty: none), listed
    // in the joystick's and the zoom buttons' tooltips.
    struct NavigationKeys
    {
        QString pan_up;
        QString pan_down;
        QString pan_left;
        QString pan_right;
        QString zoom_in;
        QString zoom_out;
    };
    void setNavigationKeys(const NavigationKeys &keys) { this->navigation_keys = keys; }
    // Keyboard pan keys held: the view's direction (x right, y down; each
    // -1, 0 or 1). The joystick's arrows glow for them too.
    void setKeyboardPan(const QPointF &direction) { this->keyboard_pan_direction = direction; }
    // Replaces the legend content. Open popups stay open when their group
    // still exists; hiding the legend closes them.
    void setLegend(const Legend &legend);
    // A translator has changed. Renderer-owned labels are resolved during
    // build(), while application-provided legend/slider strings must be
    // refreshed by their owner. Preserve popups, pointer capture and camera.
    void languageChanged();
    const Legend &legend() const { return this->legend_content; }

    // --- Frame ----------------------------------------------------------------
    // Builds the HUD into canvas (cleared first). Rebuilds once if the text
    // atlas had to be cleared during the build (earlier sprites are stale).
    void build(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
               const FrameInputs &inputs, const Palette &palette);

    // --- Input (logical pixels) -----------------------------------------------
    // A press on a HUD panel is consumed (never reaches map navigation or
    // picking); on an enabled button it arms that button. A press on a
    // legend combo opens its list, on a colour ramp the palette menu (both
    // on press, like QComboBox / the QWidget ramp). While a popup is open,
    // a press outside it closes it and is consumed (Qt popup behaviour).
    bool mousePress(double x, double y);
    // Consumed iff the matching press was. Returns the button's action when
    // the release is on the armed button; command (optional) receives the
    // action with its legend arguments.
    Action mouseRelease(double x, double y, bool *consumed, Command *command = nullptr);
    // Updates hover (and drags the combo list's scroll bar). Consumed while
    // a HUD press is held. over_button: the pointer is over an enabled
    // button (pointing-hand cursor); cursor() has the exact shape.
    bool mouseMove(double x, double y, bool *over_button);
    // Right button press: on a globe slider it resets the slider (command);
    // consumed over any HUD panel.
    bool mousePressRight(double x, double y);
    // Wheel over the HUD (angle delta in 1/8 degree, positive = away from
    // the user). Scrolls an open combo list, turns the compass; consumed
    // over any HUD panel so the map does not zoom underneath.
    bool mouseWheel(double x, double y, double angle_delta_y);
    // The wheel over the joystick or the zoom buttons zooms the map, about
    // the crosshair (not about the pointer up in the corner); mouseWheel()
    // leaves it to the map there.
    bool wheelZoomsAtCrosshair(double x, double y) const;
    // Commands of continuous interactions (slider drags, compass drags and
    // wheel, slider resets) since the last call, in order. The application
    // drains them after every input event.
    QVector<Command> takeCommands();
    // The pointer left the map: drop the hover highlight (a held press
    // stays armed; the window keeps the implicit grab).
    void clearHover();
    // Escape: closes an open popup. True when one was open.
    bool closePopup();
    bool popupOpen() const { return this->open_combo >= 0 || this->open_palette_menu >= 0; }
    // Any blocking HUD panel at (x, y).
    bool blocksInput(double x, double y) const;
    Cursor cursor() const;

    // Same number formatting as the QWidget legend ramp (three significant
    // digits, scientific notation outside 1e-3 .. 1e3, locale aware).
    static QString formatLegendValue(double value);

    // Layout constants (logical px) of the QWidget HUD.
    static constexpr double MarginPx = 12.0;
    // Terrain-mesh panel: mesh builds come in short bursts whenever the
    // camera moves; the panel appears only after this much continuous
    // building and disappears this long after it ended (frames continue
    // until then: animating()).
    static constexpr double MeshPanelShowDelayS = 0.6;
    static constexpr double MeshPanelHideDelayS = 0.4;
    static constexpr double ScaleWidthPx = 116.0;
    static constexpr double ScaleHeightPx = 42.0;
    static constexpr double ScalePaddingPx = 6.0;
    static constexpr double ScaleTickHeightPx = 6.0;
    static constexpr double CrosshairExtentPx = 60.0;
    static constexpr double GpsRadiusPx = 5.0;
    // EntityMapLegendHud: fixed width Sizes::SidebarRightWidth, contents
    // margins 7, spacing 4; MapSymbologyRampWidget 54 px high with a 16 px
    // ramp inset 7/4; hover swatch 96 x 52.
    static constexpr double LegendWidthPx = 380.0;
    static constexpr double LegendPaddingPx = 7.0;
    static constexpr double LegendSpacingPx = 4.0;
    static constexpr double LegendRampRowHeightPx = 54.0;
    static constexpr double LegendRampInsetXPx = 7.0;
    static constexpr double LegendRampInsetYPx = 4.0;
    static constexpr double LegendRampHeightPx = 16.0;
    static constexpr double LegendSwatchWidthPx = 96.0;
    static constexpr double LegendSwatchHeightPx = 52.0;
    // QToolTip wake-up delay.
    static constexpr double TooltipDelayS = 0.7;
    // Globe controls (map_monitor_hud_controls.cpp): compass 82 px, slider
    // columns 56 px wide with 74 px sliders.
    static constexpr double CompassDiameterPx = 82.0;
    static constexpr double SliderColumnWidthPx = 56.0;
    static constexpr double SliderHeightPx = 74.0;
    // A compass press that moves less than this is a click (turn north).
    static constexpr double CompassDragThresholdPx = 4.0;
    // Pan joystick, top right: the ring and the knob; the knob moves at most
    // JoystickKnobTravelPx from the centre (full deflection).
    static constexpr double JoystickDiameterPx = 76.0;
    static constexpr double JoystickKnobDiameterPx = 26.0;
    static constexpr double JoystickKnobTravelPx = 18.0;
    // Zoom in / out, stacked under the joystick: square icon buttons.
    static constexpr double ZoomButtonExtentPx = 34.0;

private:
    enum class Element
    {
        None,
        CancelMapTiles,
        CancelTerrain,
        ViewMode2d,
        ViewMode3d,
        Fullscreen,
        ZoomIn,
        ZoomOut,
        // Legend (group = node/link/heatmap, index = item / palette choice).
        LegendCombo,
        LegendRampRow,
        LegendRamp,
        LegendComboItem,
        LegendComboScroll,
        LegendPalette,
        LegendPaletteFlip,
        LegendPaletteReset,
        // Globe (index = toggle / underground mode / slider).
        GlobeWireframe,
        GlobeMapTiles,
        GlobeHillshade,
        GlobeUnderground,
        GlobeSlider,
        Compass,
        Joystick,
        // Background of an open popup: blocks what lies underneath
        // (Target::index: PopupComboList or PopupPaletteMenu).
        Popup
    };
    static constexpr int PopupComboList = 0;
    static constexpr int PopupPaletteMenu = 1;
    struct Target
    {
        Element element = Element::None;
        int group = -1;
        int index = -1;

        bool operator==(const Target &other) const
        {
            return this->element == other.element && this->group == other.group
                && this->index == other.index;
        }
        bool operator!=(const Target &other) const { return !(*this == other); }
    };
    struct HitBox
    {
        MapBgfxHudRect rect;
        Target target;
        bool enabled = false;
        Cursor cursor = Cursor::Arrow;
        QString tooltip;
    };

    void buildOnce(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                   const FrameInputs &inputs, const Palette &palette);
    void drawPanel(MapBgfxHudCanvas *canvas, const MapBgfxHudRect &rect, const Palette &palette);
    void drawButton(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                    const MapBgfxHudRect &rect, const QString &label, const Target &target,
                    bool enabled, bool checked, const Palette &palette,
                    const QString &tooltip = QString());
    double buttonWidth(MapBgfxHudTextCache *cache, const QString &label) const;
    double buttonHeight(MapBgfxHudTextCache *cache) const;
    // Returns the panel width actually used (layout pass when canvas null).
    double downloadPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y,
                         const QString &title, int active, int queued, Element cancel,
                         bool cancel_pending, double time_s, const Palette &palette);
    void scaleBar(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                  const FrameInputs &inputs, const Palette &palette);
    // Scale panel at (x, y) for a computed bar.
    void drawScalePanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y,
                        const ScaleBar &scale, const Palette &palette);

    // --- Globe (slice 5) -----------------------------------------------------------
    // Terrain-mesh progress panel (layout pass when canvas null); width.
    double meshPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y,
                     double time_s, const Palette &palette);
    // Sliders, underground mode, compass and the globe scale, bottom left.
    void globeControls(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                       const FrameInputs &inputs, const Palette &palette);
    void drawSlider(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, const MapBgfxHudRect &column,
                    int index, const Palette &palette);
    void drawCompass(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y,
                     const Palette &palette);
    // Pan joystick, top right, and the zoom buttons under it.
    void joystick(MapBgfxHudCanvas *canvas, const FrameInputs &inputs, const Palette &palette);
    void zoomButtons(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, const FrameInputs &inputs,
                     const Palette &palette);
    // Slider position 0..1 at pointer y on slider index's track.
    double sliderFraction(int index, double y) const;

    // --- Legend (slice 6b) -------------------------------------------------------
    double comboHeight(MapBgfxHudTextCache *cache) const;
    double comboItemHeight(MapBgfxHudTextCache *cache) const;
    void legendPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                     const FrameInputs &inputs, const Palette &palette);
    void drawCombo(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                   const MapBgfxHudRect &rect, int group, const Palette &palette);
    void drawRampRow(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                     const MapBgfxHudRect &row, int group, const Palette &palette);
    void drawRampLabels(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                        const MapBgfxHudRect &row, const MapBgfxHudRect &ramp,
                        const LegendRamp &content, bool uniform, const Palette &palette);
    int rampLabelCount(MapBgfxHudTextCache *cache, const LegendRamp &content,
                       double ramp_width) const;
    void drawHoverSwatch(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                         const Palette &palette);
    void comboPopup(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                    const FrameInputs &inputs, const Palette &palette);
    void paletteMenu(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                     const FrameInputs &inputs, const Palette &palette);
    void tooltipBox(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                    const FrameInputs &inputs, const Palette &palette);
    // Ramp colour at fraction 0..1 of a ramp (interpolated between stops).
    static QRgb rampColor(const LegendRamp &ramp, double fraction);
    QString rampTooltip(const LegendRamp &ramp) const;
    void openCombo(int group);
    void openPaletteMenu(int group);
    // Scroll-bar drag: puts the thumb's grab point at y.
    void dragComboThumbTo(double y);
    // Track click outside the thumb: one page towards y (QScrollBar).
    void pageComboTowards(double y);
    void addHitBox(const MapBgfxHudRect &rect, const Target &target, bool enabled,
                   Cursor cursor, const QString &tooltip = QString());
    // Whether a hit box of the last build still answers input (popup boxes
    // only while their popup is open).
    bool hitBoxLive(const HitBox &box) const;
    const HitBox *hitBoxAt(double x, double y) const;
    Target targetAt(double x, double y) const;
    static bool isPopupElement(Element element);
    // Elements whose press arms a click (release on the same target acts).
    static bool isClickable(Element element);
    static Action actionFor(Element element);
    Command commandFor(const Target &target);

    QFont font;
    QFont bold_font;
    QFont scale_font;
    QFont legend_label_font;
    QFont legend_value_font;
    ScaleProvider scale_provider;
    QImage crosshair_image;
    QImage fullscreen_image;
    QImage fullscreen_exit_image;
    QImage zoom_in_image;
    QImage zoom_out_image;
    bool gps_valid = false;
    double gps_longitude_deg = 0.0;
    double gps_latitude_deg = 0.0;
    int tiles_active = 0;
    int tiles_queued = 0;
    int terrain_active = 0;
    int terrain_queued = 0;
    // A clicked Cancel stays disabled until the activity changes (QWidget HUD).
    bool tiles_cancel_pending = false;
    bool terrain_cancel_pending = false;
    bool two_d = true;
    bool globe_available = false;
    bool view_switch_visible = true;
    bool fullscreen_active = false;
    QPointF joystick_deflection;
    bool joystick_active = false;
    QPointF keyboard_pan_direction;
    NavigationKeys navigation_keys;
    // Joystick and zoom buttons of the last build (wheel zooms there).
    MapBgfxHudRect joystick_rect;
    MapBgfxHudRect zoom_rect;

    // Globe controls (slice 5): state, the slider tracks of the last build,
    // the compass drag and the queued continuous commands.
    Globe globe;
    std::array<MapBgfxHudRect, GlobeSliderCount> slider_tracks;
    MapBgfxHudRect compass_rect;
    double compass_last_x = 0.0;
    double compass_last_y = 0.0;
    double compass_drag_distance = 0.0;
    QVector<Command> pending_commands;

    // Legend content and its per-frame layout (positions of the last build,
    // used to anchor popups and the hover swatch).
    Legend legend_content;
    MapBgfxHudRect legend_rect;
    std::array<MapBgfxHudRect, 3> legend_combo_rects;
    std::array<MapBgfxHudRect, 3> legend_ramp_rects;
    // Popups: group whose combo list / palette menu is open, or -1.
    int open_combo = -1;
    int open_palette_menu = -1;
    // First visible row of the open combo list.
    int combo_scroll = 0;
    // A freshly opened list centres its current entry at the next build
    // (only then is the number of visible rows known).
    bool combo_center_pending = false;
    // Geometry of the open combo list (last build) for scroll-bar input.
    MapBgfxHudRect combo_list_rect;
    MapBgfxHudRect combo_track_rect;
    MapBgfxHudRect combo_thumb_rect;
    int combo_visible_rows = 0;
    // Distance from the thumb's top to where it was grabbed.
    double combo_thumb_grab = 0.0;
    // Wheel delta not yet turned into whole rows (touchpads).
    double combo_wheel_remainder = 0.0;

    QVector<HitBox> hit_boxes;
    QVector<MapBgfxHudRect> blocking_panels;
    Target hovered;
    Target armed;
    bool press_consumed = false;
    double pointer_x = -1.0;
    double pointer_y = -1.0;
    bool pointer_inside = false;
    // Tooltip of the hovered target: since when (frame time, -1 = start at
    // the next build), and whether a press dismissed it until the pointer
    // reaches another target.
    double tooltip_since_s = -1.0;
    bool tooltip_suppressed = false;
    // The last build found a tooltip target still inside its delay.
    bool tooltip_waiting = false;
    // Terrain-mesh panel debounce (frame times; -1 = not running).
    double mesh_busy_since_s = -1.0;
    double mesh_idle_since_s = -1.0;
    bool mesh_panel_shown = false;
    void updateMeshPanel(double time_s);
};

#endif // MAP_BGFX_HUD_H
