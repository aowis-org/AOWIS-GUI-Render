#include "map/render/map_icon_atlas.h"

#include "network/network_symbology_rendering.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTransform>

#include <array>

namespace
{
// Render the vector-derived atlas at 2x the previous resolution. The sprite
// geometry remains the same size on screen; linear filtering therefore has
// substantially more source coverage information at icon edges.
constexpr int AtlasHeight = 264;
constexpr int AtlasWidth = 1280;
constexpr int CellMaximumDimension = 256;
constexpr int CellGap = 4;

struct IconAsset
{
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    qreal view_width = 0.0;
    qreal view_height = 0.0;
    qreal stroke_width = 14.0;
    QPainterPath stroke_path;
    QPainterPath fill_path;
    QPainterPath detail_path;
    QRect pixel_rect;
};

QPainterPath reservoirFillPath()
{
    QPainterPath path;
    path.moveTo(5.0, 5.0);
    path.lineTo(23.0, 97.0);
    path.cubicTo(27.0, 117.0, 41.0, 133.0, 61.0, 133.0);
    path.lineTo(125.0, 133.0);
    path.cubicTo(145.0, 133.0, 159.0, 117.0, 163.0, 97.0);
    path.lineTo(181.0, 5.0);
    path.closeSubpath();
    return path;
}

QPainterPath reservoirStrokePath()
{
    QPainterPath path;
    path.moveTo(7.0, 7.0);
    path.lineTo(24.0, 96.0);
    path.cubicTo(27.0, 117.0, 41.0, 133.0, 61.0, 133.0);
    path.lineTo(125.0, 133.0);
    path.cubicTo(145.0, 133.0, 159.0, 117.0, 163.0, 97.0);
    path.lineTo(179.0, 7.0);

    QPointF current(13.640777, 19.11651);
    path.moveTo(current);
    for (int index = 0; index < 4; ++index)
    {
        path.cubicTo(
            current + QPointF(11.0, 10.0),
            current + QPointF(index == 1 ? 29.000003 : 29.0, 10.0),
            current + QPointF(index == 1 ? 40.000003 : 40.0, 0.0));
        current = path.currentPosition();
    }
    return path;
}

QPainterPath tankStrokePath()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(11.0, 22.0, 116.0, 120.0), 10.0, 10.0);
    path.moveTo(17.0, 22.0);
    path.cubicTo(23.0, 8.0, 115.0, 8.0, 121.0, 22.0);
    path.moveTo(49.0, 14.0);
    path.lineTo(89.0, 14.0);
    path.moveTo(49.0, 14.0);
    path.cubicTo(53.0, 7.0, 85.0, 7.0, 89.0, 14.0);
    path.moveTo(24.0, 142.0);
    path.lineTo(24.0, 176.0);
    path.moveTo(46.0, 142.0);
    path.lineTo(46.0, 176.0);
    path.moveTo(92.0, 142.0);
    path.lineTo(92.0, 176.0);
    path.moveTo(114.0, 142.0);
    path.lineTo(114.0, 176.0);
    path.moveTo(7.0, 176.0);
    path.lineTo(131.0, 176.0);
    return path;
}

IconAsset pumpIconAsset()
{
    IconAsset asset;
    asset.entity_type = InfrastructureEntity::Pump;
    asset.view_width = 164.0;
    asset.view_height = 122.0;
    asset.stroke_width = 12.0;

    QPainterPath body;
    body.addEllipse(QRectF(8.0, 8.0, 106.0, 106.0));
    QPainterPath outlet;
    outlet.addRoundedRect(QRectF(98.0, 38.0, 58.0, 46.0), 6.0, 6.0);
    const QPainterPath silhouette = body.united(outlet);

    asset.fill_path = silhouette;
    asset.fill_path.setFillRule(Qt::OddEvenFill);
    asset.fill_path.addEllipse(QRectF(43.0, 43.0, 36.0, 36.0));
    asset.stroke_path = silhouette;
    asset.stroke_path.addEllipse(QRectF(43.0, 43.0, 36.0, 36.0));
    return asset;
}

QPainterPath valveStrokePath()
{
    QPainterPath path;
    path.addEllipse(QRectF(8.0, 8.0, 122.0, 122.0));
    path.moveTo(25.0, 33.0);
    path.lineTo(69.0, 69.0);
    path.lineTo(113.0, 33.0);
    path.moveTo(25.0, 105.0);
    path.lineTo(69.0, 69.0);
    path.lineTo(113.0, 105.0);
    return path;
}

QPainterPath demandPointStarPath()
{
    QPainterPath path;
    path.moveTo(70.0, 4.0);
    path.lineTo(86.0, 54.0);
    path.lineTo(136.0, 70.0);
    path.lineTo(86.0, 86.0);
    path.lineTo(70.0, 136.0);
    path.lineTo(54.0, 86.0);
    path.lineTo(4.0, 70.0);
    path.lineTo(54.0, 54.0);
    path.closeSubpath();
    return path;
}

std::array<IconAsset, 5> buildAssets()
{
    std::array<IconAsset, 5> result;

    result[0].entity_type = InfrastructureEntity::Reservoir;
    result[0].view_width = 186.0;
    result[0].view_height = 138.0;
    result[0].stroke_path = reservoirStrokePath();
    result[0].fill_path = reservoirFillPath();

    result[1].entity_type = InfrastructureEntity::Tank;
    result[1].view_width = 138.0;
    result[1].view_height = 183.0;
    result[1].stroke_path = tankStrokePath();
    result[1].fill_path.addRoundedRect(QRectF(11.0, 22.0, 116.0, 120.0), 10.0, 10.0);

    result[2] = pumpIconAsset();

    result[3].entity_type = InfrastructureEntity::Valve;
    result[3].view_width = 138.0;
    result[3].view_height = 138.0;
    result[3].stroke_path = valveStrokePath();
    result[3].fill_path.addEllipse(QRectF(8.0, 8.0, 122.0, 122.0));

    result[4].entity_type = InfrastructureEntity::DemandPoint;
    result[4].view_width = 140.0;
    result[4].view_height = 140.0;
    result[4].stroke_width = 10.0;
    result[4].stroke_path = demandPointStarPath();
    result[4].fill_path = demandPointStarPath();

    int x = CellGap;
    for (IconAsset &asset : result)
    {
        const qreal maximum_dimension = qMax(asset.view_width, asset.view_height);
        const qreal scale = CellMaximumDimension / maximum_dimension;
        const int width = qMax(1, qRound(asset.view_width * scale));
        const int height = qMax(1, qRound(asset.view_height * scale));
        const int y = (AtlasHeight - height) / 2;
        asset.pixel_rect = QRect(x, y, width, height);
        x += width + CellGap;
    }

    return result;
}

const std::array<IconAsset, 5> &assets()
{
    static const std::array<IconAsset, 5> result = buildAssets();
    return result;
}

const IconAsset *assetForEntity(InfrastructureEntity entity_type)
{
    const std::array<IconAsset, 5> &all_assets = assets();
    for (const IconAsset &asset : all_assets)
    {
        if (asset.entity_type == entity_type)
            return &asset;
    }
    return nullptr;
}
}

QImage mapIconAtlasImage()
{
    QImage image(AtlasWidth, AtlasHeight, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const std::array<IconAsset, 5> &all_assets = assets();
    for (const IconAsset &asset : all_assets)
    {
        const qreal scale_x = asset.pixel_rect.width() / asset.view_width;
        const qreal scale_y = asset.pixel_rect.height() / asset.view_height;
        const QTransform transform(
            scale_x, 0.0, 0.0, scale_y,
            asset.pixel_rect.left(), asset.pixel_rect.top());

        const qreal stroke_scale = qMin(scale_x, scale_y);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromRgb(networkSymbologyIconDefaultFillColor()));
        if (!asset.fill_path.isEmpty())
            painter.drawPath(transform.map(asset.fill_path));

        QPen outline_pen(QColor::fromRgb(networkSymbologyIconOutlineColor()));
        outline_pen.setWidthF(asset.stroke_width * stroke_scale);
        outline_pen.setCapStyle(Qt::RoundCap);
        outline_pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(outline_pen);
        painter.setBrush(Qt::NoBrush);
        if (!asset.stroke_path.isEmpty())
            painter.drawPath(transform.map(asset.stroke_path));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromRgb(networkSymbologyIconOutlineColor()));
        if (!asset.detail_path.isEmpty())
            painter.drawPath(transform.map(asset.detail_path));
    }

    painter.end();
    return image;
}

MapIconAtlasEntry mapIconAtlasEntry(InfrastructureEntity entity_type)
{
    const IconAsset *asset = assetForEntity(entity_type);
    if (asset == nullptr)
        return MapIconAtlasEntry();

    MapIconAtlasEntry result;
    result.valid = true;
    result.uv_rect = QRectF(
        asset->pixel_rect.left() / qreal(AtlasWidth),
        asset->pixel_rect.top() / qreal(AtlasHeight),
        asset->pixel_rect.width() / qreal(AtlasWidth),
        asset->pixel_rect.height() / qreal(AtlasHeight));
    const qreal maximum_dimension = qMax(asset->view_width, asset->view_height);
    result.width_ratio = asset->view_width / maximum_dimension;
    result.height_ratio = asset->view_height / maximum_dimension;
    return result;
}

bool mapHasIcon(InfrastructureEntity entity_type)
{
    return assetForEntity(entity_type) != nullptr;
}
