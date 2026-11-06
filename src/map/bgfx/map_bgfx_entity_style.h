#ifndef MAP_BGFX_ENTITY_STYLE_H
#define MAP_BGFX_ENTITY_STYLE_H

#include "map/render/map_network_render_symbology.h"
#include "network/network_render_snapshot.h"
#include "map/bgfx/map_bgfx_2d_style_binding.h"

#include <QHash>
#include <QSet>
#include <QVector>

#include <bgfx/bgfx.h>

#include <cstdint>

// GPU colouring of the network (2D map and globe): geometry carries entity
// references only, the vertex shaders colour every primitive from per-entity
// data textures and a palette texture (shaders/entity_style.sh).
//
// - Entity textures (RGBA32F, TextureWidth texels per row, texel = render
//   id), one for nodes and one for links:
//     x: the colouring metric's raw value,
//     y: nodes: the heatmap metric's raw value; links: flow direction (-1, 0, +1),
//     z: flag bits (Flag),
//     w: unused.
//   Uploaded when values, visibility, selection or diagnostics change: one
//   bgfx::updateTexture2D per texture, no geometry is touched.
// - Palette texture (RGBA8, 256 x 3): the node, link and heatmap ramps in the
//   CPU's 256 buckets (networkSymbologyRampColor()), so a palette or flip
//   change is a 3 KB upload.
// - Uniforms: metric ranges, toggles and the fixed colours. A range change
//   (every time step of a result-based metric) costs nothing on the CPU.
//
// The CPU's QRgb rules of the former colour streams, in the shader:
// visual None -> the base colour (MapNetworkRenderSymbology::*_base_color);
// no value or no finite range -> networkSymbologyUnavailableColor(); else the
// ramp bucket of (value - minimum) / (maximum - minimum), 0.5 for an empty
// range. Icons: the default fill while not colourised, the "unavailable" fill
// without a value.
class MapBgfxEntityStyle : public MapBgfx2dStyleBinding
{
public:
    using Mode = MapBgfx2dStyleBinding::Mode;
    using Table = MapBgfx2dStyleBinding::Table;

    // Flag bits of texel.z (an integer stored in a float; mirrored in
    // entity_style.sh).
    enum Flag : quint32
    {
        HasValue = 1u << 0,
        HasHeat = 1u << 1,
        Hidden = 1u << 2,
        Selected = 1u << 3,
        Error = 1u << 4,
        Stale = 1u << 5,
        // Nodes: a junction; links: a pump or valve (device base colour).
        Kind = 1u << 6,
        // The entity class has an icon (mapHasIcon()).
        HasIcon = 1u << 7
    };

    static constexpr int TextureWidth = 2048;
    // Texture stages of the entity data; stage 0 stays free for the icon
    // atlas and the model albedo.
    static constexpr uint8_t NodeStage = 1;
    static constexpr uint8_t LinkStage = 2;
    static constexpr uint8_t PaletteStage = 3;

    MapBgfxEntityStyle();
    bool initialize();
    void release();
    bool ready() const override;

    // --- Inputs (cheap; uploads happen in synchronize()) ------------------
    // Entity classes of the network (junction, device link, icon).
    void setEntities(const NetworkRenderSnapshot &snapshot);
    // Values, ranges, palettes, toggles and base colours.
    void setSymbology(const MapNetworkRenderSymbology &symbology);
    // Icon fill while icons are not colourised (a uniform: the 2D map and
    // the themed globe differ, MapBgfxSurface::globeSymbology()).
    void setIconDefaultFill(QRgb color);
    // Entity keys (MapBgfx2dScene::entityKey()) of the entities not drawn.
    void setHiddenEntities(const QSet<quint64> &keys);
    void setSelectedEntity(quint64 key);
    // Entity key -> stale (grey) instead of error (red).
    void setDiagnosticEntities(const QHash<quint64, bool> &stale_by_key);

    // Rebuilds the CPU texels of what changed (no GPU work; synchronize()
    // calls it, tests read the result through nodeTexels() / linkTexels()).
    void prepare();
    // Uploads what changed since the last call. Once per frame, before the
    // first draw that binds this style.
    void synchronize();
    // RGBA texels by render id (4 floats each), as last prepared.
    const QVector<float> &nodeTexels() const { return this->nodes.texels; }
    const QVector<float> &linkTexels() const { return this->links.texels; }
    // Textures and uniforms of the next submit (bgfx forgets both after it).
    void bind(Mode mode, Table table = Table::Nodes,
              bool tint_selected_icons = false) const override;

    // Diagnostics (benchmarks, tests).
    quint64 textureUploadCount() const { return this->texture_upload_count; }
    quint64 textureUploadBytes() const { return this->texture_upload_bytes; }

private:
    // One entity texture: CPU texels, the GPU texture and its row count.
    struct EntityTexture
    {
        QVector<float> texels;
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int rows = 0;
        // texels out of date / texture out of date.
        bool dirty = true;
        bool upload_pending = true;
    };

    void rebuildNodeTexels();
    void rebuildLinkTexels();
    void rebuildPalette();
    void upload(EntityTexture *texture);
    // Dynamic flags of one entity class (hidden, selected, diagnostics).
    void applyStateFlags(bool links, QVector<float> *texels) const;
    // The uniform values bind() sets, from the symbology (once per change,
    // not per draw).
    void refreshUniforms();

    MapNetworkRenderSymbology symbology;
    QRgb icon_default_fill = qRgb(70, 70, 70);
    // Uniform values of entity_style.sh (u_entity_node, u_entity_link,
    // u_entity_heat, u_entity_options, u_entity_colors).
    struct UniformData
    {
        float node_range[4] = {};
        float link_range[4] = {};
        float heat_range[4] = {};
        float options[4] = {};
        float colors[9 * 4] = {};
    };
    UniformData uniforms;
    // Static class flags (Kind, HasIcon) by render id.
    QVector<quint8> node_class_flags;
    QVector<quint8> link_class_flags;
    QSet<quint64> hidden_keys;
    quint64 selected_key = 0;
    QHash<quint64, bool> diagnostic_stale_by_key;

    EntityTexture nodes;
    EntityTexture links;
    bgfx::TextureHandle palette = BGFX_INVALID_HANDLE;
    bool palette_dirty = true;
    NetworkSymbologyPalette palettes[3] = {};
    bool palettes_flipped[3] = {};
    bool palette_known = false;

    bgfx::UniformHandle node_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle link_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle palette_sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle node_range_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle link_range_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle heat_range_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle options_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colors_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle mode_uniform = BGFX_INVALID_HANDLE;
    quint64 texture_upload_count = 0;
    quint64 texture_upload_bytes = 0;
};

#endif // MAP_BGFX_ENTITY_STYLE_H
