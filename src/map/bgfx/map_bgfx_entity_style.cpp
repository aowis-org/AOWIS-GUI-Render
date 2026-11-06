#include "map/bgfx/map_bgfx_entity_style.h"

#include "map/render/map_icon_atlas.h"
#include "network/infrastructure_entity_traits.h"
#include "network/network_symbology_rendering.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace
{
// Entity data is read with texelFetch: no filtering, no wrapping.
constexpr uint64_t kDataSamplerFlags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
constexpr int kPaletteWidth = NetworkSymbologyColorBucketCount;
// u_entity_colors entries (entity_style.sh).
enum ColorIndex
{
    NodeBaseColor,
    PipeBaseColor,
    DeviceLinkBaseColor,
    UnavailableColor,
    IconFillColor,
    IconUnavailableColor,
    SelectionColor,
    ErrorColor,
    StaleColor,
    ColorCount
};

// Whether per-entity data changed. Bitwise (NaN marks "no value" and never
// compares equal), immediate for implicitly shared data (the sizes-only
// slider path hands the same vectors again).
template <typename Value>
bool sameData(const QVector<Value> &a, const QVector<Value> &b)
{
    return a.size() == b.size()
        && (a.constData() == b.constData()
            || std::memcmp(a.constData(), b.constData(), size_t(a.size()) * sizeof(Value)) == 0);
}

template <typename Handle>
void destroyHandle(Handle *handle)
{
    if (bgfx::isValid(*handle))
    {
        bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
}

void setRgb(float *target, QRgb color)
{
    target[0] = float(qRed(color)) / 255.0f;
    target[1] = float(qGreen(color)) / 255.0f;
    target[2] = float(qBlue(color)) / 255.0f;
    target[3] = 1.0f;
}

// (minimum, 1 / (maximum - minimum), base fraction, state) of one metric, as
// networkSymbologyColor(): state 0: visual None; 1: values map onto the
// ramp; 2: no finite range, every value is "unavailable". An empty range
// puts every value at the ramp's middle.
void setRange(float *target, bool active, double minimum, double maximum)
{
    target[0] = 0.0f;
    target[1] = 0.0f;
    target[2] = 0.0f;
    target[3] = 0.0f;
    if (!active)
        return;
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
    {
        target[3] = 2.0f;
        return;
    }
    target[3] = 1.0f;
    target[0] = float(minimum);
    if (minimum == maximum)
        target[2] = 0.5f;
    else
        target[1] = float(1.0 / (maximum - minimum));
}

// Texture rows for a number of render id slot_count (at least one row).
int rowsFor(qsizetype slot_count)
{
    return int(std::max<qsizetype>(1, (slot_count + MapBgfxEntityStyle::TextureWidth - 1)
                                          / MapBgfxEntityStyle::TextureWidth));
}
}

MapBgfxEntityStyle::MapBgfxEntityStyle()
{
    refreshUniforms();
}

bool MapBgfxEntityStyle::initialize()
{
    this->node_sampler = bgfx::createUniform("s_entity_nodes", bgfx::UniformType::Sampler);
    this->link_sampler = bgfx::createUniform("s_entity_links", bgfx::UniformType::Sampler);
    this->palette_sampler = bgfx::createUniform("s_entity_palette", bgfx::UniformType::Sampler);
    this->node_range_uniform = bgfx::createUniform("u_entity_node", bgfx::UniformType::Vec4);
    this->link_range_uniform = bgfx::createUniform("u_entity_link", bgfx::UniformType::Vec4);
    this->heat_range_uniform = bgfx::createUniform("u_entity_heat", bgfx::UniformType::Vec4);
    this->options_uniform = bgfx::createUniform("u_entity_options", bgfx::UniformType::Vec4);
    this->colors_uniform = bgfx::createUniform("u_entity_colors", bgfx::UniformType::Vec4, ColorCount);
    this->mode_uniform = bgfx::createUniform("u_entity_mode", bgfx::UniformType::Vec4);
    const bgfx::Caps *caps = bgfx::getCaps();
    if (caps != nullptr && (caps->formats[bgfx::TextureFormat::RGBA32F] & BGFX_CAPS_FORMAT_TEXTURE_2D) == 0)
    {
        qWarning() << "bgfx: no RGBA32F textures; the network cannot be coloured on this device";
        release();
        return false;
    }
    // Everything goes up again on the (new) device.
    this->nodes.upload_pending = true;
    this->links.upload_pending = true;
    this->palette_dirty = true;
    return ready();
}

bool MapBgfxEntityStyle::ready() const
{
    return bgfx::isValid(this->node_sampler) && bgfx::isValid(this->link_sampler)
        && bgfx::isValid(this->palette_sampler) && bgfx::isValid(this->node_range_uniform)
        && bgfx::isValid(this->link_range_uniform) && bgfx::isValid(this->heat_range_uniform)
        && bgfx::isValid(this->options_uniform) && bgfx::isValid(this->colors_uniform)
        && bgfx::isValid(this->mode_uniform);
}

void MapBgfxEntityStyle::release()
{
    destroyHandle(&this->nodes.handle);
    destroyHandle(&this->links.handle);
    destroyHandle(&this->palette);
    this->nodes.rows = 0;
    this->links.rows = 0;
    this->nodes.upload_pending = true;
    this->links.upload_pending = true;
    this->palette_dirty = true;
    destroyHandle(&this->node_sampler);
    destroyHandle(&this->link_sampler);
    destroyHandle(&this->palette_sampler);
    destroyHandle(&this->node_range_uniform);
    destroyHandle(&this->link_range_uniform);
    destroyHandle(&this->heat_range_uniform);
    destroyHandle(&this->options_uniform);
    destroyHandle(&this->colors_uniform);
    destroyHandle(&this->mode_uniform);
}

// --- Inputs -----------------------------------------------------------------

void MapBgfxEntityStyle::setEntities(const NetworkRenderSnapshot &snapshot)
{
    QVector<quint8> node_flags(snapshot.nodes.size() + 1, 0);
    for (const NetworkRenderNode &node : snapshot.nodes)
    {
        if (qsizetype(node.render_id) >= node_flags.size())
            node_flags.resize(qsizetype(node.render_id) + 1, 0);
        quint8 flags = 0;
        if (node.entity_type == InfrastructureEntity::Junction)
            flags |= quint8(Kind);
        if (mapHasIcon(node.entity_type))
            flags |= quint8(HasIcon);
        node_flags[qsizetype(node.render_id)] = flags;
    }
    QVector<quint8> link_flags(snapshot.links.size() + 1, 0);
    for (const NetworkRenderLink &link : snapshot.links)
    {
        if (qsizetype(link.render_id) >= link_flags.size())
            link_flags.resize(qsizetype(link.render_id) + 1, 0);
        quint8 flags = 0;
        if (link.entity_type != InfrastructureEntity::Pipe)
            flags |= quint8(Kind);
        if (mapHasIcon(link.entity_type))
            flags |= quint8(HasIcon);
        link_flags[qsizetype(link.render_id)] = flags;
    }
    if (node_flags != this->node_class_flags)
    {
        this->node_class_flags = node_flags;
        this->nodes.dirty = true;
    }
    if (link_flags != this->link_class_flags)
    {
        this->link_class_flags = link_flags;
        this->links.dirty = true;
    }
}

void MapBgfxEntityStyle::setSymbology(const MapNetworkRenderSymbology &symbology)
{
    // Only the per-entity data needs uploads.
    if (!sameData(symbology.node_values, this->symbology.node_values)
        || !sameData(symbology.heatmap_values, this->symbology.heatmap_values))
        this->nodes.dirty = true;
    if (!sameData(symbology.link_values, this->symbology.link_values)
        || !sameData(symbology.flow_directions, this->symbology.flow_directions))
        this->links.dirty = true;
    const NetworkSymbologyPalette palettes[3] = {symbology.node_palette, symbology.link_palette,
                                                 symbology.heatmap_palette};
    const bool flipped[3] = {symbology.node_palette_flipped, symbology.link_palette_flipped,
                             symbology.heatmap_palette_flipped};
    for (int row = 0; row < 3; ++row)
    {
        if (!this->palette_known || palettes[row] != this->palettes[row] || flipped[row] != this->palettes_flipped[row])
            this->palette_dirty = true;
        this->palettes[row] = palettes[row];
        this->palettes_flipped[row] = flipped[row];
    }
    this->palette_known = true;
    this->symbology = symbology;
    refreshUniforms();
}

void MapBgfxEntityStyle::setHiddenEntities(const QSet<quint64> &keys)
{
    if (keys == this->hidden_keys)
        return;
    this->hidden_keys = keys;
    this->nodes.dirty = true;
    this->links.dirty = true;
}

void MapBgfxEntityStyle::setSelectedEntity(quint64 key)
{
    if (key == this->selected_key)
        return;
    this->selected_key = key;
    this->nodes.dirty = true;
    this->links.dirty = true;
}

void MapBgfxEntityStyle::setDiagnosticEntities(const QHash<quint64, bool> &stale_by_key)
{
    if (stale_by_key == this->diagnostic_stale_by_key)
        return;
    this->diagnostic_stale_by_key = stale_by_key;
    this->nodes.dirty = true;
    this->links.dirty = true;
}

// --- Texels -----------------------------------------------------------------

void MapBgfxEntityStyle::applyStateFlags(bool links, QVector<float> *texels) const
{
    const qsizetype slot_count = texels->size() / 4;
    float *data = texels->data();
    // Entity keys carry the entity class in their upper half; only the keys
    // of this texture's class apply.
    const std::function<void(quint64, quint32)> add_flag = [links, slot_count, data](quint64 key, quint32 flag)
    {
        const InfrastructureEntity entity_type = static_cast<InfrastructureEntity>(quint8(key >> 32));
        if (InfrastructureEntityTraits::isHydraulicNetworkLink(entity_type) != links)
            return;
        const qsizetype render_id = qsizetype(key & 0xffffffffu);
        if (render_id <= 0 || render_id >= slot_count)
            return;
        float *flags = data + render_id * 4 + 2;
        *flags = float(quint32(*flags) | flag);
    };
    for (const quint64 key : this->hidden_keys)
        add_flag(key, Hidden);
    if (this->selected_key != 0)
        add_flag(this->selected_key, Selected);
    for (QHash<quint64, bool>::const_iterator entry = this->diagnostic_stale_by_key.cbegin();
         entry != this->diagnostic_stale_by_key.cend(); ++entry)
        add_flag(entry.key(), entry.value() ? (Error | Stale) : Error);
}

void MapBgfxEntityStyle::rebuildNodeTexels()
{
    const QVector<double> &values = this->symbology.node_values;
    const QVector<double> &heat = this->symbology.heatmap_values;
    const qsizetype slot_count = std::max({this->node_class_flags.size(), values.size(), heat.size(), qsizetype(1)});
    QVector<float> &texels = this->nodes.texels;
    texels.fill(0.0f, qsizetype(rowsFor(slot_count)) * TextureWidth * 4);
    float *data = texels.data();
    for (qsizetype render_id = 0; render_id < slot_count; ++render_id)
    {
        float *texel = data + render_id * 4;
        quint32 flags = render_id < this->node_class_flags.size() ? this->node_class_flags[render_id] : 0u;
        if (render_id < values.size() && std::isfinite(values[render_id]))
        {
            texel[0] = float(values[render_id]);
            flags |= HasValue;
        }
        if (render_id < heat.size() && std::isfinite(heat[render_id]))
        {
            texel[1] = float(heat[render_id]);
            flags |= HasHeat;
        }
        texel[2] = float(flags);
    }
    applyStateFlags(false, &texels);
}

void MapBgfxEntityStyle::rebuildLinkTexels()
{
    const QVector<double> &values = this->symbology.link_values;
    const QVector<qint8> &directions = this->symbology.flow_directions;
    const qsizetype slot_count = std::max({this->link_class_flags.size(), values.size(), directions.size(), qsizetype(1)});
    QVector<float> &texels = this->links.texels;
    texels.fill(0.0f, qsizetype(rowsFor(slot_count)) * TextureWidth * 4);
    float *data = texels.data();
    for (qsizetype render_id = 0; render_id < slot_count; ++render_id)
    {
        float *texel = data + render_id * 4;
        quint32 flags = render_id < this->link_class_flags.size() ? this->link_class_flags[render_id] : 0u;
        if (render_id < values.size() && std::isfinite(values[render_id]))
        {
            texel[0] = float(values[render_id]);
            flags |= HasValue;
        }
        if (render_id < directions.size())
            texel[1] = float(directions[render_id]);
        texel[2] = float(flags);
    }
    applyStateFlags(true, &texels);
}

void MapBgfxEntityStyle::upload(EntityTexture *texture)
{
    const int rows = int(texture->texels.size() / (qsizetype(TextureWidth) * 4));
    if (!bgfx::isValid(texture->handle) || texture->rows != rows)
    {
        destroyHandle(&texture->handle);
        // Mutable (no initial memory): updated in place from then on.
        texture->handle = bgfx::createTexture2D(uint16_t(TextureWidth), uint16_t(rows), false, 1,
                                                bgfx::TextureFormat::RGBA32F, kDataSamplerFlags, nullptr);
        texture->rows = rows;
    }
    if (!bgfx::isValid(texture->handle))
        return;
    const uint32_t bytes = uint32_t(texture->texels.size() * qsizetype(sizeof(float)));
    bgfx::updateTexture2D(texture->handle, 0, 0, 0, 0, uint16_t(TextureWidth), uint16_t(rows),
                          bgfx::copy(texture->texels.constData(), bytes));
    ++this->texture_upload_count;
    this->texture_upload_bytes += bytes;
}

void MapBgfxEntityStyle::rebuildPalette()
{
    // The CPU's 256 buckets (networkSymbologyRampColor()), flip applied;
    // rows: node, link, heatmap ramp (entity_style.sh).
    QVector<quint8> pixels(kPaletteWidth * 3 * 4, 255);
    for (int row = 0; row < 3; ++row)
    {
        for (int bucket = 0; bucket < kPaletteWidth; ++bucket)
        {
            const QColor color = networkSymbologyInterpolatedRampColor(
                double(bucket) / double(kPaletteWidth - 1), this->palettes[row], this->palettes_flipped[row]);
            quint8 *pixel = pixels.data() + (row * kPaletteWidth + bucket) * 4;
            pixel[0] = quint8(color.red());
            pixel[1] = quint8(color.green());
            pixel[2] = quint8(color.blue());
        }
    }
    if (!bgfx::isValid(this->palette))
        this->palette = bgfx::createTexture2D(uint16_t(kPaletteWidth), 3, false, 1, bgfx::TextureFormat::RGBA8,
                                              kDataSamplerFlags, nullptr);
    if (!bgfx::isValid(this->palette))
        return;
    bgfx::updateTexture2D(this->palette, 0, 0, 0, 0, uint16_t(kPaletteWidth), 3,
                          bgfx::copy(pixels.constData(), uint32_t(pixels.size())));
}

void MapBgfxEntityStyle::prepare()
{
    if (this->nodes.dirty)
    {
        rebuildNodeTexels();
        this->nodes.dirty = false;
        this->nodes.upload_pending = true;
    }
    if (this->links.dirty)
    {
        rebuildLinkTexels();
        this->links.dirty = false;
        this->links.upload_pending = true;
    }
}

void MapBgfxEntityStyle::synchronize()
{
    if (!ready())
        return;
    prepare();
    if (this->nodes.upload_pending)
    {
        upload(&this->nodes);
        this->nodes.upload_pending = false;
    }
    if (this->links.upload_pending)
    {
        upload(&this->links);
        this->links.upload_pending = false;
    }
    if (this->palette_dirty)
    {
        rebuildPalette();
        this->palette_dirty = false;
    }
}

void MapBgfxEntityStyle::refreshUniforms()
{
    static_assert(sizeof(UniformData::colors) == sizeof(float) * 4 * ColorCount,
                  "u_entity_colors entries (entity_style.sh) and UniformData::colors differ");
    const MapNetworkRenderSymbology &symbology = this->symbology;
    setRange(this->uniforms.node_range, symbology.visual_node != VisualNode::None, symbology.ranges.node_minimum,
             symbology.ranges.node_maximum);
    setRange(this->uniforms.link_range, symbology.visual_link != VisualLink::None, symbology.ranges.link_minimum,
             symbology.ranges.link_maximum);
    setRange(this->uniforms.heat_range, symbology.visual_heatmap != VisualHeatmap::None,
             symbology.ranges.heatmap_minimum, symbology.ranges.heatmap_maximum);
    this->uniforms.options[0] = symbology.show_junctions ? 1.0f : 0.0f;
    this->uniforms.options[1] = symbology.show_icons ? 1.0f : 0.0f;
    this->uniforms.options[2] = symbology.show_flow_direction && symbology.flow_direction_size_px > 0 ? 1.0f : 0.0f;
    this->uniforms.options[3] = 0.0f;
    float *colors = this->uniforms.colors;
    setRgb(colors + NodeBaseColor * 4, symbology.node_base_color);
    setRgb(colors + PipeBaseColor * 4, symbology.pipe_base_color);
    setRgb(colors + DeviceLinkBaseColor * 4, symbology.device_link_base_color);
    setRgb(colors + UnavailableColor * 4, networkSymbologyUnavailableColor());
    setRgb(colors + IconFillColor * 4, this->icon_default_fill);
    setRgb(colors + IconUnavailableColor * 4, networkSymbologyIconUnavailableFillColor());
    // QRhi colours: selection (0, 190, 255); errors red, stale results grey.
    setRgb(colors + SelectionColor * 4, qRgb(0, 190, 255));
    setRgb(colors + ErrorColor * 4, qRgb(255, 0, 0));
    setRgb(colors + StaleColor * 4, qRgb(128, 128, 128));
}

void MapBgfxEntityStyle::setIconDefaultFill(QRgb color)
{
    if (color == this->icon_default_fill)
        return;
    this->icon_default_fill = color;
    refreshUniforms();
}

void MapBgfxEntityStyle::bind(Mode mode, Table table, bool tint_selected_icons) const
{
    if (!ready())
        return;
    bgfx::setTexture(NodeStage, this->node_sampler, this->nodes.handle, uint32_t(kDataSamplerFlags));
    bgfx::setTexture(LinkStage, this->link_sampler, this->links.handle, uint32_t(kDataSamplerFlags));
    bgfx::setTexture(PaletteStage, this->palette_sampler, this->palette, uint32_t(kDataSamplerFlags));

    bgfx::setUniform(this->node_range_uniform, this->uniforms.node_range);
    bgfx::setUniform(this->link_range_uniform, this->uniforms.link_range);
    bgfx::setUniform(this->heat_range_uniform, this->uniforms.heat_range);
    bgfx::setUniform(this->options_uniform, this->uniforms.options);
    bgfx::setUniform(this->colors_uniform, this->uniforms.colors, ColorCount);
    const float mode_data[4] = {float(int(mode)), float(int(table)), tint_selected_icons ? 1.0f : 0.0f, 0.0f};
    bgfx::setUniform(this->mode_uniform, mode_data);
}
