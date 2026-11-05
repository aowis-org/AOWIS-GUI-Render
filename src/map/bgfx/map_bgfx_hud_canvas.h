#ifndef MAP_BGFX_HUD_CANVAS_H
#define MAP_BGFX_HUD_CANVAS_H

#include <QtGlobal>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

// Immediate-mode draw list for the native bgfx HUD (slice 6).
//
// Everything is in logical pixels (x right, y down, origin top-left) and is
// drawn with ONE texture, the HUD atlas (MapBgfxHudTextCache): text runs and
// images are atlas sprites, solid shapes sample the atlas' white texel. One
// indexed triangle list per frame, one draw call (MapBgfxHudGpu).
//
// Colours are passed as QRgb-style 0xAARRGGBB and stored premultiplied as
// ABGR (bytes R,G,B,A), because the HUD shader multiplies the premultiplied
// atlas texel by the vertex colour and blends One / OneMinusSrcAlpha. Shapes
// are plain triangles; their edges are antialiased by the view's MSAA.
//
// Indices are 32-bit: the editor overlay (Phase 2) draws one icon quad per
// visible network node through the same kit, far beyond 65535 vertices.
struct MapBgfxHudRect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool contains(double px, double py) const
    {
        return px >= this->x && py >= this->y
            && px < this->x + this->width && py < this->y + this->height;
    }
    double right() const { return this->x + this->width; }
    double bottom() const { return this->y + this->height; }
};

// An atlas region and the logical size it is drawn at. origin_x/origin_y:
// where the layout position lies inside the sprite (text runs carry a
// padding column for glyph overhang; layout uses the advance width).
struct MapBgfxHudSprite
{
    bool valid = false;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    double width = 0.0;
    double height = 0.0;
    double origin_x = 0.0;
    double origin_y = 0.0;
};

struct MapBgfxHudVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    quint32 abgr = 0;
};

struct MapBgfxHudPoint
{
    double x = 0.0;
    double y = 0.0;
};

class MapBgfxHudCanvas
{
public:
    static constexpr double Pi = 3.14159265358979323846;

    // Premultiplied ABGR from 0xAARRGGBB.
    static quint32 premultipliedAbgr(quint32 argb)
    {
        const quint32 alpha = (argb >> 24) & 0xffu;
        const quint32 red = ((argb >> 16) & 0xffu) * alpha / 255u;
        const quint32 green = ((argb >> 8) & 0xffu) * alpha / 255u;
        const quint32 blue = (argb & 0xffu) * alpha / 255u;
        return (alpha << 24) | (blue << 16) | (green << 8) | red;
    }

    void clear()
    {
        this->vertex_list.clear();
        this->index_list.clear();
    }

    // Atlas coordinate of an opaque white texel (used for solid shapes).
    void setWhiteTexel(float u, float v)
    {
        this->white_u = u;
        this->white_v = v;
    }

    void fillRect(const MapBgfxHudRect &rect, quint32 argb)
    {
        if (!(rect.width > 0.0) || !(rect.height > 0.0))
            return;
        QVector<MapBgfxHudPoint> corners;
        corners.append({rect.x, rect.y});
        corners.append({rect.right(), rect.y});
        corners.append({rect.right(), rect.bottom()});
        corners.append({rect.x, rect.bottom()});
        fillConvexPolygon(corners, argb);
    }

    void fillRoundedRect(const MapBgfxHudRect &rect, double radius, quint32 argb)
    {
        fillConvexPolygon(roundedRectOutline(rect, radius), argb);
    }

    // Border of width line_width inside rect.
    void strokeRoundedRect(const MapBgfxHudRect &rect, double radius, double line_width,
                           quint32 argb)
    {
        if (!(line_width > 0.0))
            return;
        const MapBgfxHudRect inner{rect.x + line_width, rect.y + line_width,
                                   rect.width - 2.0 * line_width, rect.height - 2.0 * line_width};
        if (!(inner.width > 0.0) || !(inner.height > 0.0))
        {
            fillRoundedRect(rect, radius, argb);
            return;
        }
        // Both outlines need the same number of points (fillRing pairs
        // them): when either corner is rounded, both get corner arcs, the
        // inner one possibly of radius 0 (a wide pen on a small radius).
        const bool rounded = radius > 0.0;
        fillRing(roundedRectOutline(rect, radius, rounded),
                 roundedRectOutline(inner, std::max(0.0, radius - line_width), rounded), argb);
    }

    // Circles and lines are emitted straight into the draw list (no
    // temporary outlines): the editor overlay draws thousands per frame.
    void fillCircle(double center_x, double center_y, double radius, quint32 argb)
    {
        if (!(radius > 0.0))
            return;
        const int segments = circleSegments(radius);
        const quint32 color = premultipliedAbgr(argb);
        const quint32 base = quint32(this->vertex_list.size());
        appendCirclePoints(center_x, center_y, radius, segments, color);
        for (int index = 1; index + 1 < segments; ++index)
        {
            this->index_list.append(base);
            this->index_list.append(base + quint32(index));
            this->index_list.append(base + quint32(index) + 1u);
        }
    }

    // Ring between radius and radius - line_width; both outlines use the
    // outer one's segment count (unequal counts used to fill the whole disc).
    void strokeCircle(double center_x, double center_y, double radius, double line_width,
                      quint32 argb)
    {
        if (!(radius > 0.0) || !(line_width > 0.0))
            return;
        const double inner = std::max(0.0, radius - line_width);
        if (!(inner > 0.0))
        {
            fillCircle(center_x, center_y, radius, argb);
            return;
        }
        const int segments = circleSegments(radius);
        const quint32 color = premultipliedAbgr(argb);
        const quint32 base = quint32(this->vertex_list.size());
        const quint32 count = quint32(segments);
        appendCirclePoints(center_x, center_y, radius, segments, color);
        appendCirclePoints(center_x, center_y, inner, segments, color);
        for (quint32 index = 0; index < count; ++index)
        {
            const quint32 next = (index + 1u) % count;
            const quint32 quad[6] = {base + index, base + next, base + count + next,
                                     base + index, base + count + next, base + count + index};
            for (const quint32 vertex : quad)
                this->index_list.append(vertex);
        }
    }

    // Straight line of width line_width (square ends).
    void line(double x0, double y0, double x1, double y1, double line_width, quint32 argb)
    {
        const double dx = x1 - x0;
        const double dy = y1 - y0;
        const double length = std::hypot(dx, dy);
        if (!(length > 0.0) || !(line_width > 0.0))
            return;
        const double nx = -dy / length * 0.5 * line_width;
        const double ny = dx / length * 0.5 * line_width;
        const quint32 color = premultipliedAbgr(argb);
        const quint32 base = quint32(this->vertex_list.size());
        this->vertex_list.append({float(x0 + nx), float(y0 + ny), this->white_u, this->white_v, color});
        this->vertex_list.append({float(x1 + nx), float(y1 + ny), this->white_u, this->white_v, color});
        this->vertex_list.append({float(x1 - nx), float(y1 - ny), this->white_u, this->white_v, color});
        this->vertex_list.append({float(x0 - nx), float(y0 - ny), this->white_u, this->white_v, color});
        appendQuadIndices(base);
    }

    // Textured convex quad: corners in order, each with its atlas uv (a
    // sprite drawn rotated or partially, e.g. a dash pattern along a line).
    void spriteQuad(const MapBgfxHudPoint (&corners)[4], const float (&u)[4], const float (&v)[4],
                    quint32 tint_argb)
    {
        const quint32 color = premultipliedAbgr(tint_argb);
        const quint32 base = quint32(this->vertex_list.size());
        for (int index = 0; index < 4; ++index)
        {
            this->vertex_list.append({float(corners[index].x), float(corners[index].y),
                                      u[index], v[index], color});
        }
        appendQuadIndices(base);
    }

    void fillTriangle(const MapBgfxHudPoint &a, const MapBgfxHudPoint &b,
                      const MapBgfxHudPoint &c, quint32 argb)
    {
        QVector<MapBgfxHudPoint> corners;
        corners.append(a);
        corners.append(b);
        corners.append(c);
        fillConvexPolygon(corners, argb);
    }

    // Rounded rect filled with a left-to-right gradient through evenly
    // spaced colour stops (0xAARRGGBB), interpolated linearly per channel
    // like QLinearGradient.
    void fillHorizontalGradient(const MapBgfxHudRect &rect, double radius,
                                const QVector<quint32> &stops)
    {
        QVector<double> positions;
        for (qsizetype index = 0; index < stops.size(); ++index)
            positions.append(stops.size() > 1 ? double(index) / double(stops.size() - 1) : 0.0);
        fillLinearGradient(rect, radius, false, rect.x, rect.right(), positions, stops);
    }

    // Rounded rect filled with a horizontal (vertical=false: along x) or
    // vertical (along y) linear gradient running from gradient_start to
    // gradient_end on that axis, with stops at positions 0..1 (ascending)
    // and pad spread outside, like QLinearGradient. The shape is cut into
    // one slab per pair of neighbouring stops (plus the padded ends);
    // within a slab the colour is an affine function of the axis, so
    // per-vertex colours interpolate exactly, also on rounded corners.
    void fillLinearGradient(const MapBgfxHudRect &rect, double radius, bool vertical,
                            double gradient_start, double gradient_end,
                            const QVector<double> &positions, const QVector<quint32> &stops)
    {
        if (stops.isEmpty() || positions.size() != stops.size())
            return;
        if (stops.size() == 1 || !(std::fabs(gradient_end - gradient_start) > 0.0))
        {
            fillRoundedRect(rect, radius, stops.first());
            return;
        }
        QVector<MapBgfxHudPoint> outline = roundedRectOutline(rect, radius);
        if (outline.size() < 3)
            return;
        // Clip in a frame where the gradient axis is x.
        if (vertical)
        {
            for (MapBgfxHudPoint &point : outline)
                std::swap(point.x, point.y);
        }
        const double length = gradient_end - gradient_start;
        const std::function<double(double)> axis_at = [gradient_start, length](double position)
        {
            return gradient_start + length * position;
        };
        // Slab borders: -inf, stop positions, +inf (pad spread).
        QVector<double> borders;
        borders.append(-1e30);
        for (const double position : positions)
            borders.append(axis_at(position));
        borders.append(1e30);
        if (length < 0.0)
            std::sort(borders.begin(), borders.end());
        const std::function<quint32(double)> color_at = [&](double axis_value)
        {
            const double t = (axis_value - gradient_start) / length;
            if (t <= positions.first())
                return stops.first();
            if (t >= positions.last())
                return stops.last();
            for (qsizetype index = 0; index + 1 < positions.size(); ++index)
            {
                const double a = positions.at(index);
                const double b = positions.at(index + 1);
                if (t >= a && t <= b)
                    return interpolateArgb(stops.at(index), stops.at(index + 1),
                                           b > a ? (t - a) / (b - a) : 0.0);
            }
            return stops.last();
        };
        for (qsizetype slab = 0; slab + 1 < borders.size(); ++slab)
        {
            const double low = borders.at(slab);
            const double high = borders.at(slab + 1);
            if (!(high > low))
                continue;
            QVector<MapBgfxHudPoint> piece = clipToVerticalSlab(outline, low, high);
            if (piece.size() < 3)
                continue;
            QVector<quint32> colors;
            colors.reserve(piece.size());
            for (const MapBgfxHudPoint &point : piece)
                colors.append(premultipliedAbgr(color_at(point.x)));
            if (vertical)
            {
                for (MapBgfxHudPoint &point : piece)
                    std::swap(point.x, point.y);
            }
            fillConvexPolygonColored(piece, colors);
        }
    }

    // Convex polygon with one colour per vertex (0xAARRGGBB each).
    void fillConvexPolygonArgb(const QVector<MapBgfxHudPoint> &points,
                               const QVector<quint32> &argb_colors)
    {
        if (points.size() != argb_colors.size())
            return;
        QVector<quint32> colors;
        colors.reserve(argb_colors.size());
        for (const quint32 argb : argb_colors)
            colors.append(premultipliedAbgr(argb));
        fillConvexPolygonColored(points, colors);
    }

    // Any convex polygon in one colour.
    void fillConvex(const QVector<MapBgfxHudPoint> &points, quint32 argb)
    {
        fillConvexPolygon(points, argb);
    }

    // Per-channel linear interpolation of two 0xAARRGGBB colours, rounded
    // like networkSymbologyInterpolatedRampColor() (qRound per channel).
    static quint32 interpolateArgb(quint32 from, quint32 to, double fraction)
    {
        const std::function<quint32(int)> channel = [from, to, fraction](int shift)
        {
            const double a = double((from >> shift) & 0xffu);
            const double b = double((to >> shift) & 0xffu);
            return quint32(std::clamp(qRound(a + (b - a) * fraction), 0, 255)) << shift;
        };
        return channel(24) | channel(16) | channel(8) | channel(0);
    }

    // Atlas sprite stretched to rect, tinted (white = original colours).
    void sprite(const MapBgfxHudRect &rect, const MapBgfxHudSprite &sprite, quint32 tint_argb)
    {
        if (!sprite.valid || !(rect.width > 0.0) || !(rect.height > 0.0))
            return;
        const quint32 color = premultipliedAbgr(tint_argb);
        const quint32 base = quint32(this->vertex_list.size());
        this->vertex_list.append({float(rect.x), float(rect.y), sprite.u0, sprite.v0, color});
        this->vertex_list.append({float(rect.right()), float(rect.y), sprite.u1, sprite.v0, color});
        this->vertex_list.append({float(rect.right()), float(rect.bottom()), sprite.u1, sprite.v1, color});
        this->vertex_list.append({float(rect.x), float(rect.bottom()), sprite.u0, sprite.v1, color});
        appendQuadIndices(base);
    }

    // Sprite at its own logical size, its layout origin at (x, y) (for text:
    // the left end of the advance and the top of the line).
    void sprite(double x, double y, const MapBgfxHudSprite &atlas_sprite, quint32 tint_argb)
    {
        sprite(MapBgfxHudRect{x - atlas_sprite.origin_x, y - atlas_sprite.origin_y,
                              atlas_sprite.width, atlas_sprite.height},
               atlas_sprite, tint_argb);
    }

    const QVector<MapBgfxHudVertex> &vertices() const { return this->vertex_list; }
    const QVector<quint32> &indices() const { return this->index_list; }
    // Per-frame vertex budget below bgfx's transient vertex buffer
    // (MapBgfxSurface sets Init::limits.maxTransientVbSize to 16 MiB = 838860
    // HUD vertices; the HUD and the editor overlay share it with imagery
    // quads). A larger list is reported and not drawn.
    static constexpr qsizetype MaximumVertices = 600000;
    bool overflowed() const { return this->vertex_list.size() > MaximumVertices; }

private:
    // About one segment per 2 px of circumference, 12 to 64.
    static int circleSegments(double radius)
    {
        return std::clamp(int(std::ceil(radius * 3.0)), 12, 64);
    }

    // Circle points from angle 0 in steps of 2 pi / segments, by rotating
    // one vector: no trigonometry per point.
    void appendCirclePoints(double cx, double cy, double radius, int segments, quint32 color)
    {
        const double step = 2.0 * Pi / double(segments);
        const double cosine = std::cos(step);
        const double sine = std::sin(step);
        double x = radius;
        double y = 0.0;
        for (int index = 0; index < segments; ++index)
        {
            this->vertex_list.append({float(cx + x), float(cy + y), this->white_u, this->white_v, color});
            const double rotated_x = x * cosine - y * sine;
            y = x * sine + y * cosine;
            x = rotated_x;
        }
    }

    // keep_arcs: emit the corner arcs even for radius 0 (coincident
    // points), so two outlines of one ring always have equal point counts.
    static QVector<MapBgfxHudPoint> roundedRectOutline(const MapBgfxHudRect &rect, double radius,
                                                       bool keep_arcs = false)
    {
        QVector<MapBgfxHudPoint> points;
        if (!(rect.width > 0.0) || !(rect.height > 0.0))
            return points;
        const double r = std::clamp(radius, 0.0, 0.5 * std::min(rect.width, rect.height));
        if (r <= 0.0 && !keep_arcs)
        {
            points.append({rect.x, rect.y});
            points.append({rect.right(), rect.y});
            points.append({rect.right(), rect.bottom()});
            points.append({rect.x, rect.bottom()});
            return points;
        }
        constexpr int CornerSegments = 6;
        // Corner centres clockwise from top-left, each arc spanning 90 deg.
        const double centers[4][2] = {
            {rect.x + r, rect.y + r}, {rect.right() - r, rect.y + r},
            {rect.right() - r, rect.bottom() - r}, {rect.x + r, rect.bottom() - r}};
        const double start_angles[4] = {Pi, 1.5 * Pi, 0.0, 0.5 * Pi};
        for (int corner = 0; corner < 4; ++corner)
        {
            for (int step = 0; step <= CornerSegments; ++step)
            {
                const double angle = start_angles[corner]
                    + 0.5 * Pi * double(step) / double(CornerSegments);
                points.append({centers[corner][0] + r * std::cos(angle),
                               centers[corner][1] + r * std::sin(angle)});
            }
        }
        return points;
    }

    void fillConvexPolygon(const QVector<MapBgfxHudPoint> &points, quint32 argb)
    {
        if (points.size() < 3)
            return;
        const quint32 color = premultipliedAbgr(argb);
        const quint32 base = quint32(this->vertex_list.size());
        for (const MapBgfxHudPoint &point : points)
            this->vertex_list.append({float(point.x), float(point.y), this->white_u, this->white_v, color});
        for (qsizetype index = 1; index + 1 < points.size(); ++index)
        {
            this->index_list.append(base);
            this->index_list.append(base + quint32(index));
            this->index_list.append(base + quint32(index) + 1u);
        }
    }

    // Convex polygon with one colour per vertex (already premultiplied ABGR).
    void fillConvexPolygonColored(const QVector<MapBgfxHudPoint> &points,
                                  const QVector<quint32> &abgr_colors)
    {
        if (points.size() < 3 || points.size() != abgr_colors.size())
            return;
        const quint32 base = quint32(this->vertex_list.size());
        for (qsizetype index = 0; index < points.size(); ++index)
        {
            const MapBgfxHudPoint &point = points.at(index);
            this->vertex_list.append({float(point.x), float(point.y), this->white_u,
                                      this->white_v, abgr_colors.at(index)});
        }
        for (qsizetype index = 1; index + 1 < points.size(); ++index)
        {
            this->index_list.append(base);
            this->index_list.append(base + quint32(index));
            this->index_list.append(base + quint32(index) + 1u);
        }
    }

    // Sutherland-Hodgman clip of a convex polygon to left <= x <= right; the
    // result is convex again (a fan triangulates it).
    static QVector<MapBgfxHudPoint> clipToVerticalSlab(const QVector<MapBgfxHudPoint> &polygon,
                                                       double left, double right)
    {
        const std::function<QVector<MapBgfxHudPoint>(const QVector<MapBgfxHudPoint> &, double, bool)>
            clip = [](const QVector<MapBgfxHudPoint> &input, double edge, bool keep_greater)
        {
            QVector<MapBgfxHudPoint> output;
            if (input.isEmpty())
                return output;
            const std::function<bool(const MapBgfxHudPoint &)> inside =
                [edge, keep_greater](const MapBgfxHudPoint &point)
            {
                return keep_greater ? point.x >= edge : point.x <= edge;
            };
            MapBgfxHudPoint previous = input.last();
            for (const MapBgfxHudPoint &current : input)
            {
                const bool current_inside = inside(current);
                const bool previous_inside = inside(previous);
                if (current_inside != previous_inside)
                {
                    // Edge crossing: x == edge exactly, y interpolated.
                    const double t = (edge - previous.x) / (current.x - previous.x);
                    output.append({edge, previous.y + (current.y - previous.y) * t});
                }
                if (current_inside)
                    output.append(current);
                previous = current;
            }
            return output;
        };
        return clip(clip(polygon, left, true), right, false);
    }

    // Area between two outlines with the same number of points (same shape
    // generator, inner one inset).
    void fillRing(const QVector<MapBgfxHudPoint> &outer, const QVector<MapBgfxHudPoint> &inner,
                  quint32 argb)
    {
        if (outer.size() < 3 || outer.size() != inner.size())
        {
            fillConvexPolygon(outer, argb);
            return;
        }
        const quint32 color = premultipliedAbgr(argb);
        const quint32 base = quint32(this->vertex_list.size());
        const quint32 count = quint32(outer.size());
        for (const MapBgfxHudPoint &point : outer)
            this->vertex_list.append({float(point.x), float(point.y), this->white_u, this->white_v, color});
        for (const MapBgfxHudPoint &point : inner)
            this->vertex_list.append({float(point.x), float(point.y), this->white_u, this->white_v, color});
        for (quint32 index = 0; index < count; ++index)
        {
            const quint32 next = (index + 1u) % count;
            const quint32 quad[6] = {base + index, base + next, base + count + next,
                                     base + index, base + count + next, base + count + index};
            for (const quint32 vertex : quad)
                this->index_list.append(vertex);
        }
    }

    void appendQuadIndices(quint32 base)
    {
        const quint32 quad[6] = {base, base + 1u, base + 2u, base, base + 2u, base + 3u};
        for (const quint32 vertex : quad)
            this->index_list.append(vertex);
    }

    QVector<MapBgfxHudVertex> vertex_list;
    QVector<quint32> index_list;
    float white_u = 0.0f;
    float white_v = 0.0f;
};

#endif // MAP_BGFX_HUD_CANVAS_H
