#include "map/bgfx/map_bgfx_hud_text_cache.h"

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace
{
// Opaque white block in the atlas corner; its centre texel feeds solid shapes.
constexpr int WhiteBlockExtent = 4;
// Transparent gap around every entry so linear filtering never bleeds.
constexpr int EntryPadding = 1;
// Logical padding left/right of a text run for glyph overhang (italic, "j").
constexpr double TextPaddingPx = 1.0;
}

MapBgfxHudTextCache::MapBgfxHudTextCache(int atlas_extent)
    : atlas_image(std::max(64, atlas_extent), std::max(64, atlas_extent),
                  QImage::Format_RGBA8888_Premultiplied)
{
    reset();
}

void MapBgfxHudTextCache::setDevicePixelRatio(double ratio)
{
    const double bounded = std::isfinite(ratio) && ratio > 0.0 ? ratio : 1.0;
    if (std::fabs(bounded - this->device_pixel_ratio) < 1e-6)
        return;
    this->device_pixel_ratio = bounded;
    reset();
}

void MapBgfxHudTextCache::setLogicalDpi(double dpi)
{
    const double bounded = std::isfinite(dpi) && dpi > 1.0 ? dpi : 96.0;
    if (std::fabs(bounded - this->logical_dpi) < 1e-6)
        return;
    this->logical_dpi = bounded;
    reset();
}

void MapBgfxHudTextCache::reset()
{
    this->atlas_image.fill(Qt::transparent);
    for (int y = 0; y < WhiteBlockExtent; ++y)
    {
        for (int x = 0; x < WhiteBlockExtent; ++x)
            this->atlas_image.setPixel(x, y, 0xffffffffu);
    }
    const double extent = double(this->atlas_image.width());
    this->white_u = float((WhiteBlockExtent * 0.5) / extent);
    this->white_v = float((WhiteBlockExtent * 0.5) / extent);
    this->sprites.clear();
    this->shelf_x = WhiteBlockExtent + EntryPadding;
    this->shelf_y = 0;
    this->shelf_height = WhiteBlockExtent;
    this->dirty_rect = this->atlas_image.rect();
    ++this->atlas_generation;
}

bool MapBgfxHudTextCache::allocate(int width, int height, QPoint *position)
{
    const int extent = this->atlas_image.width();
    if (width > extent || height > extent)
        return false;
    if (this->shelf_x + width > extent)
    {
        this->shelf_y += this->shelf_height;
        this->shelf_x = 0;
        this->shelf_height = 0;
    }
    if (this->shelf_y + height > this->atlas_image.height())
        return false;
    *position = QPoint(this->shelf_x, this->shelf_y);
    this->shelf_x += width;
    this->shelf_height = std::max(this->shelf_height, height);
    return true;
}

MapBgfxHudSprite MapBgfxHudTextCache::store(const QImage &image, const QSizeF &logical_size)
{
    MapBgfxHudSprite result;
    if (image.isNull())
        return result;
    const int padded_width = image.width() + 2 * EntryPadding;
    const int padded_height = image.height() + 2 * EntryPadding;
    QPoint position;
    if (!allocate(padded_width, padded_height, &position))
    {
        // Full: start over with only what is requested from now on.
        reset();
        this->evicted_this_frame = true;
        if (!allocate(padded_width, padded_height, &position))
            return result;
    }
    const QPoint target(position.x() + EntryPadding, position.y() + EntryPadding);
    {
        QPainter painter(&this->atlas_image);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(target, image);
    }
    const QRect changed(target, image.size());
    this->dirty_rect = this->dirty_rect.isNull() ? changed : this->dirty_rect.united(changed);

    const double extent_x = double(this->atlas_image.width());
    const double extent_y = double(this->atlas_image.height());
    result.valid = true;
    result.u0 = float(double(target.x()) / extent_x);
    result.v0 = float(double(target.y()) / extent_y);
    result.u1 = float(double(target.x() + image.width()) / extent_x);
    result.v1 = float(double(target.y() + image.height()) / extent_y);
    result.width = logical_size.width();
    result.height = logical_size.height();
    return result;
}

QSizeF MapBgfxHudTextCache::measure(const QString &text, const QFont &font) const
{
    const QFontMetricsF metrics(font);
    return QSizeF(metrics.horizontalAdvance(text), metrics.height());
}

bool MapBgfxHudTextCache::containsText(const QString &text, const QFont &font) const
{
    return this->sprites.contains(QStringLiteral("T|") + font.key() + QLatin1Char('|') + text);
}

MapBgfxHudSprite MapBgfxHudTextCache::text(const QString &text, const QFont &font)
{
    const QString key = QStringLiteral("T|") + font.key() + QLatin1Char('|') + text;
    const QHash<QString, MapBgfxHudSprite>::const_iterator found = this->sprites.constFind(key);
    if (found != this->sprites.cend())
        return found.value();

    const QSizeF advance = measure(text, font);
    const QSizeF logical(std::ceil(advance.width() + 2.0 * TextPaddingPx),
                         std::ceil(advance.height()));
    const double ratio = this->device_pixel_ratio;
    QImage run(std::max(1, int(std::ceil(logical.width() * ratio))),
               std::max(1, int(std::ceil(logical.height() * ratio))),
               QImage::Format_RGBA8888_Premultiplied);
    run.fill(Qt::transparent);
    // Same logical DPI as the screen: point-sized fonts match the widgets.
    const int dots_per_meter = int(std::lround(this->logical_dpi / 0.0254));
    run.setDotsPerMeterX(dots_per_meter);
    run.setDotsPerMeterY(dots_per_meter);
    run.setDevicePixelRatio(ratio);
    {
        QPainter painter(&run);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        const QFontMetricsF metrics(font, &run);
        painter.drawText(QPointF(TextPaddingPx, metrics.ascent()), text);
    }
    run.setDevicePixelRatio(1.0);
    MapBgfxHudSprite sprite = store(run, logical);
    sprite.origin_x = TextPaddingPx;
    sprite.origin_y = 0.0;
    if (sprite.valid)
        this->sprites.insert(key, sprite);
    return sprite;
}

MapBgfxHudSprite MapBgfxHudTextCache::cachedImage(const QString &key,
                                                  const QSizeF &logical_size) const
{
    const QString full_key = QStringLiteral("I|%1|%2x%3")
        .arg(key).arg(logical_size.width()).arg(logical_size.height());
    return this->sprites.value(full_key);
}

MapBgfxHudSprite MapBgfxHudTextCache::image(const QString &key, const QImage &image,
                                            const QSizeF &logical_size)
{
    const QString full_key = QStringLiteral("I|%1|%2x%3")
        .arg(key).arg(logical_size.width()).arg(logical_size.height());
    const QHash<QString, MapBgfxHudSprite>::const_iterator found =
        this->sprites.constFind(full_key);
    if (found != this->sprites.cend())
        return found.value();
    if (image.isNull() || !(logical_size.width() > 0.0) || !(logical_size.height() > 0.0))
        return MapBgfxHudSprite();
    const QSize physical(std::max(1, int(std::lround(logical_size.width() * this->device_pixel_ratio))),
                         std::max(1, int(std::lround(logical_size.height() * this->device_pixel_ratio))));
    const QImage scaled = image.scaled(physical, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QImage pixels = scaled;
    pixels.setDevicePixelRatio(1.0);
    const MapBgfxHudSprite sprite = store(pixels, logical_size);
    if (sprite.valid)
        this->sprites.insert(full_key, sprite);
    return sprite;
}

QString MapBgfxHudTextCache::haloKey(const QString &text, const QFont &font, quint32 text_argb,
                                     quint32 halo_argb, double halo_px)
{
    return QStringLiteral("L|%1|%2|%3|%4|").arg(font.key())
               .arg(qulonglong(text_argb)).arg(qulonglong(halo_argb)).arg(halo_px)
        + text;
}

QSizeF MapBgfxHudTextCache::measureBlock(const QString &text, const QFont &font) const
{
    const QFontMetricsF metrics(font);
    const QStringList lines = text.split(QLatin1Char('\n'));
    double width = 0.0;
    for (const QString &line : lines)
        width = std::max(width, metrics.horizontalAdvance(line));
    return QSizeF(width, metrics.height() * double(lines.size()));
}

double MapBgfxHudTextCache::haloTextFootprint(const QSizeF &block, double halo_px) const
{
    // Same rounding as haloText(): halo and the overhang padding on every
    // side, at the device ratio, plus the atlas entry padding.
    const double margin = std::max(0.0, halo_px) + TextPaddingPx;
    const double ratio = this->device_pixel_ratio;
    const double width = std::ceil((block.width() + 2.0 * margin) * ratio) + 2.0 * EntryPadding;
    const double height = std::ceil((block.height() + 2.0 * margin) * ratio) + 2.0 * EntryPadding;
    return width * height;
}

bool MapBgfxHudTextCache::containsHaloText(const QString &text, const QFont &font,
                                           quint32 text_argb, quint32 halo_argb,
                                           double halo_px) const
{
    return this->sprites.contains(haloKey(text, font, text_argb, halo_argb, halo_px));
}

MapBgfxHudSprite MapBgfxHudTextCache::haloText(const QString &text, const QFont &font,
                                               quint32 text_argb, quint32 halo_argb,
                                               double halo_px)
{
    const QString key = haloKey(text, font, text_argb, halo_argb, halo_px);
    const QHash<QString, MapBgfxHudSprite>::const_iterator found = this->sprites.constFind(key);
    if (found != this->sprites.cend())
        return found.value();

    const double halo = std::max(0.0, halo_px);
    const double margin = halo + TextPaddingPx;
    const QSizeF block = measureBlock(text, font);
    const QSizeF logical(std::ceil(block.width() + 2.0 * margin),
                         std::ceil(block.height() + 2.0 * margin));
    const double ratio = this->device_pixel_ratio;
    QImage run(std::max(1, int(std::ceil(logical.width() * ratio))),
               std::max(1, int(std::ceil(logical.height() * ratio))),
               QImage::Format_RGBA8888_Premultiplied);
    run.fill(Qt::transparent);
    const int dots_per_meter = int(std::lround(this->logical_dpi / 0.0254));
    run.setDotsPerMeterX(dots_per_meter);
    run.setDotsPerMeterY(dots_per_meter);
    run.setDevicePixelRatio(ratio);
    {
        // One outline path for all lines: the halo is its stroke (round
        // joins, twice the halo width, half of it outside the glyphs), the
        // text its fill on top.
        const QFontMetricsF metrics(font);
        const QStringList lines = text.split(QLatin1Char('\n'));
        QPainterPath path;
        double baseline = margin + metrics.ascent();
        for (const QString &line : lines)
        {
            const double line_x = margin + 0.5 * (block.width() - metrics.horizontalAdvance(line));
            path.addText(QPointF(line_x, baseline), font, line);
            baseline += metrics.height();
        }
        QPainter painter(&run);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (halo > 0.0)
        {
            painter.strokePath(path, QPen(QColor::fromRgba(halo_argb), 2.0 * halo, Qt::SolidLine,
                                          Qt::RoundCap, Qt::RoundJoin));
        }
        painter.fillPath(path, QColor::fromRgba(text_argb));
    }
    run.setDevicePixelRatio(1.0);
    MapBgfxHudSprite sprite = store(run, logical);
    sprite.origin_x = margin;
    sprite.origin_y = margin;
    if (sprite.valid)
        this->sprites.insert(key, sprite);
    return sprite;
}
