#ifndef MAP_BGFX_GLYPH_ATLAS_H
#define MAP_BGFX_GLYPH_ATLAS_H

#include "map/bgfx/map_bgfx_hud_canvas.h"

#include <QFont>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QRawFont>
#include <QRect>
#include <QString>
#include <QVector>

// Signed-distance-field glyph atlas for the map labels (map_bgfx_labels.h),
// drawn by MapBgfxLabelGpu with fs_label_sdf.
//
// The usual way game and map engines draw text (Valve's SDF paper, Mapbox GL
// / MapLibre symbol layers): every GLYPH, not every string, is rasterized
// once into a single-channel atlas as its signed distance to the outline,
// at one base size (BasePixelSize). The fragment shader thresholds that
// distance per screen pixel, so the same entry draws crisp text at any size
// and device pixel ratio, and the halo is a second threshold of the same
// distance (width and colour are uniforms, nothing is baked in).
//
// - Shaping, kerning and font fallback stay Qt's: text is laid out with
//   QTextLayout, and each QGlyphRun's QRawFont + glyph index is the key.
//   The key does not contain the pixel size: one entry serves every size.
// - Distances come from the glyph's outline (QRawFont::pathForGlyph()) filled
//   at Supersample x the base size, through an exact Euclidean distance
//   transform (Felzenszwalb & Huttenlocher), averaged down to the base grid.
//   Encoded: 0.5 is the outline, +-SpreadPx base pixels map to 1 / 0 (inside
//   positive).
// - Label IDs and values use a few dozen glyphs, so the atlas fills once and
//   stays. When it is full anyway, glyph() fails (full() turns true) instead
//   of evicting entries handed out this frame; the caller defers that label
//   and clears the atlas at the start of its next frame (clear()), so quads
//   never point at reused space.
//
// Pure Qt Gui + math (no bgfx): unit-tested without a GPU.
class MapBgfxGlyphAtlas
{
public:
    // Em size the distances are sampled at, and their range (base px).
    // Mapbox uses 24 / 8; 32 / 8 keeps small IDs a bit sharper and still
    // allows a halo of about a quarter of the font size.
    static constexpr double BasePixelSize = 32.0;
    static constexpr int SpreadPx = 8;
    static constexpr int Supersample = 4;

    explicit MapBgfxGlyphAtlas(int atlas_extent = 1024);

    // One glyph entry. Quad corners are base px relative to the pen position
    // on the baseline (y down); multiply by pixelSize / BasePixelSize.
    struct Glyph
    {
        bool valid = false;
        // Whitespace: advances, draws nothing.
        bool empty = false;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = 0.0;
        double y1 = 0.0;
    };

    // One glyph of a laid-out line: atlas key, pen position (logical px,
    // relative to the top-left of the line box; y is the baseline) and the
    // scale from base px to logical px.
    struct LineGlyph
    {
        quint64 key = 0;
        QPointF pen;
        double scale = 1.0;
    };

    // Shapes one line (no '\n') with font. Cached per (font, text).
    const QVector<LineGlyph> &layoutLine(const QString &line, const QFont &font);

    // The entry of a glyph key, creating its distance field when missing.
    // allow_create false: lookup only. Invalid when absent, or when the atlas
    // has no room left (then full() is true).
    Glyph glyph(quint64 key, bool allow_create);
    bool contains(quint64 key) const { return this->glyphs.contains(key); }

    // Emits one shaped line (layoutLine()) as one quad per visible glyph:
    // the line box's top-left at (x, y) in canvas px, pens and glyphs scaled
    // by scale (1: the font's own size, e.g. a camera zoom otherwise), tinted
    // tint_argb (draw with MapBgfxLabelGpu). Glyphs not yet in the atlas are
    // created while *create_budget > 0 (decremented; nullptr: lookup only).
    // Returns false and emits nothing when a glyph is still missing (budget
    // spent or atlas full): draw the line in a later frame.
    bool emitLine(MapBgfxHudCanvas *canvas, const QString &line, const QFont &font,
                  double x, double y, double scale, quint32 tint_argb, int *create_budget);

    // A creation failed for lack of room since the last clear().
    bool full() const { return this->full_flag; }
    // Drops every entry (next generation); call between frames only.
    void clear();

    // R8 distances, rows top first (bgfx upload order).
    const QImage &atlas() const { return this->atlas_image; }
    bool dirty() const { return !this->dirty_rect.isNull(); }
    QRect dirtyRect() const { return this->dirty_rect; }
    void markUploaded() { this->dirty_rect = QRect(); }
    // Incremented whenever the atlas is cleared (all entries invalid).
    quint64 generation() const { return this->atlas_generation; }
    int glyphCount() const { return int(this->glyphs.size()); }

private:
    struct FontSource
    {
        QRawFont font;
    };

    void reset();
    bool allocate(int width, int height, QPoint *position);
    Glyph createGlyph(quint64 key);
    quint32 fontId(const QRawFont &font);

    QImage atlas_image;
    QHash<quint64, Glyph> glyphs;
    // Raw fonts by id (the high half of a glyph key), ids by identity.
    QVector<FontSource> fonts;
    QHash<QString, quint32> font_id_by_identity;
    // Shaped lines by font key + text (cleared when it grows too large).
    QHash<QString, QVector<LineGlyph>> line_layouts;
    QRect dirty_rect;
    int shelf_x = 0;
    int shelf_y = 0;
    int shelf_height = 0;
    quint64 atlas_generation = 0;
    bool full_flag = false;
};

#endif // MAP_BGFX_GLYPH_ATLAS_H
