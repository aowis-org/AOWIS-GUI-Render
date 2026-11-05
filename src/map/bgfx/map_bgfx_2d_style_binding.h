#ifndef MAP_BGFX_2D_STYLE_BINDING_H
#define MAP_BGFX_2D_STYLE_BINDING_H

// Contract for the production 2D bgfx network submission. The GUI retains
// ownership of hydraulic values, palettes, selection and diagnostic states.
// Other clients can supply the same shader-compatible textures/uniforms.
class MapBgfx2dStyleBinding
{
public:
    enum class Mode { VertexColor = 0, Symbology = 1, Selection = 2, Diagnostic = 3, Heatmap = 4 };
    enum class Table { Nodes = 0, Links = 1 };
    virtual ~MapBgfx2dStyleBinding() = default;
    virtual void bind(Mode mode, Table table = Table::Nodes,
                      bool tint_selected_icons = false) const = 0;
};

#endif // MAP_BGFX_2D_STYLE_BINDING_H
