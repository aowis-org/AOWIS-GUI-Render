#include "map/bgfx/map_bgfx_2d_gpu.h"

#include "map/bgfx/map_bgfx_backend.h"

#include <QByteArray>
#include <QFile>
#include <QDebug>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>

namespace
{
// Opaque network colours with alpha blending; no depth test (painter's order).
constexpr uint64_t kNetworkState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA;
// Heatmap spots are premultiplied (fs_heatmap_2d.sc), composited "over" in
// node order like the QRhi heatmap pipeline (One, OneMinusSrcAlpha).
constexpr uint64_t kHeatmapState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA)
    | BGFX_STATE_MSAA;
// Clamp so linear/mip filtering never bleeds a neighbouring atlas cell in.
constexpr uint64_t kAtlasSamplerFlags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

// Shaders come from MapBgfxBackend::loadProgram(), shared with the imagery
// and HUD parts, so every part reports a missing or rejected shader alike.
using MapBgfxBackend::loadProgram;

template <typename Vertex>
bgfx::VertexBufferHandle createVertices(const QVector<Vertex> &vertices,
                                        const bgfx::VertexLayout &layout, quint64 *bytes)
{
    if (vertices.isEmpty())
        return BGFX_INVALID_HANDLE;
    const qsizetype size = vertices.size() * qsizetype(sizeof(Vertex));
    if (!MapBgfxBackend::fitsUint32(size))
        return BGFX_INVALID_HANDLE;
    *bytes += quint64(size);
    return bgfx::createVertexBuffer(bgfx::copy(vertices.constData(), uint32_t(size)), layout);
}

bgfx::IndexBufferHandle createIndices(const QVector<quint32> &indices, quint64 *bytes)
{
    if (indices.isEmpty())
        return BGFX_INVALID_HANDLE;
    const qsizetype size = indices.size() * qsizetype(sizeof(quint32));
    if (!MapBgfxBackend::fitsUint32(size))
        return BGFX_INVALID_HANDLE;
    *bytes += quint64(size);
    // 32-bit indices: a dense chunk may exceed 65535 vertices (WebGL 2 and
    // all desktop renderers support uint32 indices).
    return bgfx::createIndexBuffer(bgfx::copy(indices.constData(), uint32_t(size)),
                                   BGFX_BUFFER_INDEX32);
}

template <typename Handle>
void destroyHandle(Handle *handle)
{
    if (bgfx::isValid(*handle)) {
        bgfx::destroy(*handle);
        *handle = BGFX_INVALID_HANDLE;
    }
}

// A stream-0 buffer that should exist (non-empty data) but does not.
template <typename Data, typename Handle>
bool missing(const QVector<Data> &data, Handle handle)
{
    return !data.isEmpty() && !bgfx::isValid(handle);
}
}

bool MapBgfx2dGpu::initialize(const QImage &icon_atlas)
{
    // Layouts matching the vertex structs in map_bgfx_2d_mesh.h; the entity
    // render id is an attribute of every primitive (entity_style.sh).
    this->point_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .end();
    this->line_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .end();
    this->icon_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 2, bgfx::AttribType::Float)
        .end();
    this->chevron_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 2, bgfx::AttribType::Float)
        .end();
    if (this->point_layout.getStride() != sizeof(MapBgfx2dGpuVertex)
        || this->line_layout.getStride() != sizeof(MapBgfx2dLineVertex)
        || this->icon_layout.getStride() != sizeof(MapBgfx2dIconVertex)
        || this->chevron_layout.getStride() != sizeof(MapBgfx2dChevronVertex))
    {
        qWarning() << "bgfx network: vertex layouts do not match the mesh's vertex structs";
        return false;
    }

    this->point_program = loadProgram("vs_network_2d", "fs_marker_2d");
    this->line_program = loadProgram("vs_line_2d", "fs_network_2d");
    this->icon_program = loadProgram("vs_icon_2d", "fs_icon_2d");
    this->flow_program = loadProgram("vs_flow_2d", "fs_flow_2d");
    // Heatmap spots reuse the node marker vertex shader (radius as size).
    this->heatmap_program = loadProgram("vs_network_2d", "fs_heatmap_2d");
    if (!ready())
    {
        qWarning() << "bgfx network programs: point" << bgfx::isValid(this->point_program)
                   << "line" << bgfx::isValid(this->line_program)
                   << "icon" << bgfx::isValid(this->icon_program)
                   << "flow" << bgfx::isValid(this->flow_program)
                   << "heatmap" << bgfx::isValid(this->heatmap_program);
        return false;
    }
    this->camera_uniform = bgfx::createUniform("u_network_camera", bgfx::UniformType::Vec4);
    this->pixel_uniform = bgfx::createUniform("u_network_pixel", bgfx::UniformType::Vec4);
    this->line_style_uniform = bgfx::createUniform("u_line_style", bgfx::UniformType::Vec4);
    this->marker_style_uniform = bgfx::createUniform("u_marker_style", bgfx::UniformType::Vec4);
    this->icon_style_uniform = bgfx::createUniform("u_icon_style", bgfx::UniformType::Vec4);
    this->flow_style_uniform = bgfx::createUniform("u_flow_style", bgfx::UniformType::Vec4);
    this->flow_limits_uniform = bgfx::createUniform("u_flow_limits", bgfx::UniformType::Vec4);
    this->heatmap_style_uniform = bgfx::createUniform("u_heatmap_style", bgfx::UniformType::Vec4);
    this->icon_atlas_sampler = bgfx::createUniform("s_icon_atlas", bgfx::UniformType::Sampler);
    const bool uniforms_ready = bgfx::isValid(this->camera_uniform) && bgfx::isValid(this->pixel_uniform)
        && bgfx::isValid(this->line_style_uniform) && bgfx::isValid(this->marker_style_uniform)
        && bgfx::isValid(this->icon_style_uniform) && bgfx::isValid(this->flow_style_uniform)
        && bgfx::isValid(this->flow_limits_uniform) && bgfx::isValid(this->heatmap_style_uniform)
        && bgfx::isValid(this->icon_atlas_sampler);
    if (!uniforms_ready)
    {
        qWarning() << "bgfx rejected a network uniform";
        return false;
    }
    if (!createIconAtlas(icon_atlas))
    {
        qWarning() << "bgfx rejected the icon atlas texture" << icon_atlas.size();
        return false;
    }
    return true;
}

bool MapBgfx2dGpu::createIconAtlas(const QImage &icon_atlas)
{
    if (icon_atlas.isNull() || icon_atlas.width() > 8192 || icon_atlas.height() > 8192)
        return false;
    // Icons are drawn at ~10-60 px from 256 px atlas cells: without mipmaps
    // that minification aliases badly. bgfx expects the complete chain
    // (max(1, size >> level) down to 1x1) packed in one memory block.
    QImage level = icon_atlas.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int width = level.width();
    const int height = level.height();
    QByteArray pixels;
    const std::function<void(const QImage &)> append_level = [&pixels](const QImage &image)
    {
        const int row_bytes = image.width() * 4;
        for (int row = 0; row < image.height(); ++row)
            pixels.append(reinterpret_cast<const char *>(image.constScanLine(row)), row_bytes);
    };
    append_level(level);
    int level_width = width;
    int level_height = height;
    while (level_width > 1 || level_height > 1)
    {
        level_width = std::max(1, level_width / 2);
        level_height = std::max(1, level_height / 2);
        // Smooth scaling works on premultiplied data; force the format back.
        level = level.scaled(level_width, level_height, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        append_level(level);
    }
    this->icon_atlas_texture = bgfx::createTexture2D(
        uint16_t(width), uint16_t(height), true, 1, bgfx::TextureFormat::RGBA8,
        kAtlasSamplerFlags, bgfx::copy(pixels.constData(), uint32_t(pixels.size())));
    return bgfx::isValid(this->icon_atlas_texture);
}

void MapBgfx2dGpu::releaseBuffers(QVector<ChunkBuffers> *chunk_buffers)
{
    for (ChunkBuffers &buffers : *chunk_buffers) {
        destroyHandle(&buffers.link_vertices);
        destroyHandle(&buffers.link_indices);
        destroyHandle(&buffers.attachment_vertices);
        destroyHandle(&buffers.attachment_indices);
        destroyHandle(&buffers.node_vertices);
        destroyHandle(&buffers.icon_vertices);
        destroyHandle(&buffers.chevron_vertices);
        destroyHandle(&buffers.chevron_indices);
    }
    chunk_buffers->clear();
}

void MapBgfx2dGpu::releaseChunks()
{
    releaseBuffers(&this->chunk_buffers);
}

bool MapBgfx2dGpu::uploadMesh(const MapBgfx2dMesh &source, QVector<ChunkBuffers> *chunk_buffers,
                              quint64 *bytes)
{
    releaseBuffers(chunk_buffers);
    chunk_buffers->reserve(source.chunks().size());
    for (const MapBgfx2dMeshChunk &chunk : source.chunks()) {
        ChunkBuffers buffers;
        buffers.link_vertices = createVertices(chunk.link_vertices, this->line_layout, bytes);
        buffers.link_indices = createIndices(chunk.link_indices, bytes);
        buffers.link_index_count = uint32_t(chunk.link_indices.size());
        buffers.attachment_vertices =
            createVertices(chunk.attachment_vertices, this->line_layout, bytes);
        buffers.attachment_indices = createIndices(chunk.attachment_indices, bytes);
        buffers.attachment_index_count = uint32_t(chunk.attachment_indices.size());
        buffers.node_vertices = createVertices(chunk.node_vertices, this->point_layout, bytes);
        buffers.node_vertex_count = uint32_t(chunk.node_vertices.size());
        buffers.icon_vertices = createVertices(chunk.icon_vertices, this->icon_layout, bytes);
        buffers.icon_vertex_count = uint32_t(chunk.icon_vertices.size());
        buffers.chevron_vertices =
            createVertices(chunk.chevron_vertices, this->chevron_layout, bytes);
        buffers.chevron_indices = createIndices(chunk.chevron_indices, bytes);
        buffers.chevron_index_count = uint32_t(chunk.chevron_indices.size());
        // Keep the buffers index aligned with source.chunks() even when empty.
        chunk_buffers->append(buffers);

        const bool failed = missing(chunk.link_vertices, buffers.link_vertices)
            || missing(chunk.link_indices, buffers.link_indices)
            || missing(chunk.attachment_vertices, buffers.attachment_vertices)
            || missing(chunk.attachment_indices, buffers.attachment_indices)
            || missing(chunk.node_vertices, buffers.node_vertices)
            || missing(chunk.icon_vertices, buffers.icon_vertices)
            || missing(chunk.chevron_vertices, buffers.chevron_vertices)
            || missing(chunk.chevron_indices, buffers.chevron_indices);
        if (failed)
            return false;
    }
    return true;
}

bool MapBgfx2dGpu::synchronize(const MapBgfx2dScene &scene,
                               const MapBgfx2dMesh::IconLookup &icons)
{
    if (!scene.isPrepared())
        return true;
    if (!this->mesh.prepare(scene, icons))
        return true;

    quint64 bytes = 0;
    const bool uploaded = uploadMesh(this->mesh, &this->chunk_buffers, &bytes);
    if (!uploaded)
        return false;
    ++this->upload_count;
    this->uploaded_bytes += bytes;
    return true;
}

bool MapBgfx2dGpu::setHighlightLayer(HighlightLayer *layer, const MapBgfx2dScene &scene)
{
    layer->mesh.setChevronsEnabled(false);
    layer->mesh.reset();
    if (scene.linkPolylines().isEmpty() && scene.networkNodes().isEmpty()) {
        releaseBuffers(&layer->buffers);
        return true;
    }
    // No icon lookup: highlights only cover link and node geometry (QRhi).
    if (!layer->mesh.prepare(scene, MapBgfx2dMesh::IconLookup()))
        return true;
    quint64 bytes = 0;
    if (!uploadMesh(layer->mesh, &layer->buffers, &bytes)) {
        releaseBuffers(&layer->buffers);
        return false;
    }
    return true;
}

bool MapBgfx2dGpu::setHighlights(const MapBgfx2dScene &selected, bool selected_has_error,
                                 const MapBgfx2dScene &diagnostic)
{
    this->selected_has_error = selected_has_error;
    const bool selected_ok = setHighlightLayer(&this->selected_highlight, selected);
    const bool diagnostic_ok = setHighlightLayer(&this->diagnostic_highlight, diagnostic);
    return selected_ok && diagnostic_ok;
}

void MapBgfx2dGpu::setCameraUniforms(const MapBgfx2dMeshChunk &chunk,
                                     const MapBgfx2d::Camera &camera) const
{
    const MapBgfx2dClipCamera uniforms = MapBgfx2dChunking::cameraUniform(chunk.origin, camera);
    const float camera_data[4] = {uniforms.origin_x, uniforms.origin_y,
                                  uniforms.scale_x, uniforms.scale_y};
    const float pixel_data[4] = {uniforms.pixel_x, uniforms.pixel_y, 0.0f, 0.0f};
    // bgfx uniforms are per-submit state; set them for every draw call.
    bgfx::setUniform(this->camera_uniform, camera_data);
    bgfx::setUniform(this->pixel_uniform, pixel_data);
}

MapBgfx2dGpu::FrameStats MapBgfx2dGpu::draw(bgfx::ViewId view_id, const MapBgfx2d::Camera &camera,
                                            const Style &style,
                                            const MapBgfx2dStyleBinding &entity_style) const
{
    FrameStats stats;
    if (!ready() || !entity_style.ready())
        return stats;
    const QVector<MapBgfx2dMeshChunk> &chunks = this->mesh.chunks();
    stats.chunks_total = int(chunks.size());
    if (chunks.size() != this->chunk_buffers.size())
        return stats;

    const double link_half = std::max(0.5, style.link_width_px * 0.5);
    const double attachment_half = std::max(0.5, style.attachment_width_px * 0.5);
    const double flow_half_extent = 0.5 * style.flow_length_px + style.flow_half_stroke_px + 1.0;
    // Cull with the largest screen extent any symbol has beyond its position.
    const double margin = std::max({link_half * 2.0, attachment_half * 2.0, style.node_half_px,
                                    style.icon_size_px, flow_half_extent * 1.5}) + 2.0;

    QVector<int> visible;
    visible.reserve(chunks.size());
    for (int index = 0; index < int(chunks.size()); ++index) {
        if (MapBgfx2dChunking::chunkVisible(chunks[index], camera, margin))
            visible.append(index);
    }
    stats.chunks_drawn = int(visible.size());

    const std::function<void(bgfx::UniformHandle, float, float, float, float)> set_vec4 =
        [](bgfx::UniformHandle uniform, float x, float y, float z, float w)
    {
        const float data[4] = {x, y, z, w};
        bgfx::setUniform(uniform, data);
    };

    // Layer order across all chunks (as QRhi): heatmap (directly above the
    // imagery), attachments, links, flow chevrons, selected link, error
    // links, node dots, selected node, error nodes, icons. Highlights are
    // wider copies of the entity in the highlight colour drawn on top, so a
    // selected entity with an error shows a red core with a blue rim.
    const double selected_link_half =
        0.5 * std::max(3.0, style.link_width_px + (this->selected_has_error ? 6.0 : 2.0));
    const double diagnostic_link_half = 0.5 * std::max(3.0, style.link_width_px + 2.0);
    const double selected_node_half =
        style.node_half_px + (this->selected_has_error ? 5.0 : 2.0);
    const double diagnostic_node_half = style.node_half_px + 2.0;
    if (style.heatmap_radius_px > 0.0 && style.heatmap_opacity > 0.0) {
        // Own culling margin: a spot reaches heatmap_radius_px beyond its node.
        const double heatmap_margin = style.heatmap_radius_px + 2.0;
        for (int index = 0; index < int(chunks.size()); ++index) {
            const ChunkBuffers &buffers = this->chunk_buffers[index];
            if (!bgfx::isValid(buffers.node_vertices) || buffers.node_vertex_count == 0
                || !MapBgfx2dChunking::chunkVisible(chunks[index], camera, heatmap_margin))
                continue;
            setCameraUniforms(chunks[index], camera);
            set_vec4(this->marker_style_uniform, float(style.heatmap_radius_px), 0.0f, 0.0f, 0.0f);
            set_vec4(this->heatmap_style_uniform, float(style.heatmap_opacity),
                     float(style.heatmap_solid_fraction), 0.0f, 0.0f);
            bgfx::setVertexBuffer(0, buffers.node_vertices, 0, buffers.node_vertex_count);
            entity_style.bind(MapBgfx2dStyleBinding::Mode::Heatmap);
            bgfx::setState(kHeatmapState);
            bgfx::submit(view_id, this->heatmap_program);
            ++stats.draw_calls;
        }
    }
    for (const int index : visible) {
        const ChunkBuffers &buffers = this->chunk_buffers[index];
        if (!bgfx::isValid(buffers.attachment_vertices) || buffers.attachment_index_count == 0)
            continue;
        setCameraUniforms(chunks[index], camera);
        // Square caps of half the width close polyline joins.
        set_vec4(this->line_style_uniform, float(attachment_half), float(attachment_half), 0.0f, 0.0f);
        bgfx::setVertexBuffer(0, buffers.attachment_vertices);
        // Attachments take their demand point's colour.
        entity_style.bind(MapBgfx2dStyleBinding::Mode::Symbology, MapBgfx2dStyleBinding::Table::Nodes);
        bgfx::setIndexBuffer(buffers.attachment_indices, 0, buffers.attachment_index_count);
        bgfx::setState(kNetworkState);
        bgfx::submit(view_id, this->line_program);
        ++stats.draw_calls;
    }
    for (const int index : visible) {
        const ChunkBuffers &buffers = this->chunk_buffers[index];
        if (!bgfx::isValid(buffers.link_vertices) || buffers.link_index_count == 0)
            continue;
        setCameraUniforms(chunks[index], camera);
        set_vec4(this->line_style_uniform, float(link_half), float(link_half), 0.0f, 0.0f);
        bgfx::setVertexBuffer(0, buffers.link_vertices);
        entity_style.bind(MapBgfx2dStyleBinding::Mode::Symbology, MapBgfx2dStyleBinding::Table::Links);
        bgfx::setIndexBuffer(buffers.link_indices, 0, buffers.link_index_count);
        bgfx::setState(kNetworkState);
        bgfx::submit(view_id, this->line_program);
        ++stats.draw_calls;
    }
    if (style.flow_length_px > 0.0) {
        const float threshold = float(MapBgfx2dFlow::thresholdLevel(camera.world_pixels));
        for (const int index : visible) {
            const ChunkBuffers &buffers = this->chunk_buffers[index];
            if (!bgfx::isValid(buffers.chevron_vertices) || buffers.chevron_index_count == 0)
                continue;
            setCameraUniforms(chunks[index], camera);
            set_vec4(this->flow_style_uniform, float(style.flow_length_px),
                     float(style.flow_half_stroke_px), threshold, float(camera.world_pixels));
            set_vec4(this->flow_limits_uniform, float(MapBgfx2dFlow::MinimumLinkPixels),
                     0.0f, 0.0f, 0.0f);
            bgfx::setVertexBuffer(0, buffers.chevron_vertices);
            entity_style.bind(MapBgfx2dStyleBinding::Mode::Symbology, MapBgfx2dStyleBinding::Table::Links);
            bgfx::setIndexBuffer(buffers.chevron_indices, 0, buffers.chevron_index_count);
            bgfx::setState(kNetworkState);
            bgfx::submit(view_id, this->flow_program);
            ++stats.draw_calls;
        }
    }
    drawHighlightLines(view_id, this->selected_highlight, camera, selected_link_half,
                       MapBgfx2dStyleBinding::Mode::Selection, entity_style, &stats);
    drawHighlightLines(view_id, this->diagnostic_highlight, camera, diagnostic_link_half,
                       MapBgfx2dStyleBinding::Mode::Diagnostic, entity_style, &stats);
    for (const int index : visible) {
        const ChunkBuffers &buffers = this->chunk_buffers[index];
        if (!bgfx::isValid(buffers.node_vertices) || buffers.node_vertex_count == 0)
            continue;
        setCameraUniforms(chunks[index], camera);
        set_vec4(this->marker_style_uniform, float(style.node_half_px), 0.0f, 0.0f, 0.0f);
        bgfx::setVertexBuffer(0, buffers.node_vertices, 0, buffers.node_vertex_count);
        entity_style.bind(MapBgfx2dStyleBinding::Mode::Symbology);
        bgfx::setState(kNetworkState);
        bgfx::submit(view_id, this->point_program);
        ++stats.draw_calls;
    }
    drawHighlightNodes(view_id, this->selected_highlight, camera, selected_node_half,
                       MapBgfx2dStyleBinding::Mode::Selection, entity_style, &stats);
    drawHighlightNodes(view_id, this->diagnostic_highlight, camera, diagnostic_node_half,
                       MapBgfx2dStyleBinding::Mode::Diagnostic, entity_style, &stats);
    for (const int index : visible) {
        const ChunkBuffers &buffers = this->chunk_buffers[index];
        if (!bgfx::isValid(buffers.icon_vertices) || buffers.icon_vertex_count == 0)
            continue;
        setCameraUniforms(chunks[index], camera);
        set_vec4(this->icon_style_uniform, float(style.icon_size_px), 0.0f, 0.0f, 0.0f);
        bgfx::setVertexBuffer(0, buffers.icon_vertices, 0, buffers.icon_vertex_count);
        entity_style.bind(MapBgfx2dStyleBinding::Mode::Symbology);
        bgfx::setTexture(0, this->icon_atlas_sampler, this->icon_atlas_texture,
                         uint32_t(kAtlasSamplerFlags));
        bgfx::setState(kNetworkState);
        bgfx::submit(view_id, this->icon_program);
        ++stats.draw_calls;
    }
    return stats;
}

void MapBgfx2dGpu::drawHighlightLines(bgfx::ViewId view_id, const HighlightLayer &layer, const MapBgfx2d::Camera &camera,
                                      double half_width_px, MapBgfx2dStyleBinding::Mode mode,
                                      const MapBgfx2dStyleBinding &entity_style, FrameStats *stats) const
{
    const QVector<MapBgfx2dMeshChunk> &chunks = layer.mesh.chunks();
    if (chunks.size() != layer.buffers.size())
        return;
    const float half_width = float(half_width_px);
    const float data[4] = {half_width, half_width, 0.0f, 0.0f};
    for (qsizetype index = 0; index < chunks.size(); ++index) {
        const ChunkBuffers &buffers = layer.buffers[index];
        if (!bgfx::isValid(buffers.link_vertices) || buffers.link_index_count == 0
            || !MapBgfx2dChunking::chunkVisible(chunks[index], camera, 2.0 * half_width_px + 2.0))
            continue;
        setCameraUniforms(chunks[index], camera);
        bgfx::setUniform(this->line_style_uniform, data);
        bgfx::setVertexBuffer(0, buffers.link_vertices);
        entity_style.bind(mode, MapBgfx2dStyleBinding::Table::Links);
        bgfx::setIndexBuffer(buffers.link_indices, 0, buffers.link_index_count);
        bgfx::setState(kNetworkState);
        bgfx::submit(view_id, this->line_program);
        ++stats->draw_calls;
    }
}

void MapBgfx2dGpu::drawHighlightNodes(bgfx::ViewId view_id, const HighlightLayer &layer, const MapBgfx2d::Camera &camera,
                                      double half_size_px, MapBgfx2dStyleBinding::Mode mode,
                                      const MapBgfx2dStyleBinding &entity_style, FrameStats *stats) const
{
    const QVector<MapBgfx2dMeshChunk> &chunks = layer.mesh.chunks();
    if (chunks.size() != layer.buffers.size())
        return;
    const float data[4] = {float(half_size_px), 0.0f, 0.0f, 0.0f};
    for (qsizetype index = 0; index < chunks.size(); ++index) {
        const ChunkBuffers &buffers = layer.buffers[index];
        if (!bgfx::isValid(buffers.node_vertices) || buffers.node_vertex_count == 0
            || !MapBgfx2dChunking::chunkVisible(chunks[index], camera, half_size_px + 2.0))
            continue;
        setCameraUniforms(chunks[index], camera);
        bgfx::setUniform(this->marker_style_uniform, data);
        bgfx::setVertexBuffer(0, buffers.node_vertices, 0, buffers.node_vertex_count);
        entity_style.bind(mode);
        bgfx::setState(kNetworkState);
        bgfx::submit(view_id, this->point_program);
        ++stats->draw_calls;
    }
}

void MapBgfx2dGpu::release()
{
    releaseChunks();
    releaseBuffers(&this->selected_highlight.buffers);
    releaseBuffers(&this->diagnostic_highlight.buffers);
    this->selected_highlight.mesh.reset();
    this->diagnostic_highlight.mesh.reset();
    // GPU buffers are gone: the next synchronize() must rebuild and upload
    // even though the network geometry revision did not change.
    this->mesh.reset();
    destroyHandle(&this->icon_atlas_texture);
    destroyHandle(&this->icon_atlas_sampler);
    destroyHandle(&this->camera_uniform);
    destroyHandle(&this->pixel_uniform);
    destroyHandle(&this->line_style_uniform);
    destroyHandle(&this->marker_style_uniform);
    destroyHandle(&this->icon_style_uniform);
    destroyHandle(&this->flow_style_uniform);
    destroyHandle(&this->flow_limits_uniform);
    destroyHandle(&this->heatmap_style_uniform);
    destroyHandle(&this->point_program);
    destroyHandle(&this->line_program);
    destroyHandle(&this->icon_program);
    destroyHandle(&this->flow_program);
    destroyHandle(&this->heatmap_program);
}
