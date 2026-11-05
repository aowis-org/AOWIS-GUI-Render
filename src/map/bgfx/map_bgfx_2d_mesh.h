#ifndef MAP_BGFX_2D_MESH_H
#define MAP_BGFX_2D_MESH_H

#include "map/bgfx/map_bgfx_2d_scene.h"

#include <QHash>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>

// Camera-independent CPU staging for the static bgfx network buffers.
//
// Spatial chunks: primitives are grouped by a fixed world grid (GridZoom
// tiles, ~9.8 km at the equator). Every chunk has its own local origin at its
// cell centre, so vertex coordinates stay small floats for networks of any
// extent (worst-case error ~1e-11 world = ~0.002 px at zoom 19), and every
// chunk has bounds for per-frame view culling. Only uniforms change with the
// camera; the mesh is rebuilt only when the scene was re-prepared (geometry
// revision or declutter toggle, MapBgfx2dScene::preparationId()).
//
// Thick lines: each segment is one indexed quad. Every vertex stores its own
// endpoint, the opposite endpoint and a side (+1/-1); vs_line_2d.sc extrudes
// it in screen pixels perpendicular to the segment and extends it by the
// cap length along it, so consecutive segments of a polyline close their
// joins. GL line primitives cannot do this: they are 1 px on core profiles
// and WebGL.
//
// Geometry only, no colours: every vertex carries the render id of its
// entity, and the vertex shaders colour it from the per-entity data
// textures (MapBgfxEntityStyle, shaders/entity_style.sh). The buffers are
// immutable until the geometry revision changes; sizes are uniforms. A
// hydraulic time step, a metric, palette or range change, hiding, selection
// and diagnostics therefore never touch a vertex buffer.
//
// Heatmap: every node marker quad doubles as the node's heatmap spot. The
// heatmap pass draws node_vertices with the radius as size uniform in the
// heatmap mode (the heatmap ramp, no spot without a value), so the heatmap
// adds no geometry and its radius scales continuously.
//
// Flow chevrons are zoom-independent: every link gets chevrons at fixed world
// positions centred on the link, organised as a level hierarchy (position j
// has level ctz(j), so level k is every 2^k-th position). The vertex shader
// shows a level once its on-screen spacing reaches FlowSpacingPixels and
// fades it in over one octave, from the continuous zoom: no chevron ever
// appears or disappears at an imagery level boundary.
struct MapBgfx2dGpuVertex
{
    float x = 0.0f;         // marker position, chunk-local world
    float y = 0.0f;
    float corner_x = 0.0f;  // -1 / +1, scaled by the marker size uniform
    float corner_y = 0.0f;
    float entity = 0.0f;    // node render id
    float reserved = 0.0f;
};

struct MapBgfx2dLineVertex
{
    float x = 0.0f;        // own endpoint, chunk-local world
    float y = 0.0f;
    float other_x = 0.0f;  // opposite endpoint, chunk-local world
    float other_y = 0.0f;
    float side = 0.0f;     // +1 / -1
    // Render id of the link; of the demand point for attachment lines.
    float entity = 0.0f;
};

struct MapBgfx2dIconVertex
{
    float x = 0.0f;          // icon centre, chunk-local world
    float y = 0.0f;
    float offset_x = 0.0f;   // corner offset in units of the icon size
    float offset_y = 0.0f;
    float u = 0.0f;          // icon atlas coordinates
    float v = 0.0f;
    float entity = 0.0f;     // render id of the node or link
    float on_link = 0.0f;    // 1: a link icon (pump, valve), 0: a node icon
};

struct MapBgfx2dChevronVertex
{
    float x = 0.0f;          // chevron centre, chunk-local world
    float y = 0.0f;
    float direction_x = 0.0f; // link direction (unit, world == screen axes)
    float direction_y = 0.0f;
    float corner_u = 0.0f;   // -1 / +1 along the direction
    float corner_v = 0.0f;   // -1 / +1 across
    float level = 0.0f;      // hierarchy level; MapBgfx2dFlow::AlwaysLevel for the centre
    float link_length = 0.0f; // whole link length, world units
    // Link render id: the flow direction (which way the chevron points, or
    // none) and its colour come from the link's data.
    float entity = 0.0f;
    float reserved = 0.0f;
};

// Atlas placement for one entity type; supplied by the caller so this header
// stays free of the QPainter-based atlas (see map_icon_atlas.h).
struct MapBgfx2dIconSprite
{
    bool valid = false;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float half_width_ratio = 0.5f;
    float half_height_ratio = 0.5f;
};

struct MapBgfx2dClipCamera
{
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float pixel_x = 1.0f;
    float pixel_y = 1.0f;
};

struct MapBgfx2dWorldRect
{
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    void include(double x, double y)
    {
        this->left = std::min(this->left, x);
        this->right = std::max(this->right, x);
        this->top = std::min(this->top, y);
        this->bottom = std::max(this->bottom, y);
    }
};

struct MapBgfx2dMeshChunk
{
    MapBgfx2d::WorldPoint origin;   // cell centre, x in [0,1)
    MapBgfx2dWorldRect bounds;      // absolute world, same x frame as origin
    QVector<MapBgfx2dLineVertex> link_vertices;
    QVector<quint32> link_indices;
    QVector<MapBgfx2dLineVertex> attachment_vertices;
    QVector<quint32> attachment_indices;
    QVector<MapBgfx2dGpuVertex> node_vertices;
    QVector<MapBgfx2dIconVertex> icon_vertices;
    QVector<MapBgfx2dChevronVertex> chevron_vertices;
    QVector<quint32> chevron_indices;
};

namespace MapBgfx2dChunking
{
constexpr int GridZoom = 12;
constexpr double CellSize = 1.0 / double(1 << GridZoom);

inline quint64 cellKey(int cell_x, int cell_y)
{
    return (quint64(quint32(cell_x)) << 32) | quint64(quint32(cell_y));
}

inline int wrapCellX(int cell_x)
{
    const int count = 1 << GridZoom;
    const int wrapped = cell_x % count;
    return wrapped < 0 ? wrapped + count : wrapped;
}

inline MapBgfx2dClipCamera cameraUniform(const MapBgfx2d::WorldPoint &origin,
                                         const MapBgfx2d::Camera &camera)
{
    MapBgfx2dClipCamera result;
    // Subtract the high-precision world coordinates on the CPU, before
    // converting the small chunk-to-camera delta to a GPU float.
    result.origin_x = float(2.0 *
        (MapBgfx2d::wrapNearest(origin.x, camera.center.x) - camera.center.x)
        * camera.world_pixels / camera.viewport_width);
    result.origin_y = float(-2.0 * (origin.y - camera.center.y)
        * camera.world_pixels / camera.viewport_height);
    result.scale_x = float(2.0 * camera.world_pixels / camera.viewport_width);
    result.scale_y = float(-2.0 * camera.world_pixels / camera.viewport_height);
    result.pixel_x = float(2.0 / camera.viewport_width);
    result.pixel_y = float(-2.0 / camera.viewport_height);
    return result;
}

// margin_pixels: largest screen-space extent of any symbol beyond its world
// position (half line width + cap, half marker/icon size, chevron size).
inline bool chunkVisible(const MapBgfx2dMeshChunk &chunk, const MapBgfx2d::Camera &camera,
                         double margin_pixels)
{
    if (!(camera.world_pixels > 0.0))
        return false;
    const double shift = MapBgfx2d::wrapNearest(chunk.origin.x, camera.center.x) - chunk.origin.x;
    const double half_width = (camera.viewport_width * 0.5 + margin_pixels) / camera.world_pixels;
    const double half_height = (camera.viewport_height * 0.5 + margin_pixels) / camera.world_pixels;
    return chunk.bounds.right + shift >= camera.center.x - half_width
        && chunk.bounds.left + shift <= camera.center.x + half_width
        && chunk.bounds.bottom >= camera.center.y - half_height
        && chunk.bounds.top <= camera.center.y + half_height;
}

// Pixels for a size in metres at the continuous zoom and the camera
// latitude (Web Mercator scale 1/cos(lat)). Continuous in the zoom, so a
// metre-based size scales smoothly instead of per imagery level.
inline double pixelsForMeters(double meters, double latitude_deg, double continuous_zoom)
{
    const double meters_per_pixel = 1.0 / (MapBgfx2dSceneMath::worldUnitsPerMeter(latitude_deg)
        * 256.0 * std::exp2(continuous_zoom));
    if (!(meters_per_pixel > 0.0) || !std::isfinite(meters))
        return 0.0;
    return meters / meters_per_pixel;
}
}

namespace MapBgfx2dFlow
{
// Same visual rhythm as QRhi: ~100 px between chevrons, none on links
// shorter than 18 px on screen.
constexpr double SpacingPixels = 100.0;
constexpr double MinimumLinkPixels = 18.0;
// Base spacing: SpacingPixels at the deepest camera zoom (MapModel::MaxZoom).
constexpr double FinestZoom = 19.0;
constexpr double BaseSpacingWorld = SpacingPixels / (256.0 * 524288.0); // 2^19
// At most 2 * MaximumPerSide + 1 chevrons per link; longer links use a
// coarser base level, i.e. wider spacing at the deepest zoom.
constexpr int MaximumPerSide = 8;
constexpr float AlwaysLevel = 1.0e6f;

inline int trailingZeros(int value)
{
    int count = 0;
    while (value > 0 && (value & 1) == 0)
    {
        value >>= 1;
        ++count;
    }
    return count;
}

// Continuous threshold level for a camera: levels >= it are fully shown,
// levels in (threshold - 1, threshold) fade in. Shader mirror: vs_flow_2d.sc.
inline double thresholdLevel(double world_pixels)
{
    return std::log2(SpacingPixels / (BaseSpacingWorld * world_pixels));
}
}

class MapBgfx2dMesh
{
public:
    using IconLookup = std::function<MapBgfx2dIconSprite(quint8)>;

    bool prepare(const MapBgfx2dScene &scene, const IconLookup &icons)
    {
        if (this->initialized && this->scene_preparation_id == scene.preparationId())
            return false;
        reset();

        for (const MapBgfx2dPolyline &polyline : scene.linkPolylines())
        {
            for (qsizetype index = 1; index < polyline.points.size(); ++index)
            {
                appendLine({polyline.points[index - 1], polyline.points[index],
                            polyline.render_id}, false);
            }
            if (this->chevrons_enabled)
                appendChevrons(polyline);
            // Link icons (pumps, valves) at the middle of the link length.
            if (!polyline.pipe)
            {
                const MapBgfx2dIconSprite sprite = icons ? icons(polyline.entity_type)
                                                         : MapBgfx2dIconSprite();
                MapBgfx2d::WorldPoint middle;
                if (sprite.valid && polylineMiddle(polyline, &middle))
                {
                    appendIcon(middle, sprite, polyline.render_id, true);
                }
            }
        }
        for (const MapBgfx2dSegment &segment : scene.attachments())
            appendLine(segment, true);
        for (const MapBgfx2dPoint &point : scene.networkNodes())
        {
            appendMarker(point);
            if (!point.junction && icons)
            {
                const MapBgfx2dIconSprite sprite = icons(point.entity_type);
                if (sprite.valid)
                    appendIcon(point.position, sprite, point.render_id, false);
            }
        }

        this->geometry_revision = scene.geometryRevision();
        this->scene_preparation_id = scene.preparationId();
        this->initialized = true;
        ++this->preparation_count;
        return true;
    }

    // Highlight meshes need no flow chevrons.
    void setChevronsEnabled(bool enabled) { this->chevrons_enabled = enabled; }

    // Forget the staged revision (GPU resources were released).
    void reset()
    {
        this->chunk_list.clear();
        this->chunk_by_cell.clear();
        this->initialized = false;
    }

    const QVector<MapBgfx2dMeshChunk> &chunks() const { return this->chunk_list; }

    quint64 geometryRevision() const { return this->geometry_revision; }
    quint64 preparationCount() const { return this->preparation_count; }

    // Where prepare() places a link icon: the middle of the link length.
    // Shared with picking (map_bgfx_2d_picking.h) so both always agree.
    static bool polylineMiddle(const MapBgfx2dPolyline &polyline, MapBgfx2d::WorldPoint *point)
    {
        MapBgfx2d::WorldPoint direction;
        return pointAlong(polyline, 0.5 * polylineLength(polyline), point, &direction);
    }

    qsizetype linkVertexCount() const
    {
        return total([](const MapBgfx2dMeshChunk &chunk) { return chunk.link_vertices.size(); });
    }
    qsizetype linkIndexCount() const
    {
        return total([](const MapBgfx2dMeshChunk &chunk) { return chunk.link_indices.size(); });
    }
    qsizetype attachmentVertexCount() const
    {
        return total([](const MapBgfx2dMeshChunk &chunk) { return chunk.attachment_vertices.size(); });
    }
    qsizetype nodeVertexCount() const
    {
        return total([](const MapBgfx2dMeshChunk &chunk) { return chunk.node_vertices.size(); });
    }
    qsizetype iconVertexCount() const
    {
        return total([](const MapBgfx2dMeshChunk &chunk) { return chunk.icon_vertices.size(); });
    }
    qsizetype chevronCount() const
    {
        return total([](const MapBgfx2dMeshChunk &chunk) { return chunk.chevron_vertices.size() / 4; });
    }

private:
    qsizetype total(const std::function<qsizetype(const MapBgfx2dMeshChunk &)> &count) const
    {
        qsizetype sum = 0;
        for (const MapBgfx2dMeshChunk &chunk : this->chunk_list)
            sum += count(chunk);
        return sum;
    }

    static double polylineLength(const MapBgfx2dPolyline &polyline)
    {
        double length = 0.0;
        for (qsizetype index = 1; index < polyline.points.size(); ++index)
            length += std::hypot(polyline.points[index].x - polyline.points[index - 1].x,
                                 polyline.points[index].y - polyline.points[index - 1].y);
        return length;
    }

    // Point and unit direction at a distance along the polyline.
    static bool pointAlong(const MapBgfx2dPolyline &polyline, double distance,
                           MapBgfx2d::WorldPoint *point, MapBgfx2d::WorldPoint *direction)
    {
        double travelled = 0.0;
        for (qsizetype index = 1; index < polyline.points.size(); ++index)
        {
            const MapBgfx2d::WorldPoint &a = polyline.points[index - 1];
            const MapBgfx2d::WorldPoint &b = polyline.points[index];
            const double length = std::hypot(b.x - a.x, b.y - a.y);
            if (length <= 0.0)
                continue;
            if (travelled + length < distance && index + 1 < polyline.points.size())
            {
                travelled += length;
                continue;
            }
            const double ratio = std::clamp((distance - travelled) / length, 0.0, 1.0);
            *point = {a.x + (b.x - a.x) * ratio, a.y + (b.y - a.y) * ratio};
            *direction = {(b.x - a.x) / length, (b.y - a.y) / length};
            return true;
        }
        return false;
    }

    MapBgfx2dMeshChunk &chunkFor(double world_x, double world_y)
    {
        const int cell_x = MapBgfx2dChunking::wrapCellX(
            int(std::floor(world_x / MapBgfx2dChunking::CellSize)));
        const int cell_y = std::clamp(int(std::floor(world_y / MapBgfx2dChunking::CellSize)),
                                      0, (1 << MapBgfx2dChunking::GridZoom) - 1);
        const quint64 key = MapBgfx2dChunking::cellKey(cell_x, cell_y);
        QHash<quint64, int>::const_iterator found = this->chunk_by_cell.constFind(key);
        if (found != this->chunk_by_cell.cend())
            return this->chunk_list[found.value()];

        MapBgfx2dMeshChunk chunk;
        chunk.origin = {(double(cell_x) + 0.5) * MapBgfx2dChunking::CellSize,
                        (double(cell_y) + 0.5) * MapBgfx2dChunking::CellSize};
        const double inf = std::numeric_limits<double>::infinity();
        chunk.bounds = {inf, inf, -inf, -inf};
        this->chunk_by_cell.insert(key, int(this->chunk_list.size()));
        this->chunk_list.append(chunk);
        return this->chunk_list.last();
    }

    // Chunk-local coordinates of a world point (x wrapped next to the origin).
    static void localPosition(const MapBgfx2dMeshChunk &chunk, const MapBgfx2d::WorldPoint &point,
                              double *world_x, float *x, float *y)
    {
        *world_x = MapBgfx2d::wrapNearest(point.x, chunk.origin.x);
        *x = float(*world_x - chunk.origin.x);
        *y = float(point.y - chunk.origin.y);
    }

    void appendLine(const MapBgfx2dSegment &segment, bool attachment)
    {
        // Keep the segment continuous across +-180 deg, then assign it to the
        // cell of its midpoint and express both ends in that chunk's frame.
        const double end_x = MapBgfx2d::wrapNearest(segment.end.x, segment.start.x);
        MapBgfx2dMeshChunk &chunk = chunkFor(0.5 * (segment.start.x + end_x),
                                             0.5 * (segment.start.y + segment.end.y));
        const double sx = MapBgfx2d::wrapNearest(segment.start.x, chunk.origin.x);
        const double ex = MapBgfx2d::wrapNearest(end_x, sx);
        chunk.bounds.include(sx, segment.start.y);
        chunk.bounds.include(ex, segment.end.y);

        const float ax = float(sx - chunk.origin.x);
        const float ay = float(segment.start.y - chunk.origin.y);
        const float bx = float(ex - chunk.origin.x);
        const float by = float(segment.end.y - chunk.origin.y);
        QVector<MapBgfx2dLineVertex> &vertices =
            attachment ? chunk.attachment_vertices : chunk.link_vertices;
        QVector<quint32> &indices = attachment ? chunk.attachment_indices : chunk.link_indices;
        const quint32 base = quint32(vertices.size());
        // With n = normal of A->B: 0 = A+n, 1 = A-n, 2 = B-n, 3 = B+n. At B the
        // shader's direction is B->A, so its normal is -n and side +1 gives
        // B-n. Triangles (0,1,2) and (0,2,3) cover the quad.
        const float entity = float(segment.render_id);
        vertices.append({ax, ay, bx, by, 1.0f, entity});
        vertices.append({ax, ay, bx, by, -1.0f, entity});
        vertices.append({bx, by, ax, ay, 1.0f, entity});
        vertices.append({bx, by, ax, ay, -1.0f, entity});
        const quint32 quad[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        for (const quint32 index : quad)
            indices.append(index);
    }

    void appendMarker(const MapBgfx2dPoint &point)
    {
        MapBgfx2dMeshChunk &chunk = chunkFor(point.position.x, point.position.y);
        double world_x = 0.0;
        float x = 0.0f;
        float y = 0.0f;
        localPosition(chunk, point.position, &world_x, &x, &y);
        chunk.bounds.include(world_x, point.position.y);
        // Two static triangles with unit corners; the vertex shader scales
        // them by the marker size uniform and the fragment shader cuts an
        // antialiased circle. Not GL_POINTS, whose size and support differ
        // across desktop GL and WebGL 2.
        const float entity = float(point.render_id);
        chunk.node_vertices.append({x, y, -1.0f, -1.0f, entity, 0.0f});
        chunk.node_vertices.append({x, y,  1.0f, -1.0f, entity, 0.0f});
        chunk.node_vertices.append({x, y,  1.0f,  1.0f, entity, 0.0f});
        chunk.node_vertices.append({x, y, -1.0f, -1.0f, entity, 0.0f});
        chunk.node_vertices.append({x, y,  1.0f,  1.0f, entity, 0.0f});
        chunk.node_vertices.append({x, y, -1.0f,  1.0f, entity, 0.0f});
    }

    void appendIcon(const MapBgfx2d::WorldPoint &position, const MapBgfx2dIconSprite &sprite,
                    quint32 render_id, bool on_link)
    {
        MapBgfx2dMeshChunk &chunk = chunkFor(position.x, position.y);
        double world_x = 0.0;
        float x = 0.0f;
        float y = 0.0f;
        localPosition(chunk, position, &world_x, &x, &y);
        chunk.bounds.include(world_x, position.y);
        // Pixel offsets are y-down: corner -1 is the upper edge on screen and
        // samples the top of the atlas cell (v0 = uv_rect.top(); bgfx uploads
        // rows top first, so v grows downwards on every renderer).
        const float corners[6][2] = {
            {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f},
            {-1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}
        };
        for (const float *corner : corners)
        {
            chunk.icon_vertices.append({x, y,
                corner[0] * sprite.half_width_ratio, corner[1] * sprite.half_height_ratio,
                corner[0] < 0.0f ? sprite.u0 : sprite.u1,
                corner[1] < 0.0f ? sprite.v0 : sprite.v1,
                float(render_id), on_link ? 1.0f : 0.0f});
        }
    }

    void appendChevrons(const MapBgfx2dPolyline &polyline)
    {
        const double length = polylineLength(polyline);
        if (!(length > 0.0))
            return;
        // Coarsen the base level of long links so they get at most
        // 2 * MaximumPerSide + 1 chevrons.
        int base_level = 0;
        while (MapBgfx2dFlow::BaseSpacingWorld * std::exp2(base_level)
               * MapBgfx2dFlow::MaximumPerSide < 0.5 * length && base_level < 30)
        {
            ++base_level;
        }
        const double spacing = MapBgfx2dFlow::BaseSpacingWorld * std::exp2(base_level);
        // Pumps/valves carry an icon in the middle; their chevron sits at 30 %
        // (QRhi rule). Pipes are centred.
        const double centre = polyline.pipe
            ? 0.5 * length : 0.3 * length;
        for (int j = -MapBgfx2dFlow::MaximumPerSide; j <= MapBgfx2dFlow::MaximumPerSide; ++j)
        {
            const double distance = centre + double(j) * spacing;
            if (j != 0 && (distance < 0.5 * spacing || distance > length - 0.5 * spacing))
                continue;
            MapBgfx2d::WorldPoint point;
            MapBgfx2d::WorldPoint direction;
            if (!pointAlong(polyline, distance, &point, &direction))
                continue;
            const float level = j == 0
                ? MapBgfx2dFlow::AlwaysLevel
                : float(base_level + MapBgfx2dFlow::trailingZeros(std::abs(j)));
            appendChevron(point, direction, level, float(length), polyline.render_id);
        }
    }

    void appendChevron(const MapBgfx2d::WorldPoint &position, const MapBgfx2d::WorldPoint &direction,
                       float level, float link_length, quint32 render_id)
    {
        MapBgfx2dMeshChunk &chunk = chunkFor(position.x, position.y);
        double world_x = 0.0;
        float x = 0.0f;
        float y = 0.0f;
        localPosition(chunk, position, &world_x, &x, &y);
        chunk.bounds.include(world_x, position.y);
        const quint32 base = quint32(chunk.chevron_vertices.size());
        const float dx = float(direction.x);
        const float dy = float(direction.y);
        const float entity = float(render_id);
        chunk.chevron_vertices.append({x, y, dx, dy, -1.0f, -1.0f, level, link_length, entity, 0.0f});
        chunk.chevron_vertices.append({x, y, dx, dy, 1.0f, -1.0f, level, link_length, entity, 0.0f});
        chunk.chevron_vertices.append({x, y, dx, dy, 1.0f, 1.0f, level, link_length, entity, 0.0f});
        chunk.chevron_vertices.append({x, y, dx, dy, -1.0f, 1.0f, level, link_length, entity, 0.0f});
        const quint32 quad[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        for (const quint32 index : quad)
            chunk.chevron_indices.append(index);
    }

    QVector<MapBgfx2dMeshChunk> chunk_list;
    QHash<quint64, int> chunk_by_cell;
    quint64 geometry_revision = 0;
    quint64 scene_preparation_id = 0;
    quint64 preparation_count = 0;
    bool chevrons_enabled = true;
    bool initialized = false;
};

#endif // MAP_BGFX_2D_MESH_H
