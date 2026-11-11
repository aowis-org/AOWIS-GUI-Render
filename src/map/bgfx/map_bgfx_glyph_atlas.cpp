#include "map/bgfx/map_bgfx_glyph_atlas.h"

#include <QGlyphRun>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace
{
// Transparent (= far outside) gap around every entry, so linear filtering
// never reads a neighbour.
constexpr int EntryPadding = 1;
// Shaped lines kept before the memo is dropped (values of a running
// simulation produce new texts every time step).
constexpr qsizetype MaximumCachedLines = 20000;
constexpr double Infinity = 1e20;

// 1D squared Euclidean distance transform of f (Felzenszwalb & Huttenlocher
// 2012) into d; v and z are scratch of n and n + 1 entries.
void distanceTransform1d(const double *f, double *d, int n, int *v, double *z)
{
    int k = 0;
    v[0] = 0;
    z[0] = -Infinity;
    z[1] = Infinity;
    for (int q = 1; q < n; ++q)
    {
        double s = ((f[q] + double(q) * double(q)) - (f[v[k]] + double(v[k]) * double(v[k])))
            / (2.0 * double(q - v[k]));
        while (k > 0 && s <= z[k])
        {
            --k;
            s = ((f[q] + double(q) * double(q)) - (f[v[k]] + double(v[k]) * double(v[k])))
                / (2.0 * double(q - v[k]));
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = Infinity;
    }
    k = 0;
    for (int q = 0; q < n; ++q)
    {
        while (z[k + 1] < double(q))
            ++k;
        const double offset = double(q - v[k]);
        d[q] = offset * offset + f[v[k]];
    }
}

// 2D squared distance of every cell to the nearest cell where grid is 0
// (grid: 0 at the sources, Infinity elsewhere), in place.
void distanceTransform2d(QVector<double> *grid, int width, int height)
{
    const int extent = std::max(width, height);
    QVector<double> f(extent);
    QVector<double> d(extent);
    QVector<int> v(extent);
    QVector<double> z(extent + 1);
    double *cells = grid->data();
    for (int x = 0; x < width; ++x)
    {
        for (int y = 0; y < height; ++y)
            f[y] = cells[qsizetype(y) * width + x];
        distanceTransform1d(f.constData(), d.data(), height, v.data(), z.data());
        for (int y = 0; y < height; ++y)
            cells[qsizetype(y) * width + x] = d[y];
    }
    for (int y = 0; y < height; ++y)
    {
        double *row = cells + qsizetype(y) * width;
        std::copy(row, row + width, f.begin());
        distanceTransform1d(f.constData(), d.data(), width, v.data(), z.data());
        std::copy(d.cbegin(), d.cbegin() + width, row);
    }
}
}

MapBgfxGlyphAtlas::MapBgfxGlyphAtlas(int atlas_extent)
    : atlas_image(std::max(64, atlas_extent), std::max(64, atlas_extent), QImage::Format_Grayscale8)
{
    reset();
}

void MapBgfxGlyphAtlas::clear()
{
    reset();
}

void MapBgfxGlyphAtlas::reset()
{
    this->full_flag = false;
    this->atlas_image.fill(0);
    this->glyphs.clear();
    this->shelf_x = 0;
    this->shelf_y = 0;
    this->shelf_height = 0;
    this->dirty_rect = this->atlas_image.rect();
    ++this->atlas_generation;
}

bool MapBgfxGlyphAtlas::allocate(int width, int height, QPoint *position)
{
    const int extent_x = this->atlas_image.width();
    const int extent_y = this->atlas_image.height();
    if (width > extent_x || height > extent_y)
        return false;
    if (this->shelf_x + width > extent_x)
    {
        this->shelf_y += this->shelf_height;
        this->shelf_x = 0;
        this->shelf_height = 0;
    }
    if (this->shelf_y + height > extent_y)
        return false;
    *position = QPoint(this->shelf_x, this->shelf_y);
    this->shelf_x += width;
    this->shelf_height = std::max(this->shelf_height, height);
    return true;
}

quint32 MapBgfxGlyphAtlas::fontId(const QRawFont &font)
{
    // Identity without the pixel size: one entry per glyph for every size.
    const QString identity = font.familyName() + QLatin1Char('|') + font.styleName()
        + QLatin1Char('|') + QString::number(int(font.weight())) + QLatin1Char('|')
        + QString::number(int(font.style()));
    const QHash<QString, quint32>::const_iterator found = this->font_id_by_identity.constFind(identity);
    if (found != this->font_id_by_identity.cend())
        return found.value();
    const quint32 id = quint32(this->fonts.size());
    FontSource source;
    source.font = font;
    source.font.setPixelSize(BasePixelSize * double(Supersample));
    this->fonts.append(source);
    this->font_id_by_identity.insert(identity, id);
    return id;
}

const QVector<MapBgfxGlyphAtlas::LineGlyph> &MapBgfxGlyphAtlas::layoutLine(const QString &line,
                                                                           const QFont &font)
{
    const QString cache_key = font.key() + QChar(0x1f) + line;
    const QHash<QString, QVector<LineGlyph>>::const_iterator found =
        this->line_layouts.constFind(cache_key);
    if (found != this->line_layouts.cend())
        return found.value();
    if (this->line_layouts.size() >= MaximumCachedLines)
        this->line_layouts.clear();

    QVector<LineGlyph> glyphs_of_line;
    if (!line.isEmpty())
    {
        QTextLayout layout(line, font);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        // Absolute left: right-to-left runs are still laid out from x = 0.
        option.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
        layout.setTextOption(option);
        layout.beginLayout();
        QTextLine text_line = layout.createLine();
        if (text_line.isValid())
        {
            text_line.setLineWidth(1.0e6);
            text_line.setPosition(QPointF(0.0, 0.0));
        }
        layout.endLayout();
        const QList<QGlyphRun> runs = layout.glyphRuns();
        for (const QGlyphRun &run : runs)
        {
            const QRawFont raw = run.rawFont();
            if (!raw.isValid() || !(raw.pixelSize() > 0.0))
                continue;
            const quint64 font_key = quint64(fontId(raw)) << 32;
            const double scale = raw.pixelSize() / BasePixelSize;
            const QVector<quint32> indexes = run.glyphIndexes();
            const QVector<QPointF> positions = run.positions();
            const qsizetype count = std::min(indexes.size(), positions.size());
            for (qsizetype index = 0; index < count; ++index)
            {
                LineGlyph glyph;
                glyph.key = font_key | quint64(indexes.at(index));
                glyph.pen = positions.at(index);
                glyph.scale = scale;
                glyphs_of_line.append(glyph);
            }
        }
    }
    return this->line_layouts.insert(cache_key, glyphs_of_line).value();
}

bool MapBgfxGlyphAtlas::emitLine(MapBgfxHudCanvas *canvas, const QString &line,
                                 const QFont &font, double x, double y, double scale,
                                 quint32 tint_argb, int *create_budget)
{
    if (canvas == nullptr || line.isEmpty())
        return true;
    // The layout is copied: creating glyphs never touches line_layouts, but
    // a caller's next layoutLine() may rehash it.
    const QVector<LineGlyph> glyphs_of_line = layoutLine(line, font);
    for (const LineGlyph &line_glyph : glyphs_of_line)
    {
        if (contains(line_glyph.key))
            continue;
        if (create_budget == nullptr || *create_budget <= 0)
            return false;
        if (!glyph(line_glyph.key, true).valid)
            return false;
        --*create_budget;
    }
    for (const LineGlyph &line_glyph : glyphs_of_line)
    {
        const Glyph entry = glyph(line_glyph.key, false);
        if (!entry.valid || entry.empty)
            continue;
        const double glyph_scale = line_glyph.scale * scale;
        const double pen_x = x + line_glyph.pen.x() * scale;
        const double pen_y = y + line_glyph.pen.y() * scale;
        const MapBgfxHudRect quad{pen_x + entry.x0 * glyph_scale, pen_y + entry.y0 * glyph_scale,
                                  (entry.x1 - entry.x0) * glyph_scale,
                                  (entry.y1 - entry.y0) * glyph_scale};
        MapBgfxHudSprite sprite;
        sprite.valid = true;
        sprite.u0 = entry.u0;
        sprite.v0 = entry.v0;
        sprite.u1 = entry.u1;
        sprite.v1 = entry.v1;
        sprite.width = quad.width;
        sprite.height = quad.height;
        canvas->sprite(quad, sprite, tint_argb);
    }
    return true;
}

MapBgfxGlyphAtlas::Glyph MapBgfxGlyphAtlas::glyph(quint64 key, bool allow_create)
{
    const QHash<quint64, Glyph>::const_iterator found = this->glyphs.constFind(key);
    if (found != this->glyphs.cend())
        return found.value();
    if (!allow_create)
        return Glyph();
    const Glyph created = createGlyph(key);
    if (created.valid)
        this->glyphs.insert(key, created);
    else
        this->full_flag = true;
    return created;
}

MapBgfxGlyphAtlas::Glyph MapBgfxGlyphAtlas::createGlyph(quint64 key)
{
    Glyph result;
    const quint32 font_id = quint32(key >> 32);
    if (font_id >= quint32(this->fonts.size()))
        return result;
    const QRawFont &font = this->fonts.at(qsizetype(font_id)).font;
    const QPainterPath path = font.pathForGlyph(quint32(key & 0xffffffffu));
    const QRectF bounds = path.boundingRect();
    if (path.isEmpty() || !(bounds.width() > 0.0) || !(bounds.height() > 0.0))
    {
        result.valid = true;
        result.empty = true;
        return result;
    }

    // Base grid cell = Supersample high-resolution pixels; the outline plus
    // SpreadPx base px on every side.
    const int s = Supersample;
    const int left = int(std::floor(bounds.left() / double(s))) - SpreadPx;
    const int top = int(std::floor(bounds.top() / double(s))) - SpreadPx;
    const int right = int(std::ceil(bounds.right() / double(s))) + SpreadPx;
    const int bottom = int(std::ceil(bounds.bottom() / double(s))) + SpreadPx;
    const int width = right - left;
    const int height = bottom - top;
    QPoint position;
    if (!allocate(width + 2 * EntryPadding, height + 2 * EntryPadding, &position))
        return result;

    // 1. Coverage at Supersample x, thresholded to inside / outside.
    const int hi_width = width * s;
    const int hi_height = height * s;
    QImage mask(hi_width, hi_height, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(-double(left * s), -double(top * s));
        painter.fillPath(path, Qt::white);
    }

    // 2. Distance of every pixel to the nearest pixel of the other kind.
    const qsizetype cells = qsizetype(hi_width) * qsizetype(hi_height);
    QVector<double> to_inside(cells);
    QVector<double> to_outside(cells);
    for (int y = 0; y < hi_height; ++y)
    {
        const QRgb *row = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        for (int x = 0; x < hi_width; ++x)
        {
            const bool inside = qAlpha(row[x]) >= 128;
            to_inside[qsizetype(y) * hi_width + x] = inside ? 0.0 : Infinity;
            to_outside[qsizetype(y) * hi_width + x] = inside ? Infinity : 0.0;
        }
    }
    distanceTransform2d(&to_inside, hi_width, hi_height);
    distanceTransform2d(&to_outside, hi_width, hi_height);

    // 3. Signed distance (inside positive; the outline lies half a pixel
    //    beyond the last pixel of a kind), averaged per base cell, encoded.
    const float spread = float(SpreadPx);
    const float samples = float(s * s);
    for (int cy = 0; cy < height; ++cy)
    {
        uchar *target = this->atlas_image.scanLine(position.y() + EntryPadding + cy)
            + position.x() + EntryPadding;
        for (int cx = 0; cx < width; ++cx)
        {
            double sum = 0.0;
            for (int sy = 0; sy < s; ++sy)
            {
                const qsizetype row = qsizetype(cy * s + sy) * hi_width;
                for (int sx = 0; sx < s; ++sx)
                {
                    const qsizetype cell = row + cx * s + sx;
                    // Squared distances to the nearest inside / outside pixel.
                    const double outside = to_inside.at(cell);
                    const double inside = to_outside.at(cell);
                    sum += outside > 0.0 ? -(std::sqrt(outside) - 0.5) : std::sqrt(inside) - 0.5;
                }
            }
            const float distance = float(sum / double(samples) / double(s));
            const float encoded = std::clamp(0.5f + distance / (2.0f * spread), 0.0f, 1.0f);
            target[cx] = uchar(std::lround(encoded * 255.0f));
        }
    }
    const QRect changed(position.x() + EntryPadding, position.y() + EntryPadding, width, height);
    this->dirty_rect = this->dirty_rect.isNull() ? changed : this->dirty_rect.united(changed);

    const double extent_x = double(this->atlas_image.width());
    const double extent_y = double(this->atlas_image.height());
    result.valid = true;
    result.u0 = float(double(changed.x()) / extent_x);
    result.v0 = float(double(changed.y()) / extent_y);
    result.u1 = float(double(changed.x() + width) / extent_x);
    result.v1 = float(double(changed.y() + height) / extent_y);
    result.x0 = double(left);
    result.y0 = double(top);
    result.x1 = double(right);
    result.y1 = double(bottom);
    return result;
}
