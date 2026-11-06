#include "map/bgfx/map_bgfx_globe_vertex_layouts.h"

#include <cstdint>
#include <limits>

namespace
{
constexpr std::size_t kFloatBytes = sizeof(float);
constexpr std::size_t kLinkAttributes = 16 * kFloatBytes;
constexpr std::size_t kNodeAttributes = 11 * kFloatBytes;
constexpr std::size_t kIconAttributes = 11 * kFloatBytes;
constexpr std::size_t kJunctionAttributes = 10 * kFloatBytes;
constexpr std::size_t kModelAttributes = 9 * kFloatBytes;

bool validStride(std::size_t stride, std::size_t attributes, std::size_t minimum_tail)
{
    if (stride < attributes || stride > std::numeric_limits<std::uint16_t>::max()
        || stride % kFloatBytes != 0)
        return false;
    const std::size_t tail = stride - attributes;
    return tail >= minimum_tail && tail <= std::numeric_limits<std::uint8_t>::max();
}
}

bool MapBgfxGlobeVertexLayouts::initialize(const Strides &strides)
{
    // Reject incompatible formats before constructing any of the layouts.
    // Network links/nodes/icons have two metadata fields, models one.
    // A junction has no trailing metadata. The exact host byte strides are
    // supplied by GUI; float-prefix ABI offsets are asserted there.
    if (!validStride(strides.link, kLinkAttributes, 2 * sizeof(std::uint32_t))
        || !validStride(strides.node, kNodeAttributes, 2 * sizeof(std::uint32_t))
        || !validStride(strides.icon, kIconAttributes, 2 * sizeof(std::uint32_t))
        || strides.junction != kJunctionAttributes
        || !validStride(strides.model, kModelAttributes, sizeof(std::uint32_t)))
        return false;

    this->link_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 3, bgfx::AttribType::Float)
        .skip(static_cast<std::uint8_t>(strides.link - kLinkAttributes))
        .end();
    this->node_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord4, 1, bgfx::AttribType::Float)
        .skip(static_cast<std::uint8_t>(strides.node - kNodeAttributes))
        .end();
    this->icon_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .skip(static_cast<std::uint8_t>(strides.icon - kIconAttributes))
        .end();
    this->junction_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .end();
    // Tank and reservoir model vertices use the same attribute ABI.
    this->model_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float)
        .skip(static_cast<std::uint8_t>(strides.model - kModelAttributes))
        .end();

    return this->link_layout.getStride() == strides.link
        && this->node_layout.getStride() == strides.node
        && this->icon_layout.getStride() == strides.icon
        && this->junction_layout.getStride() == strides.junction
        && this->model_layout.getStride() == strides.model;
}
