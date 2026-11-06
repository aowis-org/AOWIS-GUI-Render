#include "map/bgfx/map_bgfx_globe_vertex_layouts.h"

#include <cstddef>
#include <iostream>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe layout regression at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

static bool matches(const bgfx::VertexLayout &layout, std::size_t stride,
                    const bgfx::Attrib::Enum *attributes, const std::uint8_t *components,
                    std::size_t count)
{
    if (layout.getStride() != stride || layout.attributes.size() != count)
        return false;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (layout.attributes[index].id != attributes[index]
            || layout.attributes[index].components != components[index])
            return false;
    }
    return true;
}

int main()
{
    MapBgfxGlobeVertexLayouts layouts;
    // Representative production host sizeof() values: link 16 floats+2 IDs,
    // node 11 floats+2 IDs, icon 11 floats+2 IDs, junction 10 floats,
    // model 9 floats+one ID.
    const MapBgfxGlobeVertexLayouts::Strides input{72, 52, 52, 40, 40};
    CHECK(layouts.initialize(input));
    const bgfx::Attrib::Enum link[] = {bgfx::Attrib::Position, bgfx::Attrib::TexCoord0,
        bgfx::Attrib::TexCoord1, bgfx::Attrib::Color0, bgfx::Attrib::TexCoord2, bgfx::Attrib::TexCoord3};
    const std::uint8_t link_components[] = {3,3,2,4,1,3};
    CHECK(matches(layouts.link(), 72, link, link_components, 6));
    const bgfx::Attrib::Enum node[] = {bgfx::Attrib::Position, bgfx::Attrib::TexCoord1,
        bgfx::Attrib::Color0, bgfx::Attrib::TexCoord2, bgfx::Attrib::TexCoord4};
    const std::uint8_t node_components[] = {3,2,4,1,1};
    CHECK(matches(layouts.node(), 52, node, node_components, 5));
    const bgfx::Attrib::Enum icon[] = {bgfx::Attrib::Position, bgfx::Attrib::TexCoord1,
        bgfx::Attrib::TexCoord5, bgfx::Attrib::Color0};
    const std::uint8_t icon_components[] = {3,2,2,4};
    CHECK(matches(layouts.icon(), 52, icon, icon_components, 4));
    const bgfx::Attrib::Enum junction[] = {bgfx::Attrib::Position, bgfx::Attrib::TexCoord1,
        bgfx::Attrib::Color0, bgfx::Attrib::TexCoord2};
    const std::uint8_t junction_components[] = {3,2,4,1};
    CHECK(matches(layouts.junction(), 40, junction, junction_components, 4));
    const bgfx::Attrib::Enum model[] = {bgfx::Attrib::Position, bgfx::Attrib::Normal,
        bgfx::Attrib::TexCoord5, bgfx::Attrib::TexCoord2};
    const std::uint8_t model_components[] = {3,3,2,1};
    CHECK(matches(layouts.model(), 40, model, model_components, 4));

    CHECK(!layouts.initialize({0,52,52,40,40}));
    CHECK(!layouts.initialize({60,52,52,40,40}));
    CHECK(!layouts.initialize({64,52,52,40,40}));
    CHECK(!layouts.initialize({72,52,52,40,36}));
    CHECK(!layouts.initialize({72,51,52,40,40}));
    CHECK(!layouts.initialize({72,52,52,44,40}));
    CHECK(!layouts.initialize({72,52,52,40,std::numeric_limits<std::size_t>::max()}));
    CHECK(layouts.initialize(input)); // reinitialization on a new device
    return 0;
}
#undef CHECK
