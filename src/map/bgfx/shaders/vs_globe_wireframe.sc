$input a_position

#include <bgfx_shader.sh>

// Terrain wireframe (debug view): the surface's triangle edges as lines.
uniform mat4 u_globe_view_proj;

void main()
{
    gl_Position = mul(u_globe_view_proj, vec4(a_position, 1.0));
}
