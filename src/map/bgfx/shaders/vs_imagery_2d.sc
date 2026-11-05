$input a_position, a_texcoord0
$output v_texcoord0

#include <bgfx_shader.sh>

// Imagery quads arrive in clip space: they are computed per frame on the CPU
// in double precision relative to the camera (map_bgfx_2d_imagery_layout.h).
void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
}
