#ifndef NETWORK_SYMBOLOGY_RENDERING_H
#define NETWORK_SYMBOLOGY_RENDERING_H

#include "network/network_symbology.h"

#include <QColor>
#include <QString>
#include <QtGlobal>

#include <array>
#include <cmath>

constexpr int NetworkSymbologyColorBucketCount = 256;
constexpr int NetworkSymbologyRampColorCount = 7;
constexpr int NetworkSymbologyPaletteCount = 9;

inline QRgb networkSymbologyDefaultColor()
{
    return qRgb(0, 0, 0);
}

inline QRgb networkSymbologyUnavailableColor()
{
    return qRgb(0, 0, 0);
}

inline QRgb networkSymbologyIconDefaultFillColor()
{
    // The atlas needs a saturated fill so the shader can distinguish the
    // colorizable body from the dark outline/details. The actual display
    // color comes from the themed overload below.
    return qRgb(46, 139, 192);
}

inline QRgb networkSymbologyIconDefaultFillColor(const QColor &)
{
    return qRgb(210, 210, 210);
}

inline QRgb networkSymbologyIconUnavailableFillColor()
{
    return qRgb(112, 120, 126);
}

inline QRgb networkSymbologyIconOutlineColor()
{
    return qRgb(17, 24, 32);
}

inline qreal networkSymbologyNodeSizeScale(int node_size_percent)
{
    return qBound<qreal>(0.5, node_size_percent / 100.0, 2.5);
}

inline qreal networkSymbologyBaseMarkerSizeForZoom(int zoom)
{
    return qBound<qreal>(10.0, 10.0 + (zoom - 16) * 10.0, 40.0);
}

inline qreal networkSymbologyMarkerSizeForZoom(int zoom, int node_size_percent)
{
    return qMax<qreal>(
        5.0,
        networkSymbologyBaseMarkerSizeForZoom(zoom)
            * networkSymbologyNodeSizeScale(node_size_percent));
}


inline qreal networkSymbologyIconSizePixels(
    NetworkSymbologySizeUnit unit, int size_px, double size_m, qreal meters_per_pixel)
{
    if (unit == NetworkSymbologySizeUnit::Meters)
    {
        if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0.0)
            return qreal(size_px);
        return qMax<qreal>(1.0, qreal(size_m) / meters_per_pixel);
    }
    return qreal(size_px);
}

inline qreal networkSymbologyJunctionDotDiameterForZoom(int zoom, int node_size_percent)
{
    const qreal base_diameter = qBound<qreal>(
        8.0, networkSymbologyBaseMarkerSizeForZoom(zoom) * 0.3, 12.0);
    return qMax<qreal>(
        4.0, base_diameter * networkSymbologyNodeSizeScale(node_size_percent));
}

inline const std::array<QColor, NetworkSymbologyRampColorCount> &networkSymbologyPaletteColors(
    NetworkSymbologyPalette palette)
{
    static const std::array<QColor, NetworkSymbologyRampColorCount> Viridis = {{
        QColor(QStringLiteral("#440154")),
        QColor(QStringLiteral("#443983")),
        QColor(QStringLiteral("#31688e")),
        QColor(QStringLiteral("#21918c")),
        QColor(QStringLiteral("#35b779")),
        QColor(QStringLiteral("#90d743")),
        QColor(QStringLiteral("#fde725"))
    }};
    static const std::array<QColor, NetworkSymbologyRampColorCount> Cividis = {{
        QColor(QStringLiteral("#00224e")),
        QColor(QStringLiteral("#2a3f6d")),
        QColor(QStringLiteral("#575d6d")),
        QColor(QStringLiteral("#7d7c78")),
        QColor(QStringLiteral("#a59c74")),
        QColor(QStringLiteral("#d2c060")),
        QColor(QStringLiteral("#fee838"))
    }};
    static const std::array<QColor, NetworkSymbologyRampColorCount> Plasma = {{
        QColor(QStringLiteral("#0d0887")),
        QColor(QStringLiteral("#5b02a3")),
        QColor(QStringLiteral("#9a179b")),
        QColor(QStringLiteral("#cb4679")),
        QColor(QStringLiteral("#ed7953")),
        QColor(QStringLiteral("#fbad24")),
        QColor(QStringLiteral("#f0f921"))
    }};
    static const std::array<QColor, NetworkSymbologyRampColorCount> Inferno = {{
        QColor(QStringLiteral("#000004")),
        QColor(QStringLiteral("#320a5e")),
        QColor(QStringLiteral("#781c6d")),
        QColor(QStringLiteral("#bb3754")),
        QColor(QStringLiteral("#ed6925")),
        QColor(QStringLiteral("#fbb61a")),
        QColor(QStringLiteral("#fcffa4"))
    }};
    static const std::array<QColor, NetworkSymbologyRampColorCount> Magma = {{
        QColor(QStringLiteral("#000004")),
        QColor(QStringLiteral("#2c115f")),
        QColor(QStringLiteral("#721f81")),
        QColor(QStringLiteral("#b73779")),
        QColor(QStringLiteral("#f1605d")),
        QColor(QStringLiteral("#feb078")),
        QColor(QStringLiteral("#fcfdbf"))
    }};
    // Fabio Crameri Scientific Colour Maps 8.0.1, batlow sampled at seven
    // evenly spaced positions. https://doi.org/10.5281/zenodo.1243862
    static const std::array<QColor, NetworkSymbologyRampColorCount> Batlow = {{
        QColor(QStringLiteral("#011959")),
        QColor(QStringLiteral("#144d62")),
        QColor(QStringLiteral("#3c6d56")),
        QColor(QStringLiteral("#828231")),
        QColor(QStringLiteral("#d29343")),
        QColor(QStringLiteral("#fdac9e")),
        QColor(QStringLiteral("#faccfa"))
    }};
    static const std::array<QColor, NetworkSymbologyRampColorCount> Turbo = {{
        QColor(QStringLiteral("#30123b")),
        QColor(QStringLiteral("#4662d7")),
        QColor(QStringLiteral("#35b779")),
        QColor(QStringLiteral("#a4fc3c")),
        QColor(QStringLiteral("#f9ba38")),
        QColor(QStringLiteral("#e85d1f")),
        QColor(QStringLiteral("#7a0403"))
    }};
    static const std::array<QColor, NetworkSymbologyRampColorCount> CoolWarm = {{
        QColor(QStringLiteral("#3b4cc0")),
        QColor(QStringLiteral("#688aef")),
        QColor(QStringLiteral("#b5cdfa")),
        QColor(QStringLiteral("#dddddd")),
        QColor(QStringLiteral("#f6bfa6")),
        QColor(QStringLiteral("#dc6a4f")),
        QColor(QStringLiteral("#b40426"))
    }};
    static const std::array<QColor, NetworkSymbologyRampColorCount> RedBlue = {{
        QColor(QStringLiteral("#67001f")),
        QColor(QStringLiteral("#c94741")),
        QColor(QStringLiteral("#f7b799")),
        QColor(QStringLiteral("#f6f7f7")),
        QColor(QStringLiteral("#a7d0e4")),
        QColor(QStringLiteral("#3783bb")),
        QColor(QStringLiteral("#053061"))
    }};

    switch (palette)
    {
    case NetworkSymbologyPalette::Cividis:
        return Cividis;
    case NetworkSymbologyPalette::Plasma:
        return Plasma;
    case NetworkSymbologyPalette::Inferno:
        return Inferno;
    case NetworkSymbologyPalette::Magma:
        return Magma;
    case NetworkSymbologyPalette::Batlow:
        return Batlow;
    case NetworkSymbologyPalette::Turbo:
        return Turbo;
    case NetworkSymbologyPalette::CoolWarm:
        return CoolWarm;
    case NetworkSymbologyPalette::RedBlue:
        return RedBlue;
    case NetworkSymbologyPalette::Viridis:
        return Viridis;
    }

    return Viridis;
}

inline QString networkSymbologyPaletteName(NetworkSymbologyPalette palette)
{
    switch (palette)
    {
    case NetworkSymbologyPalette::Viridis:
        return QStringLiteral("Viridis");
    case NetworkSymbologyPalette::Cividis:
        return QStringLiteral("Cividis");
    case NetworkSymbologyPalette::Plasma:
        return QStringLiteral("Plasma");
    case NetworkSymbologyPalette::Inferno:
        return QStringLiteral("Inferno");
    case NetworkSymbologyPalette::Magma:
        return QStringLiteral("Magma");
    case NetworkSymbologyPalette::Batlow:
        return QStringLiteral("Batlow");
    case NetworkSymbologyPalette::Turbo:
        return QStringLiteral("Turbo");
    case NetworkSymbologyPalette::CoolWarm:
        return QStringLiteral("Cool/Warm");
    case NetworkSymbologyPalette::RedBlue:
        return QStringLiteral("Red/Blue");
    }

    return QStringLiteral("Viridis");
}

inline QColor networkSymbologyInterpolatedRampColor(
    double fraction,
    NetworkSymbologyPalette palette = NetworkSymbologyPalette::Viridis,
    bool flipped = false)
{
    double limited_fraction = qBound(0.0, fraction, 1.0);
    if (flipped)
        limited_fraction = 1.0 - limited_fraction;

    const std::array<QColor, NetworkSymbologyRampColorCount> &ramp_colors =
        networkSymbologyPaletteColors(palette);
    const double scaled = limited_fraction * (ramp_colors.size() - 1);
    const int left_index = qMin(int(ramp_colors.size()) - 1, int(std::floor(scaled)));
    const int right_index = qMin(int(ramp_colors.size()) - 1, left_index + 1);
    const double ratio = scaled - left_index;
    const QColor &left = ramp_colors.at(left_index);
    const QColor &right = ramp_colors.at(right_index);
    return QColor(
        qRound(left.red() + (right.red() - left.red()) * ratio),
        qRound(left.green() + (right.green() - left.green()) * ratio),
        qRound(left.blue() + (right.blue() - left.blue()) * ratio));
}

inline QColor networkSymbologyRampColor(
    double fraction,
    NetworkSymbologyPalette palette = NetworkSymbologyPalette::Viridis,
    bool flipped = false)
{
    const double limited_fraction = qBound(0.0, fraction, 1.0);
    const int bucket = qRound(limited_fraction * (NetworkSymbologyColorBucketCount - 1));
    return networkSymbologyInterpolatedRampColor(
        double(bucket) / double(NetworkSymbologyColorBucketCount - 1), palette, flipped);
}

inline QRgb networkSymbologyColor(
    double value,
    double minimum,
    double maximum,
    NetworkSymbologyPalette palette = NetworkSymbologyPalette::Viridis,
    bool flipped = false)
{
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum))
        return networkSymbologyUnavailableColor();
    if (minimum == maximum)
        return networkSymbologyRampColor(0.5, palette, flipped).rgb();
    return networkSymbologyRampColor(
        (value - minimum) / (maximum - minimum), palette, flipped).rgb();
}

#endif // NETWORK_SYMBOLOGY_RENDERING_H
