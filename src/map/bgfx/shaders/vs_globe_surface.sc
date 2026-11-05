$input a_position, a_normal, a_texcoord0
$output v_texcoord0, v_normal, v_light

#include <bgfx_shader.sh>
#include "globe_hillshade.sh"

// Globe terrain surface (map_bgfx_globe_surface.h). Positions are metres
// relative to the sticky render origin (MapGlobeCamera), so float precision
// holds at any place on the planet; the matrix is the camera's canonical
// view-projection with the backend's depth-range correction applied.
uniform mat4 u_globe_view_proj;
// Where this tile's imagery lies in the imagery atlas: xy offset, zw scale.
// The tile's own cell, a sub-rectangle of an ancestor's cell while its own
// imagery is missing, or a solid-colour texel (no imagery, polar caps).
uniform vec4 u_globe_tile;

void main()
{
    gl_Position = mul(u_globe_view_proj, vec4(a_position, 1.0));
    v_texcoord0 = u_globe_tile.xy + a_texcoord0 * u_globe_tile.zw;
    v_normal = globeHillshadeNormal(a_normal);
    v_light = globeHillshadeLight(a_position);
}
