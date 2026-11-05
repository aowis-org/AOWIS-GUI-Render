$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_texcoord1, v_normal, v_light

#include <bgfx_shader.sh>
#include "globe_hillshade.sh"

// Globe terrain tile with a baked heatmap (vs_globe_surface plus the tile's
// heatmap cell): u_globe_tile maps the tile's texture coordinates into its
// imagery, u_globe_heatmap into its heatmap atlas cell (xy offset, zw scale).
uniform mat4 u_globe_view_proj;
uniform vec4 u_globe_tile;
uniform vec4 u_globe_heatmap;

void main()
{
    gl_Position = mul(u_globe_view_proj, vec4(a_position, 1.0));
    v_texcoord0 = u_globe_tile.xy + a_texcoord0 * u_globe_tile.zw;
    v_texcoord1 = u_globe_heatmap.xy + a_texcoord0 * u_globe_heatmap.zw;
    v_normal = globeHillshadeNormal(a_normal);
    v_light = globeHillshadeLight(a_position);
}
