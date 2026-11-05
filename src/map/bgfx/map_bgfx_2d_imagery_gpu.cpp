#include "map/bgfx/map_bgfx_2d_imagery_gpu.h"

#include "map/bgfx/map_bgfx_backend.h"

#include <QDebug>
#include <QFile>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace
{
struct ImageryVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Linear filtering; the atlas gutters keep neighbouring cells out, clamping
// keeps the page border cells from wrapping to the opposite side.
constexpr uint64_t kTileSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

// One RGBA8 texel of the image (RGBA8888: byte order R,G,B,A).
const uchar *texel(const QImage &image, int x, int y)
{
    return image.constScanLine(y) + qsizetype(x) * 4;
}
}

quint64 MapBgfx2dImageryGpu::keyFor(const MapBgfx2dImagery::TileId &tile)
{
    // zoom <= 31 and x, y < 2^29 for every Web Mercator level we can request.
    return (quint64(quint32(tile.zoom)) << 58)
        | (quint64(quint32(tile.x)) << 29)
        | quint64(quint32(tile.y));
}

bool MapBgfx2dImageryGpu::initialize()
{
    this->layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    this->program = MapBgfxBackend::loadProgram("vs_imagery_2d", "fs_imagery_2d");
    if (!bgfx::isValid(this->program))
        return false;
    // Pages up to the GPU's texture size limit (WebGL 2 guarantees 2048,
    // desktops and current browsers offer 4096 or more).
    const bgfx::Caps *caps = bgfx::getCaps();
    this->atlas.reset(caps != nullptr ? int(caps->limits.maxTextureSize)
                                      : MapBgfx2dImageryAtlas::MaximumPageSize,
                      TextureBudget);
    this->tile_sampler = bgfx::createUniform("s_tile", bgfx::UniformType::Sampler);
    this->background_uniform = bgfx::createUniform("u_imagery_background", bgfx::UniformType::Vec4);
    return bgfx::isValid(this->tile_sampler) && bgfx::isValid(this->background_uniform);
}

void MapBgfx2dImageryGpu::release()
{
    clearTextures();
    if (bgfx::isValid(this->tile_sampler))
    {
        bgfx::destroy(this->tile_sampler);
        this->tile_sampler = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(this->background_uniform))
    {
        bgfx::destroy(this->background_uniform);
        this->background_uniform = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(this->program))
    {
        bgfx::destroy(this->program);
        this->program = BGFX_INVALID_HANDLE;
    }
}

void MapBgfx2dImageryGpu::destroyPage(int page)
{
    if (page < this->page_textures.size() && bgfx::isValid(this->page_textures.at(page)))
    {
        bgfx::destroy(this->page_textures.at(page));
        this->page_textures[page] = BGFX_INVALID_HANDLE;
    }
}

void MapBgfx2dImageryGpu::clearTextures()
{
    for (int page = 0; page < this->page_textures.size(); ++page)
        destroyPage(page);
    this->page_textures.clear();
    this->atlas.clear();
}

bool MapBgfx2dImageryGpu::hasTexture(const MapBgfx2dImagery::TileId &tile) const
{
    return this->atlas.contains(keyFor(tile));
}

void MapBgfx2dImageryGpu::markUsed(const MapBgfx2dImagery::Layout &layout)
{
    for (const MapBgfx2dImagery::Quad &quad : layout.quads)
    {
        MapBgfx2dImageryAtlas::Slot *slot = this->atlas.slot(keyFor(quad.tile));
        if (slot != nullptr)
            slot->last_used_frame = this->frame;
    }
}

bool MapBgfx2dImageryGpu::atlasRect(const MapBgfx2dImagery::TileId &tile,
                                    bgfx::TextureHandle *page_texture, float *rect)
{
    MapBgfx2dImageryAtlas::Slot *slot = this->atlas.slot(keyFor(tile));
    if (slot == nullptr || slot->page >= this->page_textures.size()
        || !bgfx::isValid(this->page_textures.at(slot->page)))
        return false;
    slot->last_used_frame = this->frame;
    *page_texture = this->page_textures.at(slot->page);
    rect[0] = float(this->atlas.atlasU(*slot, 0.0));
    rect[1] = float(this->atlas.atlasV(*slot, 0.0));
    rect[2] = float(this->atlas.atlasU(*slot, 1.0));
    rect[3] = float(this->atlas.atlasV(*slot, 1.0));
    return true;
}

bool MapBgfx2dImageryGpu::uploadTexture(const MapBgfx2dImagery::TileId &tile,
                                        const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return false;
    // RGBA8888 is byte order R,G,B,A in memory, i.e. bgfx RGBA8. Rows are
    // uploaded top first, so v = 0 is the top edge on every renderer.
    // MapTileRepository already delivers RGBA8888 (converted on its decode
    // threads or by the browser).
    const QImage rgba = image.format() == QImage::Format_RGBA8888
        ? image : image.convertToFormat(QImage::Format_RGBA8888);
    const int width = rgba.width();
    const int height = rgba.height();
    const quint64 key = keyFor(tile);
    const MapBgfx2dImageryAtlas::Allocation allocation =
        this->atlas.allocate(key, width, height, this->frame);
    if (allocation.page < 0)
        return false;
    if (allocation.page_created)
    {
        // Contents undefined until written: only written cells are sampled.
        const MapBgfx2dImageryAtlas::Page &page = this->atlas.page(allocation.page);
        if (this->page_textures.size() <= allocation.page)
            this->page_textures.resize(allocation.page + 1, BGFX_INVALID_HANDLE);
        this->page_textures[allocation.page] = bgfx::createTexture2D(
            uint16_t(page.width), uint16_t(page.height), false, 1,
            bgfx::TextureFormat::RGBA8, kTileSamplerFlags, nullptr);
    }
    const bgfx::TextureHandle texture = this->page_textures.at(allocation.page);
    if (!bgfx::isValid(texture))
    {
        this->atlas.remove(key);
        return false;
    }
    const MapBgfx2dImageryAtlas::Slot *slot = this->atlas.slot(key);
    const QPoint origin = this->atlas.tileOrigin(*slot);
    const int row_bytes = width * 4;

    // The tile itself.
    const bgfx::Memory *memory = nullptr;
    if (rgba.bytesPerLine() == row_bytes)
    {
        // Tightly packed (every 256 px tile): no copy at all. bgfx reads the
        // pixels when it updates the texture during the next bgfx::frame()
        // and then calls the release function, which drops this implicitly
        // shared reference (the pixels stay valid even if the repository
        // evicts the tile meanwhile).
        QImage *keep = new QImage(rgba);
        memory = bgfx::makeRef(keep->constBits(), uint32_t(keep->sizeInBytes()),
                               [](void *, void *user_data) { delete static_cast<QImage *>(user_data); },
                               keep);
    }
    else
    {
        // bytesPerLine() is padded; bgfx expects tightly packed rows.
        memory = bgfx::alloc(uint32_t(qsizetype(row_bytes) * height));
        for (int row = 0; row < height; ++row)
            std::copy_n(rgba.constScanLine(row), row_bytes, memory->data + qsizetype(row) * row_bytes);
    }
    bgfx::updateTexture2D(texture, 0, 0, uint16_t(origin.x()), uint16_t(origin.y()),
                          uint16_t(width), uint16_t(height), memory);

    // The gutter: the edge texels once more around the tile (rows with the
    // corners, then the columns), so bilinear sampling at the tile's edge
    // behaves like clamp-to-edge of a texture of its own.
    for (int side = 0; side < 2; ++side)
    {
        const int source_row = side == 0 ? 0 : height - 1;
        const bgfx::Memory *strip = bgfx::alloc(uint32_t(width + 2) * 4u);
        std::copy_n(texel(rgba, 0, source_row), 4, strip->data);
        std::copy_n(rgba.constScanLine(source_row), row_bytes, strip->data + 4);
        std::copy_n(texel(rgba, width - 1, source_row), 4, strip->data + 4 + row_bytes);
        bgfx::updateTexture2D(texture, 0, 0, uint16_t(origin.x() - 1),
                              uint16_t(side == 0 ? origin.y() - 1 : origin.y() + height),
                              uint16_t(width + 2), 1, strip);
    }
    for (int side = 0; side < 2; ++side)
    {
        const int source_column = side == 0 ? 0 : width - 1;
        const bgfx::Memory *strip = bgfx::alloc(uint32_t(height) * 4u);
        for (int row = 0; row < height; ++row)
            std::copy_n(texel(rgba, source_column, row), 4, strip->data + qsizetype(row) * 4);
        bgfx::updateTexture2D(texture, 0, 0,
                              uint16_t(side == 0 ? origin.x() - 1 : origin.x() + width),
                              uint16_t(origin.y()), 1, uint16_t(height), strip);
    }
    ++this->upload_count;
    return true;
}

void MapBgfx2dImageryGpu::draw(bgfx::ViewId view, const MapBgfx2dImagery::Layout &layout,
                               double viewport_width, double viewport_height,
                               const QColor &background, double background_opacity)
{
    this->draw_calls = 0;
    if (!bgfx::isValid(this->program) || layout.quads.empty()
        || viewport_width <= 0.0 || viewport_height <= 0.0)
    {
        return;
    }

    const uint32_t vertices_per_quad = 6;
    const uint32_t quad_capacity = std::min(
        uint32_t(layout.quads.size()),
        bgfx::getAvailTransientVertexBuffer(uint32_t(layout.quads.size()) * vertices_per_quad,
                                            this->layout) / vertices_per_quad);
    if (quad_capacity == 0)
        return;

    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer, quad_capacity * vertices_per_quad, this->layout);
    ImageryVertex *vertex = reinterpret_cast<ImageryVertex *>(buffer.data);
    // Logical pixels -> clip space, still in double until the final cast.
    const std::function<float(double)> clip_x = [viewport_width](double x)
    {
        return float(x / viewport_width * 2.0 - 1.0);
    };
    const std::function<float(double)> clip_y = [viewport_height](double y)
    {
        return float(1.0 - y / viewport_height * 2.0);
    };
    // Quads in layout order (painter's order: fallbacks under targets) with
    // their page; runs of one page become one draw each.
    struct Run
    {
        int page = -1;
        uint32_t first_quad = 0;
        uint32_t quad_count = 0;
    };
    QVector<Run> runs;
    uint32_t quads = 0;
    for (const MapBgfx2dImagery::Quad &quad : layout.quads)
    {
        if (quads == quad_capacity)
            break;
        MapBgfx2dImageryAtlas::Slot *slot = this->atlas.slot(keyFor(quad.tile));
        if (slot == nullptr)
            continue;
        slot->last_used_frame = this->frame;
        const float left = clip_x(quad.left);
        const float right = clip_x(quad.right);
        const float top = clip_y(quad.top);
        const float bottom = clip_y(quad.bottom);
        const float u0 = float(this->atlas.atlasU(*slot, quad.u0));
        const float v0 = float(this->atlas.atlasV(*slot, quad.v0));
        const float u1 = float(this->atlas.atlasU(*slot, quad.u1));
        const float v1 = float(this->atlas.atlasV(*slot, quad.v1));
        const ImageryVertex corners[6] = {
            {left, top, u0, v0}, {right, top, u1, v0}, {right, bottom, u1, v1},
            {left, top, u0, v0}, {right, bottom, u1, v1}, {left, bottom, u0, v1}
        };
        std::copy_n(corners, 6, vertex + quads * vertices_per_quad);
        if (runs.isEmpty() || runs.last().page != slot->page)
            runs.append(Run{slot->page, quads, 0});
        ++runs.last().quad_count;
        ++quads;
    }

    const float background_data[4] = {
        float(background.redF()), float(background.greenF()), float(background.blueF()),
        float(std::clamp(background_opacity, 0.0, 1.0))
    };
    for (const Run &run : runs)
    {
        bgfx::setVertexBuffer(0, &buffer, run.first_quad * vertices_per_quad,
                              run.quad_count * vertices_per_quad);
        bgfx::setTexture(0, this->tile_sampler, this->page_textures.at(run.page),
                         uint32_t(kTileSamplerFlags));
        bgfx::setUniform(this->background_uniform, background_data);
        // Opaque imagery, no depth: painter's order is the layout order.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, this->program);
        ++this->draw_calls;
    }
}

void MapBgfx2dImageryGpu::endFrame()
{
    const int budget = std::max(TextureBudget, this->atlas.tilesUsedIn(this->frame) + FallbackReserve);
    const QVector<int> dropped = this->atlas.trim(budget, this->frame);
    for (const int page : dropped)
        destroyPage(page);
    ++this->frame;
}
