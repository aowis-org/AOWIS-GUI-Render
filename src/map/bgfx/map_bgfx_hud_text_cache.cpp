#include "map/bgfx/map_bgfx_hud_text_cache.h"

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>
#include <QVector>

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

// Whole physical pixels covering a logical length (at least 0).
int physicalCeil(double logical, double ratio)
{
    return std::max(0, int(std::ceil(logical * ratio - 1e-6)));
}

// Premultiplied float colour of a 0xAARRGGBB value.
struct PremultipliedColor
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

PremultipliedColor premultipliedColor(quint32 argb)
{
    const float alpha = float(qAlpha(argb)) / 255.0f;
    PremultipliedColor color;
    color.a = alpha;
    color.r = float(qRed(argb)) / 255.0f * alpha;
    color.g = float(qGreen(argb)) / 255.0f * alpha;
    color.b = float(qBlue(argb)) / 255.0f * alpha;
    return color;
}

quint8 unitToByte(float value)
{
    return quint8(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}
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

    // Texel exact: the sprite's logical size is its texel count / ratio and
    // its origin a whole number of texels, so a run drawn at a position on
    // the physical grid maps 1:1 onto screen pixels (point sampling, no
    // dropped or doubled glyph columns at fractional ratios).
    const QSizeF advance = measure(text, font);
    const double ratio = this->device_pixel_ratio;
    const int padding_texels = physicalCeil(TextPaddingPx, ratio);
    const int width_texels = std::max(1, physicalCeil(advance.width(), ratio) + 2 * padding_texels);
    const int height_texels = std::max(1, physicalCeil(advance.height(), ratio));
    const QSizeF logical(double(width_texels) / ratio, double(height_texels) / ratio);
    const double origin_x = double(padding_texels) / ratio;
    QImage run(width_texels, height_texels, QImage::Format_RGBA8888_Premultiplied);
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
        painter.drawText(QPointF(origin_x, metrics.ascent()), text);
    }
    run.setDevicePixelRatio(1.0);
    MapBgfxHudSprite sprite = store(run, logical);
    sprite.origin_x = origin_x;
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
    // Same rounding as haloText(): block, halo and the overhang padding on
    // every side in whole texels, plus the atlas entry padding.
    const double ratio = this->device_pixel_ratio;
    const int margin_texels = physicalCeil(std::max(0.0, halo_px) + TextPaddingPx, ratio);
    const double width = double(std::max(1, physicalCeil(block.width(), ratio) + 2 * margin_texels))
        + 2.0 * EntryPadding;
    const double height = double(std::max(1, physicalCeil(block.height(), ratio) + 2 * margin_texels))
        + 2.0 * EntryPadding;
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

    // Texel exact like text(): whole-texel margin, logical size = texels /
    // ratio, so a block placed on the physical grid is drawn 1:1.
    const double halo = std::max(0.0, halo_px);
    const double ratio = this->device_pixel_ratio;
    const QSizeF block = measureBlock(text, font);
    const int margin_texels = physicalCeil(halo + TextPaddingPx, ratio);
    const int width_texels = std::max(1, physicalCeil(block.width(), ratio) + 2 * margin_texels);
    const int height_texels = std::max(1, physicalCeil(block.height(), ratio) + 2 * margin_texels);
    const QSizeF logical(double(width_texels) / ratio, double(height_texels) / ratio);
    const double margin = double(margin_texels) / ratio;

    // 1. Glyph coverage with the normal hinted text rasterizer (the same
    //    glyphs as the widgets), NOT QPainterPath::addText(): an outline path
    //    is unhinted, so small label text came out soft and smeared, and its
    //    stroked halo ate into the counters of the glyphs.
    QImage coverage(width_texels, height_texels, QImage::Format_ARGB32_Premultiplied);
    coverage.fill(Qt::transparent);
    const int dots_per_meter = int(std::lround(this->logical_dpi / 0.0254));
    coverage.setDotsPerMeterX(dots_per_meter);
    coverage.setDotsPerMeterY(dots_per_meter);
    coverage.setDevicePixelRatio(ratio);
    {
        // Same metrics as measureBlock() (pixel-sized fonts: device free).
        const QFontMetricsF metrics(font);
        const QStringList lines = text.split(QLatin1Char('\n'));
        // Greyscale coverage: it is an alpha mask, LCD subpixel colour
        // fringes would turn into coloured halos.
        QFont mask_font(font);
        mask_font.setStyleStrategy(QFont::StyleStrategy(font.styleStrategy() | QFont::NoSubpixelAntialias));
        QPainter painter(&coverage);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(mask_font);
        painter.setPen(Qt::white);
        double baseline = margin + metrics.ascent();
        for (const QString &line : lines)
        {
            // Lines centred on the widest one, each starting on the
            // physical grid (hinting then keeps the stems sharp).
            const double centred = margin + 0.5 * (block.width() - metrics.horizontalAdvance(line));
            const double line_x = std::round(centred * ratio) / ratio;
            painter.drawText(QPointF(line_x, baseline), line);
            baseline += metrics.height();
        }
    }
    coverage.setDevicePixelRatio(1.0);

    // 2. Halo: the coverage dilated by a disc of halo_px (physical radius,
    //    antialiased rim), then the text composited over it. Exact for any
    //    halo width, keeps the glyph shapes untouched.
    const double radius = halo * ratio;
    const int reach = halo > 0.0 ? int(std::ceil(radius + 0.5)) : 0;
    struct Tap
    {
        int dx = 0;
        int dy = 0;
        float weight = 0.0f;
    };
    QVector<Tap> taps;
    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            const double weight = std::clamp(radius + 0.5 - std::hypot(double(dx), double(dy)), 0.0, 1.0);
            if (weight > 0.0)
                taps.append(Tap{dx, dy, float(weight)});
        }
    }
    QVector<float> alpha(qsizetype(width_texels) * qsizetype(height_texels), 0.0f);
    for (int y = 0; y < height_texels; ++y)
    {
        const QRgb *line = reinterpret_cast<const QRgb *>(coverage.constScanLine(y));
        for (int x = 0; x < width_texels; ++x)
            alpha[qsizetype(y) * width_texels + x] = float(qAlpha(line[x])) / 255.0f;
    }
    const PremultipliedColor text_color = premultipliedColor(text_argb);
    const PremultipliedColor halo_color = premultipliedColor(halo_argb);
    QImage run(width_texels, height_texels, QImage::Format_RGBA8888_Premultiplied);
    for (int y = 0; y < height_texels; ++y)
    {
        uchar *target = run.scanLine(y);
        for (int x = 0; x < width_texels; ++x)
        {
            const float text_alpha = alpha.at(qsizetype(y) * width_texels + x);
            float halo_alpha = 0.0f;
            for (const Tap &tap : taps)
            {
                const int sx = x + tap.dx;
                const int sy = y + tap.dy;
                if (sx < 0 || sy < 0 || sx >= width_texels || sy >= height_texels)
                    continue;
                halo_alpha = std::max(halo_alpha,
                                      alpha.at(qsizetype(sy) * width_texels + sx) * tap.weight);
                if (halo_alpha >= 1.0f)
                    break;
            }
            // Text over halo, premultiplied "source over".
            const float text_a = text_alpha * text_color.a;
            const float keep = 1.0f - text_a;
            uchar *pixel = target + x * 4;
            pixel[0] = unitToByte(text_color.r * text_alpha + halo_color.r * halo_alpha * keep);
            pixel[1] = unitToByte(text_color.g * text_alpha + halo_color.g * halo_alpha * keep);
            pixel[2] = unitToByte(text_color.b * text_alpha + halo_color.b * halo_alpha * keep);
            pixel[3] = unitToByte(text_a + halo_color.a * halo_alpha * keep);
        }
    }
    MapBgfxHudSprite sprite = store(run, logical);
    sprite.origin_x = margin;
    sprite.origin_y = margin;
    if (sprite.valid)
        this->sprites.insert(key, sprite);
    return sprite;
}
