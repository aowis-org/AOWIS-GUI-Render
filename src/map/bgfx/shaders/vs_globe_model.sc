$input a_position, a_normal, a_texcoord5, a_texcoord2
$output v_normal, v_texcoord0, v_model

#include <bgfx_shader.sh>
#include "globe_network.sh"

// Tank and reservoir 3D models (MapGlobeTankModelVertex /
// MapGlobeReservoirModelVertex, built on the CPU per instance in the local
// east/north/up frame). a_texcoord2: selected (0/1).
void main()
{
    gl_Position = mul(u_net_view_proj, vec4(a_position, 1.0));
    v_normal = normalize(a_normal);
    v_texcoord0 = a_texcoord5;
    v_model = vec2(a_position.z, a_texcoord2);
}
