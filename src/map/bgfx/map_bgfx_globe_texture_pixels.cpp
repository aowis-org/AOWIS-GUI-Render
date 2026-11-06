#include "map/bgfx/map_bgfx_globe_resource_pool.h"

#include <QImage>

#include <algorithm>
#include <cstdint>
#include <limits>

// Identical to the original GUI mip-chain generation: premultiplied RGBA8,
// smooth Qt downsampling, and tightly packed rows at every level.
MapBgfxGlobeMipTexture mapBgfxGlobeMipTexture(const QImage &source)
{
    MapBgfxGlobeMipTexture image;
    if (source.isNull() || source.width() > std::numeric_limits<std::uint16_t>::max()
        || source.height() > std::numeric_limits<std::uint16_t>::max())
        return image;
    image.width = static_cast<std::uint16_t>(source.width());
    image.height = static_cast<std::uint16_t>(source.height());
    QImage level = source.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    int level_width = source.width();
    int level_height = source.height();
    while (true)
    {
        for (int row = 0; row < level.height(); ++row)
        {
            const std::uint8_t *start = level.constScanLine(row);
            image.pixels.insert(image.pixels.end(), start, start + level.width() * 4);
        }
        if (level_width == 1 && level_height == 1)
            break;
        level_width = std::max(1, level_width / 2);
        level_height = std::max(1, level_height / 2);
        level = level.scaled(level_width, level_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    }
    return image;
}
