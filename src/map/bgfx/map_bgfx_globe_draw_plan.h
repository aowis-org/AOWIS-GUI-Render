#ifndef MAP_BGFX_GLOBE_DRAW_PLAN_H
#define MAP_BGFX_GLOBE_DRAW_PLAN_H

// Production globe-network pass order. This deliberately has no Qt, bgfx,
// GUI, or hydraulic-model dependency; Render owns the order in which the
// shared bgfx submission backend draws its streams.
#include <cstdint>

enum class MapBgfxGlobeUndergroundMode : std::uint8_t
{
    Solid,
    XRay,
    Hide
};

enum class MapBgfxGlobeStream : std::uint8_t
{
    Link,
    Node,
    SelectedLink,
    SelectedNode,
    DiagnosticLink,
    DiagnosticNode,
    Flow,
    Icon,
    UndergroundLink,
    Junction,
    UndergroundJunction,
    Tank,
    Reservoir,
    Count
};

enum class MapBgfxGlobeProgram : std::uint8_t
{
    Link,
    LinkXRay,
    Flow,
    Node,
    Junction,
    JunctionXRay,
    Icon,
    Model,
    Count
};

enum class MapBgfxGlobeState : std::uint8_t
{
    Overlay,
    DepthWrite,
    DepthTest,
    Model
};

enum class MapBgfxGlobeTexture : std::uint8_t
{
    None,
    Tank,
    Reservoir,
    Icon
};

struct MapBgfxGlobeDrawPass
{
    MapBgfxGlobeStream stream;
    MapBgfxGlobeProgram program;
    MapBgfxGlobeState state;
    bool vertex_colors;
    MapBgfxGlobeTexture texture;
    bool ordered_icons;
};

// Keeping pass ordering in one place prevents desktop and future mobile
// consumers from subtly diverging (notably selection, X-Ray and icons).
template <typename Callback>
void mapBgfxGlobeVisitPasses(MapBgfxGlobeUndergroundMode underground, bool show_junctions,
                             bool has_icon_atlas, Callback &&callback)
{
    const MapBgfxGlobeTexture none = MapBgfxGlobeTexture::None;
    const MapBgfxGlobeState overlay = MapBgfxGlobeState::Overlay;
    const MapBgfxGlobeState depth = MapBgfxGlobeState::DepthWrite;
    if (underground == MapBgfxGlobeUndergroundMode::Solid)
    {
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Link, MapBgfxGlobeProgram::Link, overlay, false, none, false});
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Node, MapBgfxGlobeProgram::Node, overlay, false, none, false});
        if (show_junctions)
            callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Junction, MapBgfxGlobeProgram::Junction, overlay, false, none, false});
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::SelectedNode, MapBgfxGlobeProgram::Node, overlay, true, none, false});
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::DiagnosticLink, MapBgfxGlobeProgram::Link, overlay, true, none, false});
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::DiagnosticNode, MapBgfxGlobeProgram::Node, overlay, true, none, false});
    }
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Link, MapBgfxGlobeProgram::Link, depth, false, none, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Flow, MapBgfxGlobeProgram::Flow, MapBgfxGlobeState::DepthTest, false, none, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Node, MapBgfxGlobeProgram::Node, depth, false, none, false});
    if (show_junctions)
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Junction, MapBgfxGlobeProgram::Junction, depth, false, none, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Tank, MapBgfxGlobeProgram::Model, MapBgfxGlobeState::Model, true, MapBgfxGlobeTexture::Tank, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Reservoir, MapBgfxGlobeProgram::Model, MapBgfxGlobeState::Model, true, MapBgfxGlobeTexture::Reservoir, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::SelectedLink, MapBgfxGlobeProgram::Link, overlay, true, none, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::SelectedNode, MapBgfxGlobeProgram::Node, depth, true, none, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::DiagnosticLink, MapBgfxGlobeProgram::Link, depth, true, none, false});
    callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::DiagnosticNode, MapBgfxGlobeProgram::Node, depth, true, none, false});
    if (underground == MapBgfxGlobeUndergroundMode::XRay)
    {
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::UndergroundLink, MapBgfxGlobeProgram::LinkXRay, overlay, false, none, false});
        if (show_junctions)
            callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::UndergroundJunction, MapBgfxGlobeProgram::JunctionXRay, overlay, false, none, false});
    }
    if (has_icon_atlas)
        callback(MapBgfxGlobeDrawPass{MapBgfxGlobeStream::Icon, MapBgfxGlobeProgram::Icon, overlay, false, MapBgfxGlobeTexture::Icon, true});
}

#endif // MAP_BGFX_GLOBE_DRAW_PLAN_H
