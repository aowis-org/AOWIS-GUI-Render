#ifndef MAP_BGFX_GLOBE_VERTEX_LAYOUTS_H
#define MAP_BGFX_GLOBE_VERTEX_LAYOUTS_H

#include <bgfx/bgfx.h>

#include <cstddef>

// Production globe-network attribute layouts, owned by Render.
// GUI remains the owner of the CPU scene vertex structs for now. It supplies
// only their sizeof() values; Render defines every shader-facing attribute
// and verifies that each resulting bgfx stride matches the uploaded bytes.
// Vertex formats are tightly packed floats followed by optional host metadata
// (render id and entity type), which the shader does not read.
class MapBgfxGlobeVertexLayouts final
{
public:
    struct Strides
    {
        std::size_t link = 0;
        std::size_t node = 0;
        std::size_t icon = 0;
        std::size_t junction = 0;
        std::size_t model = 0;
    };

    // A false result indicates an unsupported host ABI; no layouts are used.
    // It does not create GPU resources, and may be called again on device init.
    bool initialize(const Strides &strides);

    const bgfx::VertexLayout &link() const { return this->link_layout; }
    const bgfx::VertexLayout &node() const { return this->node_layout; }
    const bgfx::VertexLayout &icon() const { return this->icon_layout; }
    const bgfx::VertexLayout &junction() const { return this->junction_layout; }
    const bgfx::VertexLayout &model() const { return this->model_layout; }

private:
    bgfx::VertexLayout link_layout;
    bgfx::VertexLayout node_layout;
    bgfx::VertexLayout icon_layout;
    bgfx::VertexLayout junction_layout;
    bgfx::VertexLayout model_layout;
};

#endif // MAP_BGFX_GLOBE_VERTEX_LAYOUTS_H
