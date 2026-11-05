#ifndef MAP_BGFX_2D_GPU_H
#define MAP_BGFX_2D_GPU_H

#include "map/bgfx/map_bgfx_2d_mesh.h"
#include "map/bgfx/map_bgfx_2d_style_binding.h"

#include <QImage>
#include <QVector>

#include <bgfx/bgfx.h>

// The bgfx resource owner lives inside MapBgfxSurface. CPU staging is
// immutable across camera movements; GPU geometry buffers are replaced only
// when the active MapBgfx2dScene preparation identity changes (real network
// geometry edits or selection of a different decluttering presentation).
// Per camera frame only uniforms change, and chunks outside the view issue
// no draw calls at all. There are
// no colour streams: the shaders colour every primitive from the entity
// data of MapBgfxEntityStyle, which every draw binds.
class MapBgfx2dGpu
{
public:
    // Sizes are per-frame uniforms (see MapBgfx2dSymbology::sizes()).
    struct Style
    {
        double link_width_px = 3.0;
        double attachment_width_px = 1.5;
        double node_half_px = 6.0;
        double icon_size_px = 24.0;
        double flow_length_px = 10.0;
        double flow_half_stroke_px = 1.0;
        // Heatmap pass: skipped while the radius or the opacity is 0.
        double heatmap_radius_px = 0.0;
        double heatmap_opacity = 0.0;
        double heatmap_solid_fraction = 0.7;
    };

    struct FrameStats
    {
        int chunks_total = 0;
        int chunks_drawn = 0;
        int draw_calls = 0;
    };

    // icon_atlas: premultiplied RGBA icon atlas (mapIconAtlasImage()).
    bool initialize(const QImage &icon_atlas);
    bool synchronize(const MapBgfx2dScene &scene, const MapBgfx2dMesh::IconLookup &icons);
    // The network icon atlas (with mips); the globe network draws its icons
    // from it too.
    bgfx::TextureHandle iconAtlasTexture() const { return this->icon_atlas_texture; }
    // Selection and simulation-error highlights (QRhi rebuildHighlights()):
    // scenes from MapBgfx2dScene::filtered(). Tiny meshes, rebuilt on every
    // call; drawn on top of links/nodes by draw() in the selection and
    // diagnostic modes (the colours come from the entity flags).
    bool setHighlights(const MapBgfx2dScene &selected, bool selected_has_error,
                       const MapBgfx2dScene &diagnostic);
    // entity_style: synchronized for this frame.
    FrameStats draw(bgfx::ViewId view_id, const MapBgfx2d::Camera &camera, const Style &style,
                    const MapBgfx2dStyleBinding &entity_style) const;
    void release();

    quint64 uploadCount() const { return this->upload_count; }
    quint64 uploadedBytes() const { return this->uploaded_bytes; }
    quint64 meshPreparationCount() const { return this->mesh.preparationCount(); }
    qsizetype chevronCount() const { return this->mesh.chevronCount(); }
    bool ready() const
    {
        return bgfx::isValid(this->point_program) && bgfx::isValid(this->line_program)
            && bgfx::isValid(this->icon_program) && bgfx::isValid(this->flow_program)
            && bgfx::isValid(this->heatmap_program);
    }

private:
    struct ChunkBuffers
    {
        bgfx::VertexBufferHandle link_vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle link_indices = BGFX_INVALID_HANDLE;
        uint32_t link_index_count = 0;
        bgfx::VertexBufferHandle attachment_vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle attachment_indices = BGFX_INVALID_HANDLE;
        uint32_t attachment_index_count = 0;
        bgfx::VertexBufferHandle node_vertices = BGFX_INVALID_HANDLE;
        uint32_t node_vertex_count = 0;
        bgfx::VertexBufferHandle icon_vertices = BGFX_INVALID_HANDLE;
        uint32_t icon_vertex_count = 0;
        bgfx::VertexBufferHandle chevron_vertices = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle chevron_indices = BGFX_INVALID_HANDLE;
        uint32_t chevron_index_count = 0;
    };

    // A small separately staged mesh (selection or error highlights).
    struct HighlightLayer
    {
        MapBgfx2dMesh mesh;
        QVector<ChunkBuffers> buffers;
    };

    bool createIconAtlas(const QImage &icon_atlas);
    void releaseChunks();
    static void releaseBuffers(QVector<ChunkBuffers> *chunk_buffers);
    // Creates every buffer for all chunks of source, index aligned with
    // source.chunks().
    bool uploadMesh(const MapBgfx2dMesh &source, QVector<ChunkBuffers> *chunk_buffers,
                    quint64 *bytes);
    bool setHighlightLayer(HighlightLayer *layer, const MapBgfx2dScene &scene);
    void drawHighlightLines(bgfx::ViewId view_id, const HighlightLayer &layer, const MapBgfx2d::Camera &camera,
                            double half_width_px, MapBgfx2dStyleBinding::Mode mode,
                            const MapBgfx2dStyleBinding &entity_style, FrameStats *stats) const;
    void drawHighlightNodes(bgfx::ViewId view_id, const HighlightLayer &layer, const MapBgfx2d::Camera &camera,
                            double half_size_px, MapBgfx2dStyleBinding::Mode mode,
                            const MapBgfx2dStyleBinding &entity_style, FrameStats *stats) const;
    void setCameraUniforms(const MapBgfx2dMeshChunk &chunk,
                           const MapBgfx2d::Camera &camera) const;

    MapBgfx2dMesh mesh;
    QVector<ChunkBuffers> chunk_buffers;
    HighlightLayer selected_highlight;
    HighlightLayer diagnostic_highlight;
    bool selected_has_error = false;
    bgfx::VertexLayout point_layout;
    bgfx::VertexLayout line_layout;
    bgfx::VertexLayout icon_layout;
    bgfx::VertexLayout chevron_layout;
    bgfx::ProgramHandle point_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle line_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle icon_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle flow_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle heatmap_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle camera_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle pixel_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle line_style_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle marker_style_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle icon_style_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle flow_style_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle flow_limits_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle heatmap_style_uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle icon_atlas_sampler = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle icon_atlas_texture = BGFX_INVALID_HANDLE;
    quint64 upload_count = 0;
    quint64 uploaded_bytes = 0;
};

#endif // MAP_BGFX_2D_GPU_H
