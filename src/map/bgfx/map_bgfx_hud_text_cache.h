#ifndef MAP_BGFX_HUD_TEXT_CACHE_H
#define MAP_BGFX_HUD_TEXT_CACHE_H

#include "map/bgfx/map_bgfx_hud_canvas.h"

#include <QFont>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QSizeF>
#include <QString>

// HUD atlas: text and images rasterized by Qt, composited by bgfx (slice 6).
//
// Each distinct (text, font) is rasterized ONCE as a whole run with QPainter
// (full Qt shaping, kerning, hinting and font fallback, the application font)
// in white, premultiplied, at the device pixel ratio, and packed into one
// RGBA8 atlas. The HUD tints runs with the vertex colour. Images (crosshair,
// icons) are packed the same way. Texel (1, 1) is opaque white for solid
// shapes (MapBgfxHudCanvas::setWhiteTexel()).
//
// The atlas is only re-uploaded where it changed (dirtyRect()). When it is
// full it is cleared and refilled on demand; evictedSinceFrameStart() tells
// the HUD to rebuild that frame, because sprites handed out before the
// eviction point at reused space.
class MapBgfxHudTextCache
{
public:
    explicit MapBgfxHudTextCache(int atlas_extent = 1024);

    // Physical pixels per logical pixel, and the screen's logical DPI (point
    // sized fonts then match the widgets exactly). A change clears the atlas.
    void setDevicePixelRatio(double ratio);
    double devicePixelRatio() const { return this->device_pixel_ratio; }
    void setLogicalDpi(double dpi);
    double logicalDpi() const { return this->logical_dpi; }

    void beginFrame() { this->evicted_this_frame = false; }
    bool evictedSinceFrameStart() const { return this->evicted_this_frame; }

    // White text run; logical size is the advance width x line height.
    MapBgfxHudSprite text(const QString &text, const QFont &font);
    // Logical size of a text run without rasterizing it.
    QSizeF measure(const QString &text, const QFont &font) const;
    // Does not render or mutate the atlas; useful for verifying that a
    // language-change redraw requested a newly translated run.
    bool containsText(const QString &text, const QFont &font) const;

    // Map labels (map_bgfx_labels.h): text in text_argb with an outline
    // ("halo") of halo_px logical pixels in halo_argb, as ONE colour sprite
    // (draw it with a white tint), readable over any imagery. Lines are
    // separated by '\n' and centred on the widest one. The layout box is the
    // text block without the halo (measureBlock()); origin_x/origin_y say
    // where it lies inside the sprite. The font should be pixel sized: the
    // outline path and the metrics then agree on every screen.
    MapBgfxHudSprite haloText(const QString &text, const QFont &font, quint32 text_argb,
                              quint32 halo_argb, double halo_px);
    // haloText() with these arguments is in the atlas (no rasterization).
    bool containsHaloText(const QString &text, const QFont &font, quint32 text_argb,
                          quint32 halo_argb, double halo_px) const;
    // Logical size of a '\n'-separated text block: widest line x lines.
    QSizeF measureBlock(const QString &text, const QFont &font) const;
    // Atlas pixels haloText() needs for a block of this logical size
    // (padding included); callers keep one frame's labels below capacity().
    double haloTextFootprint(const QSizeF &block, double halo_px) const;
    double capacity() const
    {
        return double(this->atlas_image.width()) * double(this->atlas_image.height());
    }
    // Image (premultiplied) scaled to logical_size at the device ratio.
    MapBgfxHudSprite image(const QString &key, const QImage &image, const QSizeF &logical_size);
    // The sprite image(key, ..., logical_size) stored, or an invalid sprite;
    // lets callers skip producing an expensive image that is already there.
    MapBgfxHudSprite cachedImage(const QString &key, const QSizeF &logical_size) const;

    float whiteU() const { return this->white_u; }
    float whiteV() const { return this->white_v; }

    // RGBA8 premultiplied, rows top first (bgfx upload order).
    const QImage &atlas() const { return this->atlas_image; }
    bool dirty() const { return !this->dirty_rect.isNull(); }
    QRect dirtyRect() const { return this->dirty_rect; }
    void markUploaded() { this->dirty_rect = QRect(); }
    // Incremented whenever the atlas is cleared (all sprites invalid).
    quint64 generation() const { return this->atlas_generation; }

private:
    void reset();
    static QString haloKey(const QString &text, const QFont &font, quint32 text_argb,
                           quint32 halo_argb, double halo_px);
    bool allocate(int width, int height, QPoint *position);
    MapBgfxHudSprite store(const QImage &image, const QSizeF &logical_size);

    QImage atlas_image;
    QHash<QString, MapBgfxHudSprite> sprites;
    QRect dirty_rect;
    double device_pixel_ratio = 1.0;
    double logical_dpi = 96.0;
    int shelf_x = 0;
    int shelf_y = 0;
    int shelf_height = 0;
    float white_u = 0.0f;
    float white_v = 0.0f;
    quint64 atlas_generation = 0;
    bool evicted_this_frame = false;
};

#endif // MAP_BGFX_HUD_TEXT_CACHE_H
