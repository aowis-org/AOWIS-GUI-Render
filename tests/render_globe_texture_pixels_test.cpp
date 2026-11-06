#include "map/bgfx/map_bgfx_globe_resource_pool.h"

#include <QColor>
#include <QImage>

#include <cstddef>
#include <cstdint>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe mip texture failure at " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

int main()
{
    QImage image(3, 5, QImage::Format_RGBA8888);
    image.fill(QColor(20, 80, 160, 128));
    const MapBgfxGlobeMipTexture texture = mapBgfxGlobeMipTexture(image);
    CHECK(texture.width == 3 && texture.height == 5);
    // Original 3x5, then 1x2, then 1x1; exact original downsampling rule.
    CHECK(texture.pixels.size() == (15 + 2 + 1) * 4);
    const QImage premultiplied = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    for (int index = 0; index < 3 * 5 * 4; ++index)
        CHECK(texture.pixels[static_cast<std::size_t>(index)] == premultiplied.constBits()[index]);
    const QImage null_image;
    CHECK(mapBgfxGlobeMipTexture(null_image).pixels.empty());
    QImage singleton(1, 1, QImage::Format_RGBA8888);
    singleton.fill(QColor(255, 128, 0, 255));
    CHECK(mapBgfxGlobeMipTexture(singleton).pixels.size() == 4);
    return 0;
}
#undef CHECK
